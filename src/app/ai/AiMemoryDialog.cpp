// =============================================================================
//  app/ai/AiMemoryDialog.cpp - la fenetre "Memoire et apprentissage de l'IA"
//  (voir AiMemoryDialog.hpp).
//
//  Le dessin suit les panneaux voisins : les cartes, les titres en capitales grises
//  et les barres de l'onglet Statistiques (StatisticsPane), la fenetre Historique
//  (son cadre, sa bande de confirmation), les couleurs du theme (les familles a
//  mi-saturation pour les parts, les couleurs d'etat pour ce qui en est un).
// =============================================================================
#include "AiMemoryDialog.hpp"
#include "AiPaths.hpp"
#include "AiStore.hpp"
#include "../Dossiers.hpp"
#include "../../core/Edition.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/TextSearch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iterator>

namespace app {

namespace mem = ai::memory;

namespace {

const gfx::FontId kCaption{11};
const gfx::FontId kNote{12};
const gfx::FontId kSmall{13};
const gfx::FontId kText{15};
const gfx::FontId kBig{24};
constexpr float kPad = 14.f;
constexpr float kGap = 10.f;
const char* const kDots = "\xE2\x80\xA6";
const std::string kDot = " \xC2\xB7 ";

// ---------------------------------------------------------------- le dessin ---
float textW(const ui::PaintContext& ctx, const std::string& s, gfx::FontId f) { return ctx.r.measure(s, f).width; }

std::string fit(const ui::PaintContext& ctx, const std::string& s, gfx::FontId f, float w) {
    if (w <= 0.f) return {};
    if (textW(ctx, s, f) <= w) return s;
    const float dots = textW(ctx, kDots, f);
    auto n = ctx.r.fitCharacters(s, f, std::max(0.f, w - dots));
    n = std::min(n, s.size());
    while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) --n;
    return s.substr(0, n) + kDots;
}

// 1.14.2 (captures) : UN TEXTE COUPE SE LIT EN ENTIER DANS SON INFOBULLE, comme les cases des tables du
// logiciel. Chaque page dessinee retient, a chaque dessin, la place de ses textes coupes (et des quelques
// lignes qui disent un detail technique au survol) ; son liveTooltip les relit.
struct Tip {
    gfx::Rect   rect;
    std::string text;
};
std::vector<Tip>* gTips = nullptr;   // celles de la page en cours de dessin (nul : aucune)

// La page qui se dessine : ses infobulles repartent de zero, et se remplissent pendant le dessin.
class TipScope {
public:
    explicit TipScope(std::vector<Tip>& tips) : previous_(gTips) {
        tips.clear();
        gTips = &tips;
    }
    ~TipScope() { gTips = previous_; }
    TipScope(const TipScope&) = delete;
    TipScope& operator=(const TipScope&) = delete;

private:
    std::vector<Tip>* previous_;
};

void addTip(const gfx::Rect& r, std::string text) {
    if (gTips && r.w > 0.f && r.h > 0.f && !text.empty()) gTips->push_back({r, std::move(text)});
}

std::string tipAt(const std::vector<Tip>& tips, gfx::Point p) {
    for (auto it = tips.rbegin(); it != tips.rend(); ++it)
        if (it->rect.contains(p)) return it->text;
    return {};
}

// Une ligne de texte coupee a `w` pixels ("...") ; coupee, elle se lit en entier au survol.
void drawFit(const ui::PaintContext& ctx, gfx::Point at, const std::string& s, gfx::FontId f, float w, gfx::Color col) {
    const std::string shown = fit(ctx, s, f, w);
    ctx.r.drawText(at, shown, f, col);
    if (shown != s) addTip({at.x, at.y - 2.f, w, ctx.r.lineHeight(f) + 4.f}, s);
}

// Un paragraphe en lignes de `w` pixels au plus (`maxLines` : la derniere finit par ...).
std::vector<std::string> wrap(const ui::PaintContext& ctx, const std::string& s, gfx::FontId f, float w, std::size_t maxLines) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    bool cut = false;
    while (i <= s.size()) {
        const auto j = std::min(s.find_first_of(" \n", i), s.size());
        const std::string word = s.substr(i, j - i);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && textW(ctx, candidate, f) > w) {
            lines.push_back(line);
            line = word;
            if (lines.size() == maxLines) {
                cut = true;
                break;
            }
        } else {
            line = candidate;
        }
        if (j < s.size() && s[j] == '\n') {
            lines.push_back(line);
            line.clear();
            if (lines.size() == maxLines) {
                cut = j + 1 < s.size();
                break;
            }
        }
        i = j + 1;
    }
    if (!cut && !line.empty()) {
        if (lines.size() < maxLines) lines.push_back(line);
        else cut = true;
    }
    if (cut && !lines.empty()) lines.back() = fit(ctx, lines.back() + kDots, f, w);
    for (auto& l : lines) l = fit(ctx, l, f, w);
    return lines;
}

void drawBold(const ui::PaintContext& ctx, gfx::Point at, const std::string& s, gfx::FontId f, gfx::Color c) {
    ctx.r.drawText(at, s, f, c);
    ctx.r.drawText({at.x + 0.6f, at.y}, s, f, c);
}

void textMid(const ui::PaintContext& ctx, float x, const gfx::Rect& band, const std::string& s, gfx::FontId f, gfx::Color c) {
    ctx.r.drawText({x, band.y + (band.h - ctx.r.lineHeight(f)) * 0.5f}, s, f, c);
}

void textRight(const ui::PaintContext& ctx, float right, float y, const std::string& s, gfx::FontId f, gfx::Color c) {
    ctx.r.drawText({right - textW(ctx, s, f), y}, s, f, c);
}

// Une carte (comme l'onglet Statistiques) : un lisere, le fond du panneau.
void card(const ui::PaintContext& ctx, const gfx::Rect& r, gfx::Color border) {
    ctx.r.fillRoundedRect(r, border, 8.f);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, ctx.theme.color.panelBg, 7.f);
}

// Le titre d'un bloc : une icone et des capitales, en gris. Rend sa largeur.
float caption(const ui::PaintContext& ctx, float x, float y, ui::Icon icon, const std::string& text, gfx::Color col) {
    ui::drawIcon(ctx.r, icon, {x, y, 13.f, 13.f}, col);
    ctx.r.drawText({x + 19.f, y}, text, kCaption, col);
    return 19.f + textW(ctx, text, kCaption);
}

// Une jauge : la piste, puis la part remplie.
void meter(const ui::PaintContext& ctx, const gfx::Rect& r, double fraction, gfx::Color fill) {
    ctx.r.fillRoundedRect(r, ctx.theme.color.border, r.h * 0.5f);
    const double f = std::clamp(fraction, 0.0, 1.0);
    if (f <= 0.0) return;
    const float w = std::max(r.h, static_cast<float>(static_cast<double>(r.w) * f));
    ctx.r.fillRoundedRect({r.x, r.y, std::min(r.w, w), r.h}, fill, r.h * 0.5f);
}

// Une barre empilee : ses parts, dans l'ordre (1 px au moins pour une part non nulle).
void stackedBar(const ui::PaintContext& ctx, const gfx::Rect& r, const std::vector<std::pair<double, gfx::Color>>& parts, double scale) {
    ctx.r.fillRoundedRect(r, ctx.theme.color.border, 3.f);
    if (scale <= 0.0) return;
    float x = r.x;
    for (const auto& [value, colour] : parts) {
        if (value <= 0.0) continue;
        const float w = std::min(r.right() - x, std::max(2.f, static_cast<float>(static_cast<double>(r.w) * value / scale)));
        if (w <= 0.f) break;
        ctx.r.fillRect({x, r.y, w, r.h}, colour);
        x += w;
    }
}

std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

std::string percent(double part, double whole) {
    if (whole <= 0.0 || part <= 0.0) return "0 %";
    const double p = 100.0 * part / whole;
    char buf[32];
    if (p > 0.0 && p < 0.1) std::snprintf(buf, sizeof buf, "< 0,1 %%");
    else if (p < 10.0) std::snprintf(buf, sizeof buf, "%.1f %%", p);
    else std::snprintf(buf, sizeof buf, "%.0f %%", p);
    std::string s(buf);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

// Les cinq parts de la repartition, et leur couleur (les familles du theme : aucune n'est une couleur d'etat).
struct Group {
    std::string    label;
    std::uintmax_t bytes{0};
    std::size_t    files{0};
};
std::vector<Group> groupsOf(const mem::Measure& m) {
    const auto u = [&](mem::Part p) { return m.of(p); };
    std::vector<Group> g;
    g.push_back({"Conversations", u(mem::Part::Conversations).bytes, u(mem::Part::Conversations).files});
    g.push_back({"Pi\xC3\xA8" "ces jointes", u(mem::Part::Attachments).bytes, u(mem::Part::Attachments).files});
    g.push_back({"Connaissances", u(mem::Part::Knowledge).bytes, u(mem::Part::Knowledge).files});
    g.push_back({"Cache", m.cache().bytes, m.cache().files});
    g.push_back({"Index et donn\xC3\xA9" "es techniques", m.technical().bytes, m.technical().files});
    return g;
}
gfx::Color groupColor(const ui::PaintContext& ctx, std::size_t i) {
    // bleu, violet, sarcelle, abricot, gris : pas le corail des alarmes (il se lirait comme un defaut)
    constexpr int kFamily[] = {4, 1, 2, 3, 5};
    return ctx.theme.onSurface(ctx.theme.brand.family[kFamily[i % 5]]);
}

// Le nom de l'application (IHM Studio) ; sans edition (les essais) : "l'application".
std::string appName() {
    const std::string_view e = core::editionLabel();
    return e.empty() ? std::string("l'application") : std::string(e);
}

// "1,2 Mo" -> {"1,2", "Mo"}
std::pair<std::string, std::string> splitSize(const std::string& s) {
    const auto sp = s.rfind(' ');
    if (sp == std::string::npos) return {s, {}};
    return {s.substr(0, sp), s.substr(sp + 1)};
}

std::string dateOrDash(const std::string& iso) { return iso.empty() ? std::string("\xE2\x80\x94") : mem::displayDate(iso); }

std::string folded(std::string_view s) { return ui::foldForSearch(s); }

// Les lignes de plus de `width` caracteres coupees a un espace (la suite en retrait) : pour l'oeil seulement.
std::string wrapped(const std::string& text, std::size_t width) {
    std::string out;
    std::size_t from = 0;
    while (from <= text.size()) {
        auto nl = text.find('\n', from);
        if (nl == std::string::npos) nl = text.size();
        std::string_view line(text.data() + from, nl - from);
        bool first = true;
        while (line.size() > width) {
            std::size_t cut = line.rfind(' ', width);
            if (cut == std::string_view::npos || cut < width / 2) cut = width;
            while (cut < line.size() && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80) ++cut;
            out += first ? "" : "    ";
            out.append(line.substr(0, cut));
            out += '\n';
            line.remove_prefix(cut);
            while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
            first = false;
        }
        out += first ? "" : "    ";
        out.append(line);
        if (nl < text.size()) out += '\n';
        from = nl + 1;
    }
    return out;
}

} // namespace

// =========================================================================== les lignes ===
//  Un modele de table tout simple : des cases en texte, une cle par ligne, une
//  valeur numerique par case pour trier (les tailles, les comptes).
class AiMemoryDialog::Rows final : public ui::ITableModel {
public:
    struct Row {
        std::vector<std::string>   cells;
        std::vector<double>        sort;      // par colonne ; absent ou NaN : le texte
        std::string                key;
        std::vector<ui::CellStyle> styles;    // par colonne (facultatif)
        std::string                tip;
    };
    std::vector<std::string> headers;
    std::vector<Row>         rows;

    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers.size(); }
    [[nodiscard]] std::string headerText(std::size_t col) const override { return col < headers.size() ? headers[col] : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex row, std::size_t col) const override {
        return row < rows.size() && col < rows[row].cells.size() ? rows[row].cells[col] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex row, std::size_t col) const override {
        return row < rows.size() && col < rows[row].styles.size() ? rows[row].styles[col] : ui::CellStyle{};
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t col) const override {
        const auto num = [&](ui::RowIndex r) { return col < rows[r].sort.size() ? rows[r].sort[col] : std::nan(""); };
        const double x = num(a), y = num(b);
        if (!std::isnan(x) && !std::isnan(y)) return x < y;
        return folded(cellText(a, col)) < folded(cellText(b, col));
    }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex row) const override { return row < rows.size() ? rows[row].tip : std::string{}; }
    [[nodiscard]] int find(std::string_view key) const {
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].key == key) return static_cast<int>(i);
        return -1;
    }
    void reset() { modelReset->emit(); }
};

namespace {
// Une table du centre : une ligne choisie a la fois, lignes alternees.
ui::TableView& makeTable(ui::Widget& parent, const std::string& id, const std::shared_ptr<ui::ITableModel>& model,
                         std::vector<ui::TableView::Column> cols) {
    auto t = std::make_unique<ui::TableView>(id);
    t->setSelectionMode(ui::SelectionMode::Single);
    t->setAlternatingRowColors(true);
    t->setColumns(std::move(cols));
    t->setModel(model);
    return static_cast<ui::TableView&>(parent.addChild(std::move(t)));
}

ui::TableView::Column col(std::string title, float width, ui::Align align = ui::Align::Start) {
    ui::TableView::Column c;
    c.title = std::move(title);
    c.width = width;
    c.align = align;
    return c;
}

// La ligne choisie d'une table, par sa cle (rien si elle n'y est plus).
void selectKey(ui::TableView* t, int row) {
    if (!t) return;
    if (row >= 0) t->selectModelRows({static_cast<ui::RowIndex>(row)}, false);
    else t->selectModelRows({}, false);
}

ui::CellStyle toneStyle(ui::Tone tone, ui::Icon icon = ui::Icon::None) {
    ui::CellStyle s;
    s.fgTone = tone;
    s.icon = icon;
    s.iconTone = tone;
    return s;
}
} // namespace

// ======================================================================= Vue generale ===
class AiMemoryDialog::OverviewPage final : public ui::Widget {
public:
    explicit OverviewPage(AiMemoryDialog& d) : ui::Widget("ia.memoire.vue"), d_(d) {}
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override { return tipAt(tips_, mouse); }
    [[nodiscard]] bool hasTooltip() const override { return !tips_.empty() || ui::Widget::hasTooltip(); }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const TipScope tips(tips_);
        const auto& c = ctx.theme.color;
        const auto r = bounds();
        ctx.r.fillRect(r, c.windowBg);
        const float x0 = r.x + kPad, W = r.w - 2.f * kPad;
        float y = r.y + 12.f;
        // ---- l'etat
        {
            const bool empty = d_.measure_.total.files == 0;
            ui::drawIcon(ctx.r, empty ? ui::Icon::Info : ui::Icon::Ok, {x0, y + 2.f, 16.f, 16.f}, empty ? c.info : c.ok);
            const std::string head = "\xC3\x89tat de la m\xC3\xA9moire IA : ";
            drawBold(ctx, {x0 + 24.f, y}, head, kText, c.text);
            const float hx = x0 + 24.f + textW(ctx, head, kText) + 2.f;
            const std::string right = "M\xC3\xA9moire locale de ce PC" + kDot + d_.connection_;
            const float rw = textW(ctx, right, kSmall);
            drawFit(ctx, {hx, y}, d_.stateText(), kText, x0 + W - rw - 24.f - hx, c.text);
            ctx.r.drawText({x0 + W - rw, y + 2.f}, right, kSmall, c.textMuted);
        }
        y += 32.f;
        // ---- les comptes et les tailles : six cartes
        const float cw = (W - 5.f * kGap) / 6.f, ch = 100.f;
        std::size_t demo = 0, users = 0, ai = 0, pieces = 0;
        std::vector<std::string> projects;
        for (const auto& cv : d_.conversations_) {
            if (cv.demo) ++demo;
            pieces += cv.attachments.size();
            if (!cv.project.empty() && std::find(projects.begin(), projects.end(), cv.project) == projects.end()) projects.push_back(cv.project);
            for (const auto& m : cv.messages) (m.role == "assistant" ? ai : users)++;
        }
        const auto& reg = d_.registry_;
        std::size_t addedCount = 0, here = 0;
        for (const auto& k : reg.all()) {
            if (!k.builtIn) ++addedCount;
            if (mem::hereState(k, d_.spec_.project) == mem::Here::Active) ++here;
        }
        const auto& m = d_.measure_;
        struct Kpi {
            ui::Icon    icon;
            std::string title, big, unit, line1, line2;
            double      meter{-1.0};
            gfx::Color  meterColor{};
        };
        std::vector<Kpi> kpis;
        kpis.push_back({ui::Icon::Comment, "CONVERSATIONS", std::to_string(d_.conversations_.size()), {},
                        d_.conversations_.empty() ? std::string("aucune encore") : demo ? "dont " + std::to_string(demo) + " de d\xC3\xA9monstration" : "enregistr\xC3\xA9" "es",
                        projects.empty() ? std::string("aucun projet cit\xC3\xA9") : plural(projects.size(), "projet cit\xC3\xA9", "projets cit\xC3\xA9s")});
        kpis.push_back({ui::Icon::Document, "MESSAGES", std::to_string(d_.messageCount()), {},
                        std::to_string(users) + " de l'utilisateur" + kDot + std::to_string(ai) + " de l'IA",
                        pieces ? plural(pieces, "pi\xC3\xA8" "ce jointe cit\xC3\xA9" "e", "pi\xC3\xA8" "ces jointes cit\xC3\xA9" "es") : std::string("aucune pi\xC3\xA8" "ce jointe")});
        kpis.push_back({ui::Icon::Library, "CONNAISSANCES", std::to_string(reg.count(false)), {},
                        std::to_string(here) + " actives ici" + kDot + std::to_string(addedCount) + (addedCount > 1 ? " ajout\xC3\xA9" "es" : " ajout\xC3\xA9" "e"),
                        mem::sizeText(reg.bytesHere(d_.spec_.project)) + " par demande"});
        const auto known = reg.kindsKnown(), total = mem::Registry::kindsTotal();
        kpis.push_back({ui::Icon::Layers, "GENRES D'OBJETS CONNUS", std::to_string(known) + "/" + std::to_string(total), {},
                        known ? std::string("tir\xC3\xA9s des tables du logiciel") : std::string("connaissance d\xC3\xA9sactiv\xC3\xA9" "e"),
                        "registre des connaissances", total ? static_cast<double>(known) / static_cast<double>(total) : 0.0,
                        ctx.theme.coverage(total ? static_cast<float>(known) / static_cast<float>(total) : 0.f)});
        {
            const auto [num, unit] = splitSize(mem::sizeText(m.total.bytes));
            kpis.push_back({ui::Icon::Folder, "STOCKAGE IA TOTAL", num, unit, plural(m.total.files, "fichier dans IA/", "fichiers dans IA/"),
                            "dont cache " + mem::sizeText(m.cache().bytes)});
        }
        {
            Kpi k{ui::Icon::Save, "ESPACE DISQUE LIBRE", "?", {}, "disque non lu", {}};
            if (m.disk.known) {
                const auto [num, unit] = splitSize(mem::sizeText(m.disk.available));
                k.big = num;
                k.unit = unit;
                k.line1 = "sur " + mem::sizeText(m.disk.capacity);
                k.line2 = percent(static_cast<double>(m.disk.available), static_cast<double>(m.disk.capacity)) + " du disque de IA/";
                k.meter = m.disk.capacity ? 1.0 - static_cast<double>(m.disk.available) / static_cast<double>(m.disk.capacity) : 0.0;
                k.meterColor = m.disk.capacity && m.disk.available * 10 < m.disk.capacity ? ctx.theme.onSurface(c.warning) : c.accent;
            }
            kpis.push_back(std::move(k));
        }
        for (std::size_t i = 0; i < kpis.size(); ++i) {
            const auto& k = kpis[i];
            const gfx::Rect cr{x0 + static_cast<float>(i) * (cw + kGap), y, cw, ch};
            card(ctx, cr, c.border);
            caption(ctx, cr.x + 12.f, cr.y + 11.f, k.icon, fit(ctx, k.title, kCaption, cr.w - 44.f), c.textMuted);
            const std::string big = fit(ctx, k.big, kBig, cr.w - 50.f);
            drawBold(ctx, {cr.x + 12.f, cr.y + 28.f}, big, kBig, c.text);
            if (!k.unit.empty())
                ctx.r.drawText({cr.x + 18.f + textW(ctx, big, kBig), cr.y + 28.f + ctx.r.lineHeight(kBig) - ctx.r.lineHeight(kSmall) - 3.f}, k.unit,
                               kSmall, c.textMuted);
            drawFit(ctx, {cr.x + 12.f, cr.y + 60.f}, k.line1, kSmall, cr.w - 24.f, c.textMuted);
            drawFit(ctx, {cr.x + 12.f, cr.y + 76.f}, k.line2, kSmall, cr.w - 24.f, c.textMuted);
            if (k.meter >= 0.0) meter(ctx, {cr.x + 12.f, cr.bottom() - 8.f, cr.w - 24.f, 4.f}, k.meter, k.meterColor);
        }
        y += ch + 12.f;
        // ---- deux blocs : la memoire vive, le stockage permanent
        const float bw = (W - kGap) * 0.5f;
        const float room = r.bottom() - 12.f - y;
        if (room < 120.f) return;
        // les deux blocs a leur hauteur ; la place qui reste : l'activite recente et ce qui part avec une demande
        const float bh = room >= 312.f + kGap + 110.f ? 312.f : room;
        paintRam(ctx, {x0, y, bw, bh});
        paintDisk(ctx, {x0 + bw + kGap, y, bw, bh});
        if (bh < room) {
            const float ry = y + bh + kGap, rh = r.bottom() - 12.f - ry;
            paintRecent(ctx, {x0, ry, bw, rh});
            paintSent(ctx, {x0 + bw + kGap, ry, bw, rh});
        }
    }

private:
    // Une jauge avec son libelle a gauche et sa valeur a droite ; rend la hauteur prise.
    float gauge(const ui::PaintContext& ctx, float x, float y, float w, const std::string& label, const std::string& value, double fraction,
                gfx::Color fill) {
        const auto& c = ctx.theme.color;
        const float vw = textW(ctx, value, kSmall);
        drawFit(ctx, {x, y}, label, kSmall, w - vw - 12.f, c.text);
        ctx.r.drawText({x + w - vw, y}, value, kSmall, c.textMuted);
        meter(ctx, {x, y + 20.f, w, 8.f}, fraction, fill);
        return 40.f;
    }

    void paintRam(const ui::PaintContext& ctx, const gfx::Rect& b) {
        const auto& c = ctx.theme.color;
        const auto& ram = d_.ram_;
        card(ctx, b, c.border);
        caption(ctx, b.x + kPad, b.y + 12.f, ui::Icon::Cpu, "M\xC3\x89MOIRE VIVE (RAM) DE L'APPLICATION", c.textMuted);
        const float x = b.x + kPad, w = b.w - 2.f * kPad;
        float y = b.y + 38.f;
        const std::string app = appName();
        const double whole = std::max(1.0, ram.systemKnown && ram.total ? static_cast<double>(ram.total) : static_cast<double>(std::max(ram.peak, ram.process)));
        const auto of = [&](std::uint64_t v) {
            return mem::sizeText(v) + (ram.systemKnown && ram.total ? " sur " + mem::sizeText(ram.total) + " (" + percent(static_cast<double>(v), whole) + ")" : std::string{});
        };
        if (ram.processKnown) {
            y += gauge(ctx, x, y, w, "Utilis\xC3\xA9" "e par " + app + " (maintenant)", of(ram.process), static_cast<double>(ram.process) / whole, c.accent);
            y += gauge(ctx, x, y, w, "Pic de consommation (depuis le d\xC3\xA9marrage)", of(ram.peak), static_cast<double>(ram.peak) / whole, c.info);
        } else {
            ctx.r.drawText({x, y}, "La m\xC3\xA9moire du processus n'est pas accessible sur ce syst\xC3\xA8me.", kSmall, c.textMuted);
            y += 24.f;
        }
        if (ram.systemKnown && ram.total)
            y += gauge(ctx, x, y, w, "RAM disponible du PC", mem::sizeText(ram.available) + " sur " + mem::sizeText(ram.total),
                       static_cast<double>(ram.available) / static_cast<double>(ram.total), ctx.theme.onSurface(c.ok));
        else {
            ctx.r.drawText({x, y}, "RAM disponible du PC : non accessible.", kSmall, c.textMuted);
            y += 24.f;
        }
        // les traitements IA locaux : aucun, dit tel quel
        if (y + 22.f < b.bottom()) {
            const std::string head = "Traitements IA locaux : ";
            ctx.r.drawText({x, y}, head, kSmall, c.text);
            ctx.r.drawText({x + textW(ctx, head, kSmall), y}, "aucun (0 o) \xE2\x80\x94 aucun mod\xC3\xA8le ne tourne sur ce PC.", kSmall, c.textMuted);
            y += 26.f;
        }
        // le modele distant : rien sur ce PC
        const float noteH = 58.f;
        if (y + noteH + 8.f < b.bottom()) {
            const gfx::Rect n{x, y, w, noteH};
            ctx.r.fillRoundedRect(n, c.info.withAlpha(ctx.theme.isDark() ? 36 : 24), 6.f);
            ctx.r.fillRoundedRect({n.x, n.y, 3.f, n.h}, c.info, 1.5f);
            ui::drawIcon(ctx.r, ui::Icon::Info, {n.x + 12.f, n.y + 10.f, 14.f, 14.f}, c.info);
            const auto lines = wrap(ctx,
                                    "Le mod\xC3\xA8le d'IA distant tourne chez son fournisseur : il n'occupe ni la m\xC3\xA9moire vive ni le disque de ce PC. "
                                    "Ces chiffres sont ceux du logiciel lui-m\xC3\xAAme (" + app + ").",
                                    kSmall, n.w - 46.f, 3);
            float ly = n.y + 9.f;
            for (const auto& l : lines) {
                ctx.r.drawText({n.x + 34.f, ly}, l, kSmall, c.text);
                ly += 16.f;
            }
            y += noteH + 8.f;
        }
        // 1.14.2 (captures) : d'ou viennent les chiffres, en mots de l'utilisateur ; le nom des fonctions du
        // systeme (le detail technique) dans l'infobulle seulement.
        if (y + 16.f < b.bottom() && !ram.by.empty()) {
            const std::string by = "Mesur\xC3\xA9 par " + ram.by + " pour ce logiciel et ce PC" + kDot + "actualis\xC3\xA9 chaque seconde";
            const float fy = b.bottom() - 22.f;
            ctx.r.drawText({x, fy}, fit(ctx, by, kCaption, w), kCaption, c.textMuted);
            addTip({x, fy - 2.f, std::min(w, textW(ctx, by, kCaption)), ctx.r.lineHeight(kCaption) + 4.f},
                   "Mesur\xC3\xA9 par " + ram.by + " pour ce logiciel et ce PC, chaque seconde.\nD\xC3\xA9tail technique : " + ram.source + ".");
        }
    }

    // Les derniers evenements du journal (activite.jsonl).
    void paintRecent(const ui::PaintContext& ctx, const gfx::Rect& b) {
        const auto& c = ctx.theme.color;
        card(ctx, b, c.border);
        caption(ctx, b.x + kPad, b.y + 12.f, ui::Icon::History, "ACTIVIT\xC3\x89 R\xC3\x89" "CENTE", c.textMuted);
        const float x = b.x + kPad, w = b.w - 2.f * kPad;
        float y = b.y + 36.f;
        if (d_.activity_.empty()) {
            ctx.r.drawText({x, y}, "Rien encore : chaque op\xC3\xA9ration de l'Assistant et de cette fen\xC3\xAAtre s'inscrit ici.", kSmall, c.textMuted);
            return;
        }
        for (auto it = d_.activity_.rbegin(); it != d_.activity_.rend() && y + 18.f < b.bottom() - 6.f; ++it) {
            const std::string when = mem::displayDate(it->date) + "  " + mem::activityTypeLabel(it->type);
            ctx.r.drawText({x, y}, when, kSmall, c.textMuted);
            const float tx = x + 205.f;
            drawFit(ctx, {tx, y}, it->text, kSmall, x + w - tx, c.text);
            y += 20.f;
        }
    }

    // Ce qui partirait avec une demande faite ici : les connaissances actives pour le projet ouvert.
    void paintSent(const ui::PaintContext& ctx, const gfx::Rect& b) {
        const auto& c = ctx.theme.color;
        card(ctx, b, c.border);
        const std::string where = d_.spec_.project.empty() ? std::string("SANS PROJET OUVERT") : "DANS LE PROJET " + d_.spec_.project;
        caption(ctx, b.x + kPad, b.y + 12.f, ui::Icon::Library, fit(ctx, "JOINT \xC3\x80 CHAQUE DEMANDE " + where, kCaption, b.w - 2.f * kPad - 120.f),
                c.textMuted);
        const float x = b.x + kPad, w = b.w - 2.f * kPad;
        textRight(ctx, x + w, b.y + 11.f, mem::sizeText(d_.registry_.bytesHere(d_.spec_.project)) + " en tout", kSmall, c.text);
        float y = b.y + 36.f;
        std::size_t shown = 0, total = 0;
        for (const auto& k : d_.registry_.all()) {
            if (mem::hereState(k, d_.spec_.project) != mem::Here::Active) continue;
            ++total;
            if (y + 18.f >= b.bottom() - 24.f) continue;
            drawFit(ctx, {x, y}, k.name, kSmall, w * 0.5f, c.text);
            drawFit(ctx, {x + w * 0.52f, y}, mem::categoryLabel(k.category), kSmall, w * 0.3f, c.textMuted);
            textRight(ctx, x + w, y, mem::sizeText(k.size()), kSmall, c.textMuted);
            y += 20.f;
            ++shown;
        }
        if (shown < total) textRight(ctx, x + w, b.bottom() - 20.f, "et " + std::to_string(total - shown) + " autre(s) (onglet Apprentissage)", kCaption, c.textMuted);
    }

    void paintDisk(const ui::PaintContext& ctx, const gfx::Rect& b) {
        const auto& c = ctx.theme.color;
        const auto& m = d_.measure_;
        card(ctx, b, c.border);
        caption(ctx, b.x + kPad, b.y + 12.f, ui::Icon::Save, "STOCKAGE PERMANENT (DISQUE)", c.textMuted);
        const float x = b.x + kPad, w = b.w - 2.f * kPad;
        float y = b.y + 38.f;
        // le disque : utilise par le reste, par l'IA, libre
        if (m.disk.known && m.disk.capacity) {
            const std::string value = mem::sizeText(m.disk.available) + " libres sur " + mem::sizeText(m.disk.capacity);
            const float vw = textW(ctx, value, kSmall);
            drawFit(ctx, {x, y}, "Disque qui porte IA/", kSmall, w - vw - 12.f, c.text);
            ctx.r.drawText({x + w - vw, y}, value, kSmall, c.textMuted);
            const double cap = static_cast<double>(m.disk.capacity);
            const double used = cap - static_cast<double>(m.disk.free);
            const double ia = static_cast<double>(m.total.bytes);
            stackedBar(ctx, {x, y + 20.f, w, 8.f}, {{std::max(0.0, used - ia), c.textDisabled}, {ia, c.accent}}, cap);
            y += 34.f;
            ctx.r.drawText({x, y}, fit(ctx, "Les donn\xC3\xA9" "es de l'IA en occupent " + percent(ia, cap) + " (" + mem::sizeText(m.total.bytes) + ")", kSmall, w), kSmall,
                           c.textMuted);
            y += 26.f;
        } else {
            ctx.r.drawText({x, y}, "L'espace du disque n'a pas pu \xC3\xAAtre lu.", kSmall, c.textMuted);
            y += 26.f;
        }
        // la repartition des donnees de l'IA
        const auto groups = groupsOf(m);
        ctx.r.drawText({x, y}, "R\xC3\xA9partition des donn\xC3\xA9" "es de l'IA", kSmall, c.text);
        textRight(ctx, x + w, y, mem::sizeText(m.total.bytes), kSmall, c.textMuted);
        std::vector<std::pair<double, gfx::Color>> parts;
        for (std::size_t i = 0; i < groups.size(); ++i) parts.push_back({static_cast<double>(groups[i].bytes), groupColor(ctx, i)});
        stackedBar(ctx, {x, y + 20.f, w, 12.f}, parts, static_cast<double>(m.total.bytes));
        y += 42.f;
        // la legende, avec les tailles demandees (totale, cache, pieces jointes, index et donnees techniques)
        const float rowH = 20.f;
        for (std::size_t i = 0; i < groups.size() && y + rowH < b.bottom() - 6.f; ++i) {
            ctx.r.fillRoundedRect({x, y + 3.f, 10.f, 10.f}, groupColor(ctx, i), 2.f);
            ctx.r.drawText({x + 18.f, y}, groups[i].label, kSmall, c.text);
            const std::string v = mem::sizeText(groups[i].bytes) + kDot + plural(groups[i].files, "fichier", "fichiers") + kDot
                                  + percent(static_cast<double>(groups[i].bytes), static_cast<double>(m.total.bytes));
            textRight(ctx, x + w, y, v, kSmall, c.textMuted);
            y += rowH;
        }
        y += 6.f;
        if (y + 20.f < b.bottom()) {
            ctx.r.line({x, y}, {x + w, y}, c.border, 1.f);
            y += 8.f;
            drawBold(ctx, {x, y}, "Taille totale des donn\xC3\xA9" "es IA", kSmall, c.text);
            textRight(ctx, x + w, y, mem::sizeText(m.total.bytes) + kDot + plural(m.total.files, "fichier", "fichiers"), kSmall, c.text);
            y += rowH;
        }
        if (y + 20.f < b.bottom())
            ctx.r.drawText({x, y}, fit(ctx, std::string("Dernier nettoyage du cache : ") + (m.lastCleaning.empty() ? std::string("jamais") : mem::displayDate(m.lastCleaning)), kSmall, w),
                           kSmall, c.textMuted);
    }

    AiMemoryDialog&  d_;
    std::vector<Tip> tips_;
};

// ====================================================================== Apprentissage ===
class AiMemoryDialog::LearningPage final : public ui::Widget {
public:
    explicit LearningPage(AiMemoryDialog& d) : ui::Widget("ia.memoire.apprentissage"), d_(d) {
        const std::string base = "ia.memoire.apprentissage.";
        catBox = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "categorie")));
        projBox = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "projet")));
        add = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Ajouter une connaissance\xE2\x80\xA6", base + "ajouter")));
        add->setIcon(ui::Icon::Plus);
        model = std::make_shared<Rows>();
        model->headers = {"Nom", "Cat\xC3\xA9gorie", "Source", "Projet", "Ajout\xC3\xA9" "e", "Mise \xC3\xA0 jour", "Taille estim\xC3\xA9" "e", "\xC3\x89tat"};
        table = &makeTable(*this, base + "liste", model,
                           {col("Nom", 250.f), col("Cat\xC3\xA9gorie", 210.f), col("Source", 135.f), col("Projet", 150.f), col("Ajout\xC3\xA9" "e", 105.f),
                            col("Mise \xC3\xA0 jour", 110.f), col("Taille estim\xC3\xA9" "e", 125.f, ui::Align::End), col("\xC3\x89tat", 100.f)});
        view = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Consulter", base + "consulter")));
        edit = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Modifier", base + "modifier")));
        toggle = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("D\xC3\xA9sactiver", base + "basculer")));
        remove = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Supprimer\xE2\x80\xA6", base + "supprimer")));
        remove->setStyle(ui::Button::Style::Danger);
        remove->setIcon(ui::Icon::Delete);
        // l'editeur (ajouter, modifier, consulter) : par-dessus la liste
        edName = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + "nom")));
        edName->setPlaceholder("Le nom de la connaissance");
        edCat = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "edcategorie")));
        edProject = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + "edprojet")));
        edProject->setPlaceholder("vide : tous les projets");
        edDesc = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + "description")));
        edDesc->setPlaceholder("Ce qu'elle apporte, en une ligne");
        edContent = &static_cast<ui::MultiLineText&>(addChild(std::make_unique<ui::MultiLineText>(base + "contenu")));
        edContent->setWordWrap(true);
        edContent->setShowLineNumbers(false);
        edContent->setLanguage(ui::Language::PlainText);
        edActive = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>("Active (jointe aux demandes)", base + "active")));
        edSave = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Enregistrer", base + "enregistrer")));
        edSave->setStyle(ui::Button::Style::Primary);
        edCancel = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", base + "annuler")));
        edToEdit = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Modifier", base + "versmodifier")));
        std::vector<ui::DropDown::Item> cats;
        for (const auto& c : mem::kCategories) cats.push_back({std::string(c.label), std::string(c.key), {}, true});
        edCat->setItems(std::move(cats));
        edCat->setSelectedIndex(static_cast<int>(std::size(mem::kCategories)) - 1);
        showEditor(false);
    }

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override { return tipAt(tips_, mouse); }
    [[nodiscard]] bool hasTooltip() const override { return !tips_.empty() || ui::Widget::hasTooltip(); }

    void showEditor(bool on) {
        editorOn = on;
        const auto v = [](bool b) { return b ? ui::Visibility::Visible : ui::Visibility::Collapsed; };
        for (ui::Widget* w : {static_cast<ui::Widget*>(edName), static_cast<ui::Widget*>(edCat), static_cast<ui::Widget*>(edProject),
                              static_cast<ui::Widget*>(edDesc), static_cast<ui::Widget*>(edContent), static_cast<ui::Widget*>(edActive),
                              static_cast<ui::Widget*>(edSave), static_cast<ui::Widget*>(edCancel)})
            w->setVisibility(v(on));
        edToEdit->setVisibility(v(on && d_.editorReadOnly_ && d_.selectedKnowledge() && !d_.selectedKnowledge()->builtIn));
        if (on && d_.editorReadOnly_) edSave->setVisibility(ui::Visibility::Collapsed);
        for (ui::Widget* w : {static_cast<ui::Widget*>(table), static_cast<ui::Widget*>(view), static_cast<ui::Widget*>(edit),
                              static_cast<ui::Widget*>(toggle), static_cast<ui::Widget*>(remove)})
            w->setVisibility(v(!on));
        edCancel->setText(on && d_.editorReadOnly_ ? "Retour \xC3\xA0 la liste" : "Annuler");
        invalidateLayout();
        invalidate();
    }

    ui::DropDown*      catBox{nullptr};
    ui::DropDown*      projBox{nullptr};
    ui::Button*        add{nullptr};
    std::shared_ptr<Rows> model;
    ui::TableView*     table{nullptr};
    ui::Button*        view{nullptr};
    ui::Button*        edit{nullptr};
    ui::Button*        toggle{nullptr};
    ui::Button*        remove{nullptr};
    ui::InputText*     edName{nullptr};
    ui::DropDown*      edCat{nullptr};
    ui::InputText*     edProject{nullptr};
    ui::InputText*     edDesc{nullptr};
    ui::MultiLineText* edContent{nullptr};
    ui::Checkbox*      edActive{nullptr};
    ui::Button*        edSave{nullptr};
    ui::Button*        edCancel{nullptr};
    ui::Button*        edToEdit{nullptr};
    bool               editorOn{false};

protected:
    void onLayout() override {
        const auto r = bounds();
        const float x0 = r.x + kPad, W = r.w - 2.f * kPad;
        info_ = {x0, r.y + 10.f, W, 34.f};
        const float fy = info_.bottom() + 8.f;
        catLabelX_ = x0;
        const float catLw = ui::measureWidth("Cat\xC3\xA9gorie", kSmall) + 8.f;
        catBox->setBounds({x0 + catLw, fy, 230.f, 30.f});
        projLabelX_ = x0 + catLw + 230.f + 18.f;
        const float projLw = ui::measureWidth("Projet", kSmall) + 8.f;
        projBox->setBounds({projLabelX_ + projLw, fy, 220.f, 30.f});
        countX_ = projLabelX_ + projLw + 220.f + 16.f;
        filtersY_ = fy;
        const float aw = std::max(200.f, ui::measureWidth(add->text(), gfx::FontId{16}) + 56.f);
        add->setBounds({x0 + W - aw, fy, aw, 30.f});
        countW_ = x0 + W - aw - 12.f - countX_;
        const float top = fy + 40.f;
        const float avail = r.bottom() - 10.f - top;
        if (editorOn) {
            editor_ = {x0, top, W, avail};
            const float lw = std::min(380.f, W * 0.36f);
            const float lx = editor_.x + 12.f, fw = lw - 24.f;
            float y = editor_.y + 32.f;
            const auto field = [&](ui::Widget* w, float h) {
                w->setBounds({lx, y + 18.f, fw, h});
                y += 18.f + h + 12.f;
            };
            labelsY_.clear();
            labelsY_.push_back(y);
            field(edName, 30.f);
            labelsY_.push_back(y);
            field(edCat, 30.f);
            labelsY_.push_back(y);
            field(edProject, 30.f);
            labelsY_.push_back(y);
            field(edDesc, 30.f);
            edActive->setBounds({lx, y + 4.f, fw, 24.f});
            const float cx = editor_.x + lw + 8.f;
            contentLabel_ = {cx, editor_.y + 32.f};
            edContent->setBounds({cx, editor_.y + 50.f, editor_.right() - 12.f - cx, editor_.bottom() - 56.f - (editor_.y + 50.f)});
            float bx = editor_.right() - 12.f;
            for (ui::Button* b : {edSave, edCancel, edToEdit}) {
                if (!b->visible()) continue;
                const float bw = std::max(110.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 36.f);
                bx -= bw;
                b->setBounds({bx, editor_.bottom() - 44.f, bw, 32.f});
                bx -= 10.f;
            }
            return;
        }
        const float tableH = std::max(140.f, std::floor(avail * 0.48f));
        table->setBounds({x0, top, W, tableH});
        const float by = top + tableH + 10.f;
        const float bh = r.bottom() - 10.f - by;
        detail_ = {x0, by, std::floor(W * 0.6f), bh};
        recent_ = {detail_.right() + kGap, by, x0 + W - detail_.right() - kGap, bh};
        float bx = detail_.x + 12.f;
        const float bY = detail_.bottom() - 42.f;
        for (ui::Button* b : {view, edit, toggle}) {
            const float bw = std::max(100.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 32.f);
            b->setBounds({bx, bY, bw, 30.f});
            bx += bw + 8.f;
        }
        const float rw = std::max(120.f, ui::measureWidth(remove->text(), gfx::FontId{16}) + 56.f);
        remove->setBounds({detail_.right() - 12.f - rw, bY, rw, 30.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const TipScope tips(tips_);
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(bounds(), c.windowBg);
        // l'encart franc
        ctx.r.fillRoundedRect(info_, c.accent.withAlpha(ctx.theme.isDark() ? 34 : 22), 6.f);
        ctx.r.fillRoundedRect({info_.x, info_.y, 3.f, info_.h}, c.accent, 1.5f);
        ui::drawIcon(ctx.r, ui::Icon::Info, {info_.x + 12.f, info_.y + 10.f, 14.f, 14.f}, c.accent);
        const std::string lead = "Apprendre ici = enrichir la m\xC3\xA9moire locale que l'Assistant joint \xC3\xA0 ses demandes.";
        drawBold(ctx, {info_.x + 34.f, info_.y + (info_.h - ctx.r.lineHeight(kText)) * 0.5f}, fit(ctx, lead, kText, info_.w - 44.f), kText, c.text);
        const float lx = info_.x + 34.f + textW(ctx, lead, kText) + 8.f;
        if (lx < info_.right() - 40.f)
            textMid(ctx, lx, info_, fit(ctx, "Le mod\xC3\xA8le distant n'est pas r\xC3\xA9" "entra\xC3\xAEn\xC3\xA9.", kText, info_.right() - 10.f - lx), kText, c.text);
        // les filtres
        const gfx::Rect fb{0.f, filtersY_, 0.f, 30.f};
        textMid(ctx, catLabelX_, fb, "Cat\xC3\xA9gorie", kSmall, c.textMuted);
        textMid(ctx, projLabelX_, fb, "Projet", kSmall, c.textMuted);
        {
            std::size_t shown = d_.shownKnowledge().size(), here = 0;
            for (const auto* k : d_.shownKnowledge())
                if (mem::hereState(*k, d_.spec_.project) == mem::Here::Active) ++here;
            drawFit(ctx, {countX_, fb.y + (fb.h - ctx.r.lineHeight(kSmall)) * 0.5f},
                    plural(shown, "connaissance", "connaissances") + kDot + std::to_string(here) + " active(s) ici", kSmall, countW_, c.textMuted);
        }
        if (editorOn) {
            paintEditor(ctx);
            return;
        }
        paintDetail(ctx);
        paintRecent(ctx);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            if (k->key == ui::Key::Delete && !editorOn && table->focused()) {
                (void)d_.deleteSelected();
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    void paintEditor(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        card(ctx, editor_, c.border);
        const std::string title = d_.editorReadOnly_ ? "CONSULTER LA CONNAISSANCE" : d_.editorAdding_ ? "AJOUTER UNE CONNAISSANCE" : "MODIFIER LA CONNAISSANCE";
        caption(ctx, editor_.x + 12.f, editor_.y + 11.f, d_.editorReadOnly_ ? ui::Icon::Eye : ui::Icon::Rename, title, c.textMuted);
        const char* labels[] = {"Nom", "Cat\xC3\xA9gorie", "Projet", "Description"};
        for (std::size_t i = 0; i < labelsY_.size() && i < 4; ++i) ctx.r.drawText({editor_.x + 12.f, labelsY_[i]}, labels[i], kSmall, c.textMuted);
        std::string contentHead = "Contenu \xE2\x80\x94 le texte joint aux demandes";
        if (const auto* k = d_.selectedKnowledge(); k && !d_.editorAdding_)
            contentHead += kDot + mem::sizeText(k->size()) + (k->builtIn ? kDot + "tir\xC3\xA9 des tables du logiciel, en lecture seule" : std::string{});
        drawFit(ctx, contentLabel_, contentHead, kSmall, editor_.right() - 12.f - contentLabel_.x, c.textMuted);
    }

    void paintDetail(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        card(ctx, detail_, c.border);
        const auto* k = d_.selectedKnowledge();
        const float x = detail_.x + 12.f, w = detail_.w - 24.f;
        float y = detail_.y + 12.f;
        if (!k) {
            ctx.r.drawText({x, y},
                           d_.shownKnowledge().empty() ? "Aucune connaissance avec ces filtres : change la cat\xC3\xA9gorie ou le projet."
                                                       : "Choisis une connaissance dans la liste.",
                           kText, c.textMuted);
            return;
        }
        ui::drawIcon(ctx.r, k->builtIn ? ui::Icon::Library : ui::Icon::Document, {x, y + 1.f, 16.f, 16.f}, c.textMuted);
        {
            const std::string name = fit(ctx, k->name, kText, w - 24.f);
            drawBold(ctx, {x + 24.f, y}, name, kText, c.text);
            if (name != k->name) addTip({x + 24.f, y - 2.f, w - 24.f, ctx.r.lineHeight(kText) + 4.f}, k->name);
        }
        y += 24.f;
        // les puces : categorie, source, projet, etat
        float px = x;
        const auto chip = [&](const std::string& s, gfx::Color ink) {
            const float cw = textW(ctx, s, kSmall) + 16.f;
            if (px + cw > x + w) return;
            const gfx::Rect cr{px, y, cw, 22.f};
            ctx.r.fillRoundedRect(cr, ink.withAlpha(ctx.theme.isDark() ? 40 : 28), 11.f);
            ctx.r.drawText({cr.x + 8.f, cr.y + (cr.h - ctx.r.lineHeight(kSmall)) * 0.5f}, s, kSmall, c.text);
            px += cw + 6.f;
        };
        const auto here = mem::hereState(*k, d_.spec_.project);
        chip(mem::categoryLabel(k->category), ctx.theme.onSurface(ctx.theme.brand.family[2]));
        chip("Source : " + mem::sourceLabel(k->source), ctx.theme.onSurface(ctx.theme.brand.family[0]));
        chip(k->project.empty() ? std::string("Tous les projets") : "Projet " + k->project, ctx.theme.onSurface(ctx.theme.brand.family[4]));
        chip(mem::hereLabel(here), here == mem::Here::Active ? c.ok : here == mem::Here::Inactive ? c.textDisabled : c.warning);
        y += 32.f;
        {
            const std::string desc = k->description.empty() ? std::string("(sans description)") : k->description;
            const auto lines = wrap(ctx, desc, kSmall, w, 2);
            const float y0 = y;
            for (const auto& l : lines) {
                ctx.r.drawText({x, y}, l, kSmall, c.text);
                y += 17.f;
            }
            // coupee a deux lignes : en entier au survol
            if (!lines.empty() && lines.back().size() >= 3 && lines.back().compare(lines.back().size() - 3, 3, kDots) == 0)
                addTip({x, y0 - 2.f, w, y - y0 + 2.f}, desc);
        }
        y += 4.f;
        const std::string dates = k->builtIn ? "Int\xC3\xA9gr\xC3\xA9" "e au logiciel (" + appName() + ")" + kDot + "taille estim\xC3\xA9" "e " + mem::sizeText(k->size())
                                             : "Ajout\xC3\xA9" "e le " + dateOrDash(k->added) + kDot + "mise \xC3\xA0 jour le " + dateOrDash(k->modified) + kDot + "taille estim\xC3\xA9" "e "
                                                   + mem::sizeText(k->size());
        drawFit(ctx, {x, y}, dates, kSmall, w, c.textMuted);
        y += 22.f;
        if (here == mem::Here::OtherProject) {
            drawFit(ctx, {x, y},
                    "Propre au projet " + k->project + " : elle ne part pas avec les demandes faites dans "
                        + (d_.spec_.project.empty() ? std::string("un autre projet") : d_.spec_.project) + ".",
                    kSmall, w, ctx.theme.onSurface(c.warning));
            y += 20.f;
        }
        // un apercu du contenu
        const float bottom = detail_.bottom() - 52.f;
        if (y + 18.f < bottom) {
            const gfx::Rect box{x, y, w, bottom - y};
            ctx.r.fillRoundedRect(box, c.inputBg, 4.f);
            ctx.r.pushClip(box);
            float ly = box.y + 6.f;
            std::size_t from = 0;
            while (from < k->content.size() && ly + 15.f < box.bottom()) {
                auto nl = k->content.find('\n', from);
                if (nl == std::string::npos) nl = k->content.size();
                ctx.r.drawText({box.x + 8.f, ly}, fit(ctx, k->content.substr(from, nl - from), kSmall, box.w - 16.f), kSmall, c.textMuted);
                ly += 16.f;
                from = nl + 1;
            }
            ctx.r.popClip();
        }
    }

    void paintRecent(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        card(ctx, recent_, c.border);
        caption(ctx, recent_.x + 12.f, recent_.y + 11.f, ui::Icon::History, "ACTIVIT\xC3\x89 R\xC3\x89" "CENTE DE L'APPRENTISSAGE", c.textMuted);
        float y = recent_.y + 34.f;
        const float x = recent_.x + 12.f, w = recent_.w - 24.f;
        std::size_t n = 0;
        for (auto it = d_.activity_.rbegin(); it != d_.activity_.rend() && y + 36.f < recent_.bottom(); ++it) {
            if (it->type != "connaissance" && it->type != "regle" && it->type != "fichier") continue;
            ctx.r.drawText({x, y}, mem::displayDate(it->date) + kDot + mem::activityTypeLabel(it->type), kCaption, c.textMuted);
            drawFit(ctx, {x, y + 14.f}, it->text, kSmall, w, c.text);
            y += 38.f;
            ++n;
        }
        if (!n)
            for (const auto& l : wrap(ctx, "Rien encore. Une connaissance ajout\xC3\xA9" "e, actualis\xC3\xA9" "e, un fichier analys\xC3\xA9, une r\xC3\xA8gle enregistr\xC3\xA9" "e : chacun s'inscrit ici.",
                                     kSmall, w, 4)) {
                ctx.r.drawText({x, y}, l, kSmall, c.textMuted);
                y += 17.f;
            }
    }

    AiMemoryDialog& d_;
    std::vector<Tip> tips_;
    gfx::Rect info_{}, detail_{}, recent_{}, editor_{};
    gfx::Point contentLabel_{};
    std::vector<float> labelsY_;
    float catLabelX_{0}, projLabelX_{0}, countX_{0}, countW_{0}, filtersY_{0};
};

// ============================================================================= Memoire ===
class AiMemoryDialog::MemoryPage final : public ui::Widget {
public:
    explicit MemoryPage(AiMemoryDialog& d) : ui::Widget("ia.memoire.memoire"), d_(d) {
        const std::string base = "ia.memoire.memoire.";
        search = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + "recherche")));
        search->setPlaceholder("Rechercher : titres, messages, connaissances, pi\xC3\xA8" "ces jointes");
        model = std::make_shared<Rows>();
        table = &makeTable(*this, base + "liste", model, {});
        removeOne = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Supprimer\xE2\x80\xA6", base + "supprimer")));
        removeOne->setStyle(ui::Button::Style::Danger);
        removeOne->setIcon(ui::Icon::Delete);
        removeAll = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Supprimer toute la cat\xC3\xA9gorie\xE2\x80\xA6", base + "toutsupprimer")));
        removeAll->setStyle(ui::Button::Style::Danger);
    }

    ui::InputText*        search{nullptr};
    std::shared_ptr<Rows> model;
    ui::TableView*        table{nullptr};
    ui::Button*           removeOne{nullptr};
    ui::Button*           removeAll{nullptr};

    // La liste des categories a gauche : ce qu'elle montre (le titre, le compte, la taille).
    struct Entry {
        MemCat      cat;
        std::string title, detail;
        gfx::Rect   rect{};
    };
    std::vector<Entry> entries;

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override { return tipAt(tips_, mouse); }
    [[nodiscard]] bool hasTooltip() const override { return !tips_.empty() || ui::Widget::hasTooltip(); }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float x0 = r.x + kPad, W = r.w - 2.f * kPad;
        search->setBounds({x0, r.y + 10.f, std::min(520.f, W * 0.5f), 30.f});
        countX_ = x0 + std::min(520.f, W * 0.5f) + 14.f;
        countY_ = r.y + 10.f;
        list_ = {x0, r.y + 50.f, 260.f, r.bottom() - 10.f - (r.y + 50.f)};
        const float tx = list_.right() + kGap;
        const gfx::Rect right{tx, list_.y, x0 + W - tx, list_.h};
        table->setBounds({right.x, right.y, right.w, right.h - 48.f});
        const float by = right.bottom() - 36.f;
        const float w1 = std::max(150.f, ui::measureWidth(removeOne->text(), gfx::FontId{16}) + 56.f);
        removeOne->setBounds({right.x, by, w1, 30.f});
        const float w2 = std::max(150.f, ui::measureWidth(removeAll->text(), gfx::FontId{16}) + 36.f);
        removeAll->setBounds({right.x + w1 + 10.f, by, w2, 30.f});
        hintX_ = right.x + w1 + w2 + 26.f;
        hintW_ = right.right() - hintX_;
        hintY_ = by;
        float y = list_.y + 6.f;
        for (auto& e : entries) {
            e.rect = {list_.x + 4.f, y, list_.w - 8.f, 46.f};
            y += 48.f;
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const TipScope tips(tips_);
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(bounds(), c.windowBg);
        // le compte de la recherche
        if (!d_.search_.empty())
            ctx.r.drawText({countX_, countY_ + (30.f - ctx.r.lineHeight(kSmall)) * 0.5f},
                           plural(d_.hits_.size(), "r\xC3\xA9sultat", "r\xC3\xA9sultats") + " (titres, messages, connaissances, pi\xC3\xA8" "ces jointes)", kSmall,
                           c.textMuted);
        else
            ctx.r.drawText({countX_, countY_ + (30.f - ctx.r.lineHeight(kSmall)) * 0.5f}, "Les informations que l'Assistant garde d'une s\xC3\xA9" "ance \xC3\xA0 l'autre.",
                           kSmall, c.textMuted);
        // les categories
        card(ctx, list_, c.border);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            const bool on = e.cat == d_.memCat_;
            const bool hot = static_cast<int>(i) == hover_;
            if (on) ctx.r.fillRoundedRect(e.rect, c.selectionBg, 4.f);
            else if (hot) ctx.r.fillRoundedRect(e.rect, ctx.theme.brand.hover, 4.f);
            const gfx::Color ink = on ? c.selectionText : c.text;
            const gfx::Color muted = on ? c.selectionText : c.textMuted;
            drawFit(ctx, {e.rect.x + 10.f, e.rect.y + 6.f}, e.title, kText, e.rect.w - 20.f, ink);
            drawFit(ctx, {e.rect.x + 10.f, e.rect.y + 26.f}, e.detail, kSmall, e.rect.w - 20.f, muted);
        }
        // l'aide des boutons
        drawFit(ctx, {hintX_, hintY_ + (30.f - ctx.r.lineHeight(kSmall)) * 0.5f}, hint_, kSmall, hintW_, c.textMuted);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            int h = -1;
            for (std::size_t i = 0; i < entries.size(); ++i)
                if (entries[i].rect.contains(m->pos)) h = static_cast<int>(i);
            if (h != hover_) {
                hover_ = h;
                invalidate();
            }
        }
        if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && m->button == ui::MouseButton::Left) {
            for (const auto& e : entries)
                if (e.rect.contains(m->pos)) {
                    d_.showMemoryCategory(e.cat);
                    return ui::EventResult::Consumed;
                }
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            if (k->key == ui::Key::Delete && table->focused()) {
                (void)d_.deleteMemorySelection();
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

public:
    std::string hint_;

private:
    AiMemoryDialog& d_;
    std::vector<Tip> tips_;
    gfx::Rect list_{};
    float countX_{0}, countY_{0}, hintX_{0}, hintW_{0}, hintY_{0};
    int hover_{-1};
};

// ========================================================================= Stockage PC ===
class AiMemoryDialog::StoragePage final : public ui::Widget {
public:
    explicit StoragePage(AiMemoryDialog& d) : ui::Widget("ia.memoire.stockage"), d_(d) {
        const std::string base = "ia.memoire.stockage.";
        model = std::make_shared<Rows>();
        model->headers = {"Cat\xC3\xA9gorie", "Emplacement (sous IA/)", "Fichiers", "Taille", "Part"};
        table = &makeTable(*this, base + "liste", model,
                           {col("Cat\xC3\xA9gorie", 300.f), col("Emplacement (sous IA/)", 230.f), col("Fichiers", 80.f, ui::Align::End),
                            col("Taille", 95.f, ui::Align::End), col("Part", 75.f, ui::Align::End)});
        where = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + "emplacement")));
        where->setReadOnly(true);
        copy = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Copier l'emplacement", base + "copier")));
        copy->setIcon(ui::Icon::Copy);
        open = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Ouvrir le dossier", base + "ouvrir")));
        open->setIcon(ui::Icon::FolderOpen);
    }

    std::shared_ptr<Rows> model;
    ui::TableView*        table{nullptr};
    ui::InputText*        where{nullptr};
    ui::Button*           copy{nullptr};
    ui::Button*           open{nullptr};

protected:
    void onLayout() override {
        const auto r = bounds();
        const float x0 = r.x + kPad, W = r.w - 2.f * kPad;
        const float bottomH = 92.f;
        const float top = r.y + 12.f;
        const float h = r.bottom() - 10.f - bottomH - top;
        const float lw = std::floor(W * 0.66f);
        table->setBounds({x0, top, lw, h - 30.f});
        summaryY_ = top + h - 22.f;
        summaryX_ = x0;
        ring_ = {x0 + lw + kGap, top, W - lw - kGap, h};
        place_ = {x0, r.bottom() - 10.f - bottomH + 6.f, W, bottomH - 6.f};
        const float ow = std::max(160.f, ui::measureWidth(open->text(), gfx::FontId{16}) + 56.f);
        const float cw = std::max(160.f, ui::measureWidth(copy->text(), gfx::FontId{16}) + 56.f);
        const float fy = place_.y + 30.f;
        open->setBounds({place_.right() - 12.f - ow, fy, ow, 30.f});
        copy->setBounds({place_.right() - 12.f - ow - 10.f - cw, fy, cw, 30.f});
        where->setBounds({place_.x + 12.f, fy, place_.right() - 12.f - ow - 10.f - cw - 10.f - (place_.x + 12.f), 30.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const auto& m = d_.measure_;
        ctx.r.fillRect(bounds(), c.windowBg);
        // sous le tableau : le total
        drawBold(ctx, {summaryX_, summaryY_},
                 fit(ctx, "Total : " + mem::sizeText(m.total.bytes) + " dans " + plural(m.total.files, "fichier", "fichiers")
                              + (m.exists ? std::string{} : std::string(kDot) + "le dossier n'existe pas encore"),
                     kText, table->bounds().w),
                 kText, c.text);
        // l'anneau de la repartition
        card(ctx, ring_, c.border);
        caption(ctx, ring_.x + 12.f, ring_.y + 11.f, ui::Icon::Chart, "R\xC3\x89PARTITION DES DONN\xC3\x89" "ES DE L'IA", c.textMuted);
        const auto groups = groupsOf(m);
        const float legendH = 20.f * static_cast<float>(groups.size()) + 70.f;
        const float avail = ring_.h - 40.f - legendH;
        const float rad = std::max(30.f, std::min({ring_.w * 0.5f - 30.f, avail * 0.5f - 6.f, 110.f}));
        const gfx::Point centre{ring_.x + ring_.w * 0.5f, ring_.y + 36.f + rad + 4.f};
        const float inner = rad * 0.62f;
        const double total = static_cast<double>(m.total.bytes);
        if (total <= 0.0) {
            ui::shapes::fillArcBand(ctx.r, centre, inner, rad, -90.f, 270.f, c.border);
        } else {
            float from = -90.f;
            for (std::size_t i = 0; i < groups.size(); ++i) {
                if (!groups[i].bytes) continue;
                const float sweep = std::max(1.5f, static_cast<float>(360.0 * static_cast<double>(groups[i].bytes) / total));
                ui::shapes::fillArcBand(ctx.r, centre, inner, rad, from, std::min(270.f, from + sweep), groupColor(ctx, i));
                from += sweep;
            }
        }
        const std::string mid = mem::sizeText(m.total.bytes);
        drawBold(ctx, {centre.x - textW(ctx, mid, kText) * 0.5f, centre.y - 12.f}, mid, kText, c.text);
        const std::string sub = plural(m.total.files, "fichier", "fichiers");
        ctx.r.drawText({centre.x - textW(ctx, sub, kSmall) * 0.5f, centre.y + 6.f}, sub, kSmall, c.textMuted);
        float y = centre.y + rad + 14.f;
        const float lx = ring_.x + 16.f, lw = ring_.w - 32.f;
        for (std::size_t i = 0; i < groups.size() && y + 18.f < ring_.bottom(); ++i) {
            ctx.r.fillRoundedRect({lx, y + 3.f, 10.f, 10.f}, groupColor(ctx, i), 2.f);
            ctx.r.drawText({lx + 18.f, y}, groups[i].label, kSmall, c.text);
            textRight(ctx, lx + lw, y, mem::sizeText(groups[i].bytes) + kDot + percent(static_cast<double>(groups[i].bytes), total), kSmall, c.textMuted);
            y += 20.f;
        }
        y += 8.f;
        if (m.disk.known && y + 34.f < ring_.bottom()) {
            const std::string v = mem::sizeText(m.disk.available) + " libres sur " + mem::sizeText(m.disk.capacity);
            ctx.r.drawText({lx, y}, "Espace disque disponible", kSmall, c.text);
            textRight(ctx, lx + lw, y, v, kSmall, c.textMuted);
            meter(ctx, {lx, y + 20.f, lw, 6.f}, m.disk.capacity ? 1.0 - static_cast<double>(m.disk.available) / static_cast<double>(m.disk.capacity) : 0.0,
                  c.accent);
        }
        // l'emplacement
        card(ctx, place_, c.border);
        caption(ctx, place_.x + 12.f, place_.y + 10.f, ui::Icon::Folder, "EMPLACEMENT DU DOSSIER DE STOCKAGE", c.textMuted);
        const std::string facts = plural(m.total.files, "fichier", "fichiers") + kDot + "dernier nettoyage : "
                                  + (m.lastCleaning.empty() ? std::string("jamais") : mem::displayDate(m.lastCleaning));
        textRight(ctx, place_.right() - 12.f, place_.y + 9.f, facts, kSmall, c.textMuted);
        if (!m.problems.empty())
            ctx.r.drawText({place_.x + 12.f, place_.bottom() - 18.f}, fit(ctx, m.problems.front(), kCaption, place_.w - 24.f), kCaption, c.error);
    }

private:
    AiMemoryDialog& d_;
    gfx::Rect ring_{}, place_{};
    float summaryX_{0}, summaryY_{0};
};

// =========================================================================== Nettoyage ===
class AiMemoryDialog::CleaningPage final : public ui::Widget {
public:
    explicit CleaningPage(AiMemoryDialog& d) : ui::Widget("ia.memoire.nettoyage"), d_(d) {
        const std::string base = "ia.memoire.nettoyage.";
        for (const auto& p : {mem::Part::CacheRequests, mem::Part::CacheProcessing, mem::Part::CacheImages, mem::Part::CacheDocuments,
                              mem::Part::CachePreviews, mem::Part::CacheDownloads, mem::Part::CacheOther}) {
            auto* box = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>(std::string(mem::partInfo(p).label),
                                                                                             base + std::string(mem::partInfo(p).key))));
            cache.push_back({p, box});
        }
        clear = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Vider le cache\xE2\x80\xA6", base + "vider")));
        clear->setStyle(ui::Button::Style::Danger);
        clear->setIcon(ui::Icon::Clear);
        for (const auto p : mem::kPersistentAll) {
            const auto& info = mem::partInfo(mem::persistentPart(p));
            auto* box = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>(std::string(info.label), base + "persistant." + std::string(mem::persistentKey(p)))));
            persistent.push_back({p, box});
        }
        understood = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>(
            "Je comprends : ces donn\xC3\xA9" "es seront supprim\xC3\xA9" "es d\xC3\xA9" "finitivement de ce PC.", base + "comprends")));
        erase = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Supprimer d\xC3\xA9" "finitivement\xE2\x80\xA6", base + "supprimer")));
        erase->setStyle(ui::Button::Style::Danger);
        erase->setIcon(ui::Icon::Delete);
    }

    std::vector<std::pair<mem::Part, ui::Checkbox*>>       cache;
    ui::Button*                                            clear{nullptr};
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override { return tipAt(tips_, mouse); }
    [[nodiscard]] bool hasTooltip() const override { return !tips_.empty() || ui::Widget::hasTooltip(); }
    std::vector<std::pair<mem::Persistent, ui::Checkbox*>> persistent;
    ui::Checkbox*                                          understood{nullptr};
    ui::Button*                                            erase{nullptr};

protected:
    void onLayout() override {
        const auto r = bounds();
        const float x0 = r.x + kPad, W = r.w - 2.f * kPad;
        const float top = r.y + 12.f, h = r.bottom() - 12.f - top;
        left_ = {x0, top, std::floor((W - kGap) * 0.5f), h};
        right_ = {left_.right() + kGap, top, x0 + W - left_.right() - kGap, h};
        float y = left_.y + 62.f;
        cacheRows_.clear();
        for (auto& [p, box] : cache) {
            const bool show = p != mem::Part::CacheOther || d_.measure_.of(p).files > 0;
            box->setVisibility(show ? ui::Visibility::Visible : ui::Visibility::Collapsed);
            if (!show) continue;
            box->setBounds({left_.x + 14.f, y, left_.w * 0.62f, 22.f});
            cacheRows_.push_back({p, y});
            y += rowH_;
        }
        cacheEnd_ = y;
        const float cw = std::max(170.f, ui::measureWidth(clear->text(), gfx::FontId{16}) + 56.f);
        clear->setBounds({left_.x + 14.f, left_.bottom() - 46.f, cw, 32.f});
        y = right_.y + 80.f;
        persistentRows_.clear();
        for (auto& [p, box] : persistent) {
            box->setBounds({right_.x + 14.f, y, right_.w * 0.6f, 22.f});
            persistentRows_.push_back({p, y});
            y += rowH_;
        }
        persistentEnd_ = y;
        understood->setBounds({right_.x + 14.f, right_.bottom() - 84.f, right_.w - 28.f, 24.f});
        const float ew = std::max(200.f, ui::measureWidth(erase->text(), gfx::FontId{16}) + 56.f);
        erase->setBounds({right_.x + 14.f, right_.bottom() - 46.f, ew, 32.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const TipScope tips(tips_);
        const auto& c = ctx.theme.color;
        const auto& m = d_.measure_;
        ctx.r.fillRect(bounds(), c.windowBg);
        // ---- vider le cache
        card(ctx, left_, c.border);
        caption(ctx, left_.x + 14.f, left_.y + 12.f, ui::Icon::Clear, "VIDER LE CACHE", c.textMuted);
        drawFit(ctx, {left_.x + 14.f, left_.y + 32.f},
                "Seul le dossier cache/ est vid\xC3\xA9 : ce qui se r\xC3\xA9g\xC3\xA9n\xC3\xA8re. Coche les cat\xC3\xA9gories \xC3\xA0 vider.", kSmall, left_.w - 28.f,
                c.textMuted);
        const float right = left_.right() - 14.f;
        for (const auto& [p, y] : cacheRows_) {
            const auto u = m.of(p);
            textRight(ctx, right, y + 4.f, mem::sizeText(u.bytes) + kDot + plural(u.files, "fichier", "fichiers"), kSmall, u.files ? c.text : c.textMuted);
            drawFit(ctx, {left_.x + 40.f, y + 23.f}, std::string(mem::cacheConsequence(p)), kNote, left_.w - 54.f, c.textMuted);
        }
        float y = cacheEnd_ + 4.f;
        const auto sel = d_.cacheSelection();
        std::size_t n = 0;
        for (const auto& [p, box] : cache)
            if (box->visible() && box->isChecked()) ++n;
        ctx.r.line({left_.x + 14.f, y}, {right, y}, c.border, 1.f);
        y += 8.f;
        drawBold(ctx, {left_.x + 14.f, y},
                 fit(ctx, "\xC3\x80 lib\xC3\xA9rer : " + mem::sizeText(sel.bytes) + " (" + plural(sel.files, "fichier", "fichiers") + ")" + kDot
                              + plural(n, "cat\xC3\xA9gorie coch\xC3\xA9" "e", "cat\xC3\xA9gories coch\xC3\xA9" "es"),
                     kText, left_.w - 28.f),
                 kText, c.text);
        y += 24.f;
        for (const auto& l : wrap(ctx, "Jamais touch\xC3\xA9s par le nettoyage : les conversations, les connaissances, les pi\xC3\xA8" "ces jointes originales, "
                                       "les projets, les param\xC3\xA8tres.",
                                  kSmall, left_.w - 28.f, 2)) {
            if (y + 16.f > left_.bottom() - 54.f) break;
            ctx.r.drawText({left_.x + 14.f, y}, l, kSmall, c.textMuted);
            y += 17.f;
        }
        const std::string last = "Dernier nettoyage : " + (m.lastCleaning.empty() ? std::string("jamais") : mem::displayDate(m.lastCleaning));
        textRight(ctx, right, left_.bottom() - 46.f + (32.f - ctx.r.lineHeight(kSmall)) * 0.5f, last, kSmall, c.textMuted);
        // ---- la zone rouge
        const gfx::Color red = ctx.theme.onSurface(c.error);
        card(ctx, right_, red);
        ctx.r.fillRect({right_.x + 1.f, right_.y + 1.f, right_.w - 2.f, 30.f}, c.error.withAlpha(ctx.theme.isDark() ? 40 : 26));
        caption(ctx, right_.x + 14.f, right_.y + 10.f, ui::Icon::Warning, "SUPPRIMER LES DONN\xC3\x89" "ES PERSISTANTES", red);
        float wy = right_.y + 38.f;
        for (const auto& l : wrap(ctx, "Ce qui est supprim\xC3\xA9 ici ne se r\xC3\xA9" "cup\xC3\xA8re pas (pas de corbeille). Les projets et les param\xC3\xA8tres ne sont jamais "
                                       "touch\xC3\xA9s.",
                                  kSmall, right_.w - 28.f, 2)) {
            ctx.r.drawText({right_.x + 14.f, wy}, l, kSmall, c.text);
            wy += 17.f;
        }
        const float rr = right_.right() - 14.f;
        const auto plan = mem::persistentPlan(m);
        for (const auto& [p, yy] : persistentRows_) {
            const auto& line = plan[static_cast<std::size_t>(p)];
            textRight(ctx, rr, yy + 4.f, mem::sizeText(line.usage.bytes) + kDot + plural(line.usage.files, "fichier", "fichiers"), kSmall,
                      line.usage.files ? c.text : c.textMuted);
            drawFit(ctx, {right_.x + 40.f, yy + 23.f}, line.consequence, kNote, right_.w - 54.f, c.textMuted);
        }
        (void)persistentEnd_;
    }

private:
    AiMemoryDialog& d_;
    std::vector<Tip> tips_;
    gfx::Rect left_{}, right_{};
    std::vector<std::pair<mem::Part, float>> cacheRows_;
    std::vector<std::pair<mem::Persistent, float>> persistentRows_;
    float cacheEnd_{0}, persistentEnd_{0};
    const float rowH_{46.f};
};

// ============================================================================ Activite ===
class AiMemoryDialog::ActivityPage final : public ui::Widget {
public:
    explicit ActivityPage(AiMemoryDialog& d) : ui::Widget("ia.memoire.activite"), d_(d) {
        const std::string base = "ia.memoire.activite.";
        typeBox = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "type")));
        std::vector<ui::DropDown::Item> items{{"Tous les types", "", {}, true}};
        for (const auto t : mem::kActivityTypes) items.push_back({mem::activityTypeLabel(t), std::string(t), {}, true});
        typeBox->setItems(std::move(items));
        typeBox->setSelectedIndex(0);
        model = std::make_shared<Rows>();
        model->headers = {"Date", "Type", "Texte"};
        table = &makeTable(*this, base + "journal", model, {col("Date", 150.f), col("Type", 160.f), col("Texte", 820.f)});
    }

    ui::DropDown*         typeBox{nullptr};
    std::shared_ptr<Rows> model;
    ui::TableView*        table{nullptr};

protected:
    void onLayout() override {
        const auto r = bounds();
        const float x0 = r.x + kPad, W = r.w - 2.f * kPad;
        labelX_ = x0;
        labelY_ = r.y + 12.f;
        const float lw = ui::measureWidth("Type", kSmall) + 8.f;
        typeBox->setBounds({x0 + lw, r.y + 12.f, 230.f, 30.f});
        countX_ = x0 + lw + 244.f;
        table->setBounds({x0, r.y + 52.f, W, r.bottom() - 10.f - (r.y + 52.f)});
        // 1.14.2 (captures) : le Texte prend toute la largeur qui reste (la barre de defilement a part) ; une ligne
        // plus longue encore se lit en entier dans son infobulle (rebuildTables).
        const float textCol = std::max(300.f, W - 150.f - 160.f - 20.f);
        if (std::abs(textCol - textCol_) > 0.5f) {
            textCol_ = textCol;
            table->setColumns({col("Date", 150.f), col("Type", 160.f), col("Texte", textCol)});
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(bounds(), c.windowBg);
        const gfx::Rect band{0.f, labelY_, 0.f, 30.f};
        textMid(ctx, labelX_, band, "Type", kSmall, c.textMuted);
        const std::size_t shown = d_.activityRowCount(), all = d_.activity_.size();
        textMid(ctx, countX_, band,
                plural(shown, "\xC3\xA9v\xC3\xA9nement", "\xC3\xA9v\xC3\xA9nements") + (shown != all ? " sur " + std::to_string(all) : std::string{}) + kDot
                    + "activite.jsonl, le plus r\xC3\xA9" "cent en haut",
                kSmall, c.textMuted);
    }

private:
    AiMemoryDialog& d_;
    float labelX_{0}, labelY_{0}, countX_{0}, textCol_{0};
};

// ============================================================================== le corps ===
class AiMemoryDialog::Body final : public ui::Widget {
public:
    explicit Body(AiMemoryDialog& d) : d_(d) {}
    gfx::Rect panel{};
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override { return tipAt(tips_, mouse); }
    [[nodiscard]] bool hasTooltip() const override { return !tips_.empty() || ui::Widget::hasTooltip(); }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(1240.f, r.w - 40.f), h = std::min(800.f, r.h - 40.f);
        panel = ui::dialogframe::place(*this, {std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h});
        const float th = ui::dialogframe::kTitleBand;
        const float bottom = panel.bottom() - 52.f - (d_.confirming() ? 48.f : 0.f);
        if (d_.tabs_) d_.tabs_->setBounds({panel.x + 1.f, panel.y + th, panel.w - 2.f, bottom - panel.y - th});
        const float by = panel.bottom() - 42.f;
        float ex = panel.right() - 14.f;
        for (ui::Button* b : {d_.close_, d_.refreshButton_}) {
            if (!b) continue;
            const float bw = std::max(110.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 40.f);
            ex -= bw;
            b->setBounds({ex, by, bw, 32.f});
            ex -= 10.f;
        }
        messageX_ = panel.x + 14.f;
        messageW_ = ex - messageX_ - 10.f;
        confirm_ = {panel.x + 14.f, by - 50.f, panel.w - 28.f, 40.f};
        float cx = confirm_.right() - 8.f;
        for (ui::Button* b : {d_.no_, d_.yes_}) {
            if (!b) continue;
            const float bw = std::max(96.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 36.f);
            cx -= bw;
            b->setBounds({cx, confirm_.y + 4.f, bw, 32.f});
            cx -= 8.f;
        }
        confirmTextW_ = cx - confirm_.x - 40.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const TipScope tips(tips_);
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel, c.panelBg);
        ctx.r.strokeRect(panel, c.borderStrong, 1.f);
        const float th = ui::dialogframe::kTitleBand;
        ctx.r.fillRect({panel.x, panel.y, panel.w, th}, c.headerBg);
        ui::drawIcon(ctx.r, ui::Icon::Library, {panel.x + 12.f, panel.y + (th - 16.f) * 0.5f, 16.f, 16.f}, c.textMuted);
        ctx.r.drawText({panel.x + 36.f, panel.y + (th - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, d_.title(), ctx.theme.font.uiBold, c.text);
        const std::string right = "Donn\xC3\xA9" "es de l'Assistant sur ce PC" + kDot + "rien n'est envoy\xC3\xA9 d'ici";
        textRight(ctx, panel.right() - 16.f, panel.y + (th - ctx.r.lineHeight(kSmall)) * 0.5f, right, kSmall, c.textMuted);
        // la bande du bas
        ctx.r.line({panel.x, panel.bottom() - 52.f}, {panel.right(), panel.bottom() - 52.f}, c.border, 1.f);
        if (!d_.message_.empty() && messageW_ > 40.f)
            drawFit(ctx, {messageX_, panel.bottom() - 42.f + (32.f - ctx.r.lineHeight(kSmall)) * 0.5f}, d_.message_, kSmall, messageW_,
                    d_.messageBad_ ? c.error : ctx.theme.onSurface(c.ok));
        if (d_.confirming()) {
            ctx.r.fillRoundedRect(confirm_, ctx.theme.brand.card, 4.f);
            ctx.r.strokeRect(confirm_, c.error, 1.f);
            ui::drawIcon(ctx.r, ui::Icon::Warning, {confirm_.x + 12.f, confirm_.y + 12.f, 16.f, 16.f}, c.error);
            const auto lines = wrap(ctx, d_.confirm_.text, kSmall, confirmTextW_, 2);
            float ly = confirm_.y + (lines.size() > 1 ? 4.f : (confirm_.h - ctx.r.lineHeight(kSmall)) * 0.5f);
            for (const auto& l : lines) {
                ctx.r.drawText({confirm_.x + 36.f, ly}, l, kSmall, c.text);
                ly += 16.f;
            }
            if (!lines.empty() && lines.back().size() >= 3 && lines.back().compare(lines.back().size() - 3, 3, kDots) == 0)
                addTip({confirm_.x + 36.f, confirm_.y, confirmTextW_, confirm_.h}, d_.confirm_.text);
        }
    }

private:
    AiMemoryDialog& d_;
    std::vector<Tip> tips_;
    gfx::Rect confirm_{};
    float messageX_{0}, messageW_{0}, confirmTextW_{0};
};

// ============================================================================ la fenetre ===
AiMemoryDialog::AiMemoryDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) {
    for (const auto& p : {mem::Part::CacheRequests, mem::Part::CacheProcessing, mem::Part::CacheImages, mem::Part::CacheDocuments,
                          mem::Part::CachePreviews, mem::Part::CacheDownloads, mem::Part::CacheOther})
        cacheOn_.push_back(p);     // par defaut : tout le cache (il se regenere)
    refresh();
}

AiMemoryDialog::~AiMemoryDialog() = default;

menu::MenuTraits AiMemoryDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = !spec_.overModal;   // sur une autre fenetre modale : son voile suffit
    return t;
}

std::size_t AiMemoryDialog::messageCount() const noexcept {
    std::size_t n = 0;
    for (const auto& c : conversations_) n += c.messages.size();
    return n;
}

std::string AiMemoryDialog::stateText() const {
    if (measure_.root.empty()) return "aucun dossier IA n'est fix\xC3\xA9";
    std::size_t added = 0;
    for (const auto& k : registry_.all())
        if (!k.builtIn) ++added;
    if (!measure_.exists)
        return "vide \xE2\x80\x94 le dossier IA/ se cr\xC3\xA9" "era avec la premi\xC3\xA8re conversation ; "
               + plural(registry_.count(true), "connaissance du logiciel active", "connaissances du logiciel actives");
    if (measure_.total.files == 0)
        return "vide \xE2\x80\x94 rien d'enregistr\xC3\xA9 encore ; " + plural(registry_.count(true), "connaissance du logiciel active", "connaissances du logiciel actives");
    return "active \xE2\x80\x94 " + plural(conversations_.size(), "conversation", "conversations") + ", " + plural(messageCount(), "message", "messages") + ", "
           + plural(added, "connaissance ajout\xC3\xA9" "e", "connaissances ajout\xC3\xA9" "es") + ", " + mem::sizeText(measure_.total.bytes) + " sur le disque";
}

void AiMemoryDialog::refreshRam() {
    ram_ = mem::readRam();
    if (overview_) overview_->invalidate();
}

void AiMemoryDialog::refresh() {
    // 1.15.0 : l'etat reel de l'IA (les parametres de l'Assistant), pas toujours "demonstration".
    {
        const ai::Settings st = ai::loadSettings();
        std::string label;
        for (const auto& p : ai::providers())
            if (p.key == st.provider) label = std::string(p.label);
        if (!st.enabled || st.provider.empty() || st.provider == "demo")
            connection_ = "aucune IA connect\xC3\xA9" "e (d\xC3\xA9monstration)";
        else
            connection_ = "IA : " + (label.empty() ? st.provider : label);
    }
    measure_ = mem::measure();
    ram_ = mem::readRam();
    registry_.load();
    conversations_ = mem::loadConversations();
    attachments_ = mem::attachmentFolders(conversations_);
    activity_ = mem::readActivity();
    // la zone rouge : une categorie devenue vide se decoche (elle se grise, syncWidgets)
    persistentOn_.erase(std::remove_if(persistentOn_.begin(), persistentOn_.end(), [&](mem::Persistent p) { return persistentEmpty(p); }),
                        persistentOn_.end());
    hits_ = search_.empty() ? std::vector<mem::Hit>{} : mem::search(search_, conversations_, registry_.all());
    if (!selectedKnowledge_.empty() && !registry_.find(selectedKnowledge_)) selectedKnowledge_.clear();
    rebuildTables();
    syncWidgets();
    if (body_) {
        body_->invalidateLayout();
        body_->invalidate();
    }
}

void AiMemoryDialog::Update(const menu::FrameContext& fc) {
    menu::WidgetMenu::Update(fc);
    // La memoire vive bouge : relue chaque seconde (la Vue generale le dit).
    if (fc.totalSeconds - ramAt_ >= 1.0) {
        ramAt_ = fc.totalSeconds;
        if (tab() == Tab::Overview) refreshRam();
    }
}

// ---- les onglets --------------------------------------------------------------------------
std::string AiMemoryDialog::tabTitle(Tab t) {
    switch (t) {
        case Tab::Overview: return "Vue g\xC3\xA9n\xC3\xA9rale";
        case Tab::Learning: return "Apprentissage";
        case Tab::Memory: return "M\xC3\xA9moire";
        case Tab::Storage: return "Stockage PC";
        case Tab::Cleaning: return "Nettoyage";
        case Tab::Activity: return "Activit\xC3\xA9";
    }
    return {};
}

void AiMemoryDialog::showTab(Tab t) {
    if (tabs_) tabs_->setCurrentIndex(static_cast<std::size_t>(t));
}

AiMemoryDialog::Tab AiMemoryDialog::tab() const noexcept { return tabs_ ? static_cast<Tab>(tabs_->currentIndex()) : Tab::Overview; }

bool AiMemoryDialog::showTab(std::string_view title) {
    const std::string want = folded(title);
    if (want.empty()) return false;
    for (int i = 0; i <= static_cast<int>(Tab::Activity); ++i) {
        const std::string t = folded(tabTitle(static_cast<Tab>(i)));
        if (t == want || t.rfind(want, 0) == 0) {
            showTab(static_cast<Tab>(i));
            return true;
        }
    }
    return false;
}

// ---- les tables ---------------------------------------------------------------------------
void AiMemoryDialog::rebuildTables() {
    // Apprentissage
    if (learning_) {
        auto& m = *learning_->model;
        m.rows.clear();
        const auto shown = shownKnowledge();
        // 1.14.2 (captures, seconde passe) : LE DETAIL SUIT LA LISTE. Une selection que les filtres cachent passe a
        // la premiere ligne visible (aucune : le detail le dit) - il montrait encore une connaissance absente de la
        // liste, avec Consulter et Desactiver actifs.
        if (!selectedKnowledge_.empty()
            && std::none_of(shown.begin(), shown.end(), [&](const mem::Knowledge* k) { return k->id == selectedKnowledge_; }))
            selectedKnowledge_ = shown.empty() ? std::string{} : shown.front()->id;
        for (const auto* k : shown) {
            Rows::Row row;
            const auto here = mem::hereState(*k, spec_.project);
            row.key = k->id;
            row.cells = {k->name,
                         mem::categoryLabel(k->category),
                         mem::sourceLabel(k->source),
                         k->project.empty() ? std::string("Tous") : k->project,
                         k->builtIn ? std::string("int\xC3\xA9gr\xC3\xA9" "e") : dateOrDash(k->added).substr(0, 10),
                         k->builtIn ? std::string("int\xC3\xA9gr\xC3\xA9" "e") : dateOrDash(k->modified).substr(0, 10),
                         mem::sizeText(k->size()),
                         mem::hereLabel(here)};
            const double nan = std::nan("");
            row.sort = {nan, nan, nan, nan, nan, nan, static_cast<double>(k->size()), nan};
            row.styles.resize(8);
            row.styles[0].icon = k->builtIn ? ui::Icon::Library : ui::Icon::Document;
            row.styles[0].iconTone = ui::Tone::Muted;
            row.styles[7] = toneStyle(here == mem::Here::Active ? ui::Tone::Ok : here == mem::Here::Inactive ? ui::Tone::Muted : ui::Tone::Warning);
            row.tip = k->description;
            m.rows.push_back(std::move(row));
        }
        m.reset();
        selectKey(learning_->table, m.find(selectedKnowledge_));
        learning_->invalidate();
    }
    // Memoire
    if (memory_) {
        auto& m = *memory_->model;
        m.rows.clear();
        const double nan = std::nan("");
        std::vector<ui::TableView::Column> cols;
        const auto knowledgeRows = [&](bool preferencesOnly) {
            cols = {col("Nom", 250.f), col("Cat\xC3\xA9gorie", 205.f), col("Projet", 150.f), col("Source", 135.f), col("\xC3\x89tat", 100.f),
                    col("Taille", 80.f, ui::Align::End)};
            for (const auto& k : registry_.all()) {
                if (preferencesOnly && k.category != "preferences") continue;
                const auto here = mem::hereState(k, spec_.project);
                Rows::Row row;
                row.key = "k:" + k.id;
                row.cells = {k.name, mem::categoryLabel(k.category), k.project.empty() ? std::string("Tous") : k.project, mem::sourceLabel(k.source),
                             mem::hereLabel(here), mem::sizeText(k.size())};
                row.sort = {nan, nan, nan, nan, nan, static_cast<double>(k.size())};
                row.styles.resize(6);
                row.styles[0].icon = k.builtIn ? ui::Icon::Library : ui::Icon::Document;
                row.styles[0].iconTone = ui::Tone::Muted;
                row.styles[4] = toneStyle(here == mem::Here::Active ? ui::Tone::Ok : here == mem::Here::Inactive ? ui::Tone::Muted : ui::Tone::Warning);
                row.tip = k.description;
                m.rows.push_back(std::move(row));
            }
        };
        switch (memCat_) {
            case MemCat::Conversations:
                cols = {col("Titre", 250.f), col("Projet", 150.f), col("Messages", 100.f, ui::Align::End), col("Pi\xC3\xA8" "ces", 75.f, ui::Align::End),
                        col("Modifi\xC3\xA9" "e", 145.f), col("Taille", 80.f, ui::Align::End), col("Origine", 130.f)};
                for (const auto& c : conversations_) {
                    Rows::Row row;
                    row.key = "c:" + c.id;
                    row.cells = {c.readable ? c.title : c.title + " (illisible)", c.project.empty() ? std::string("\xE2\x80\x94") : c.project,
                                 std::to_string(c.messages.size()), std::to_string(c.attachments.size()), dateOrDash(c.modified), mem::sizeText(c.bytes),
                                 c.demo ? std::string("d\xC3\xA9monstration") : std::string("utilisateur")};
                    row.sort = {nan, nan, static_cast<double>(c.messages.size()), static_cast<double>(c.attachments.size()), nan, static_cast<double>(c.bytes), nan};
                    row.styles.resize(7);
                    row.styles[0].icon = ui::Icon::Comment;
                    row.styles[0].iconTone = ui::Tone::Muted;
                    // 1.14.2 (captures) : le titre en entier d'abord (la case le coupe), le fichier ensuite
                    row.tip = c.title + "\n" + (c.readable ? c.file : c.file + " : " + c.why);
                    m.rows.push_back(std::move(row));
                }
                break;
            case MemCat::Messages:
                cols = {col("Date", 145.f), col("Conversation", 210.f), col("R\xC3\xB4le", 105.f), col("Texte", 470.f)};
                for (const auto& c : conversations_)
                    for (std::size_t i = 0; i < c.messages.size(); ++i) {
                        const auto& msg = c.messages[i];
                        std::string text = msg.text;
                        std::replace(text.begin(), text.end(), '\n', ' ');
                        Rows::Row row;
                        row.key = "m:" + c.id + ":" + std::to_string(i);
                        row.cells = {dateOrDash(msg.date), c.title, msg.role == "assistant" ? std::string("Assistant") : std::string("Utilisateur"), text};
                        row.tip = msg.text.size() > 600 ? msg.text.substr(0, 600) + kDots : msg.text;
                        m.rows.push_back(std::move(row));
                    }
                break;
            case MemCat::Projects: {
                cols = {col("Projet", 240.f), col("Conversations", 130.f, ui::Align::End), col("Connaissances", 130.f, ui::Align::End),
                        col("Derni\xC3\xA8re conversation", 170.f), col("Ici", 90.f)};
                std::vector<std::string> names = registry_.projects();
                for (const auto& c : conversations_)
                    if (!c.project.empty() && std::find(names.begin(), names.end(), c.project) == names.end()) names.push_back(c.project);
                if (!spec_.project.empty() && std::find(names.begin(), names.end(), spec_.project) == names.end()) names.push_back(spec_.project);
                std::sort(names.begin(), names.end());
                for (const auto& p : names) {
                    std::size_t convs = 0, know = 0;
                    std::string last;
                    for (const auto& c : conversations_)
                        if (c.project == p) {
                            ++convs;
                            last = std::max(last, c.modified);
                        }
                    for (const auto& k : registry_.all())
                        if (k.project == p) ++know;
                    Rows::Row row;
                    row.key = "p:" + p;
                    row.cells = {p, std::to_string(convs), std::to_string(know), dateOrDash(last), p == spec_.project ? std::string("ouvert") : std::string{}};
                    row.sort = {nan, static_cast<double>(convs), static_cast<double>(know), nan, nan};
                    row.styles.resize(5);
                    row.styles[0].icon = ui::Icon::Project;
                    row.styles[0].iconTone = ui::Tone::Muted;
                    m.rows.push_back(std::move(row));
                }
                break;
            }
            case MemCat::Preferences: knowledgeRows(true); break;
            case MemCat::Knowledge: knowledgeRows(false); break;
            case MemCat::Attachments:
                cols = {col("Fichiers", 250.f), col("Dossier", 230.f), col("Taille", 80.f, ui::Align::End), col("Nombre", 80.f, ui::Align::End),
                        col("Conversation", 270.f)};
                for (const auto& a : attachments_) {
                    Rows::Row row;
                    row.key = "a:" + a.id;
                    row.cells = {a.names.empty() ? std::string("(vide)") : a.names, "pieces-jointes/" + a.id, mem::sizeText(a.usage.bytes),
                                 std::to_string(a.usage.files), a.conversation.empty() ? std::string("aucune (orpheline)") : a.conversationTitle};
                    row.sort = {nan, nan, static_cast<double>(a.usage.bytes), static_cast<double>(a.usage.files), nan};
                    row.styles.resize(5);
                    row.styles[0].icon = ui::Icon::Document;
                    row.styles[0].iconTone = ui::Tone::Muted;
                    if (a.conversation.empty()) row.styles[4] = toneStyle(ui::Tone::Warning);
                    row.tip = (a.names.empty() ? std::string("(vide)") : a.names) + "\n" + a.folder;
                    m.rows.push_back(std::move(row));
                }
                break;
            case MemCat::Search:
                cols = {col("Type", 190.f), col("Titre", 230.f), col("Trouv\xC3\xA9", 500.f)};
                for (std::size_t i = 0; i < hits_.size(); ++i) {
                    const auto& h = hits_[i];
                    Rows::Row row;
                    switch (h.kind) {
                        case mem::Hit::Kind::Conversation:
                        case mem::Hit::Kind::Message: row.key = "c:" + h.ref; break;
                        case mem::Hit::Kind::Knowledge: row.key = "k:" + h.ref; break;
                        case mem::Hit::Kind::Attachment: {
                            std::string f = h.ref;
                            std::replace(f.begin(), f.end(), '\\', '/');
                            const auto a = f.find('/');
                            const auto b = a == std::string::npos ? std::string::npos : f.find('/', a + 1);
                            row.key = "a:" + (b == std::string::npos ? f : f.substr(a + 1, b - a - 1));
                            break;
                        }
                    }
                    row.key += "#" + std::to_string(i);
                    row.cells = {h.where, h.title, h.excerpt};
                    row.tip = h.title + "\n" + h.excerpt;
                    m.rows.push_back(std::move(row));
                }
                break;
        }
        m.headers.clear();
        for (const auto& c : cols) m.headers.push_back(c.title);
        memory_->table->setColumns(cols);
        m.reset();
        selectKey(memory_->table, m.find(selectedMemory_));
        if (m.find(selectedMemory_) < 0) selectedMemory_.clear();
        // la liste des categories
        std::size_t prefs = 0, know = 0;
        for (const auto& k : registry_.all()) {
            if (k.category == "preferences") ++prefs;
            ++know;
        }
        std::vector<std::string> projects = registry_.projects();
        for (const auto& c : conversations_)
            if (!c.project.empty() && std::find(projects.begin(), projects.end(), c.project) == projects.end()) projects.push_back(c.project);
        std::uintmax_t convBytes = 0;
        for (const auto& c : conversations_) convBytes += c.bytes;
        std::uintmax_t attBytes = 0;
        for (const auto& a : attachments_) attBytes += a.usage.bytes;
        auto& e = memory_->entries;
        e.clear();
        if (!search_.empty()) e.push_back({MemCat::Search, memCatTitle(MemCat::Search), plural(hits_.size(), "r\xC3\xA9sultat", "r\xC3\xA9sultats")});
        e.push_back({MemCat::Conversations, memCatTitle(MemCat::Conversations), plural(conversations_.size(), "conversation", "conversations") + kDot + mem::sizeText(convBytes)});
        e.push_back({MemCat::Messages, memCatTitle(MemCat::Messages), plural(messageCount(), "message conserv\xC3\xA9", "messages conserv\xC3\xA9s")});
        e.push_back({MemCat::Projects, memCatTitle(MemCat::Projects), plural(projects.size(), "projet", "projets")});
        e.push_back({MemCat::Preferences, memCatTitle(MemCat::Preferences), plural(prefs, "pr\xC3\xA9" "f\xC3\xA9rence", "pr\xC3\xA9" "f\xC3\xA9rences")});
        e.push_back({MemCat::Knowledge, memCatTitle(MemCat::Knowledge), plural(know, "connaissance", "connaissances")});
        e.push_back({MemCat::Attachments, memCatTitle(MemCat::Attachments), plural(attachments_.size(), "dossier joint", "dossiers joints") + kDot + mem::sizeText(attBytes)});
        memory_->invalidateLayout();
        memory_->invalidate();
    }
    // Stockage PC
    if (storage_) {
        auto& m = *storage_->model;
        m.rows.clear();
        const double nan = std::nan("");
        for (std::size_t i = 0; i < mem::kPartCount; ++i) {
            const auto p = static_cast<mem::Part>(i);
            const auto& info = mem::partInfo(p);
            const auto u = measure_.of(p);
            if ((p == mem::Part::CacheOther || p == mem::Part::Other) && u.files == 0) continue;
            Rows::Row row;
            row.key = std::string(info.key);
            row.cells = {std::string(info.label), std::string(info.where), std::to_string(u.files),
                         mem::sizeText(u.bytes), percent(static_cast<double>(u.bytes), static_cast<double>(measure_.total.bytes))};
            row.sort = {nan, nan, static_cast<double>(u.files), static_cast<double>(u.bytes), static_cast<double>(u.bytes)};
            row.styles.resize(5);
            row.styles[0].icon = info.cache ? ui::Icon::Clear : info.part == mem::Part::Activity || info.part == mem::Part::Settings ? ui::Icon::Document : ui::Icon::Folder;
            row.styles[0].iconTone = ui::Tone::Muted;
            if (!u.files) row.styles[2] = row.styles[3] = row.styles[4] = toneStyle(ui::Tone::Muted);
            m.rows.push_back(std::move(row));
        }
        m.reset();
        storage_->invalidate();
    }
    // Activite
    if (activityPage_) {
        auto& m = *activityPage_->model;
        m.rows.clear();
        for (auto it = activity_.rbegin(); it != activity_.rend(); ++it) {
            if (!activityFilter_.empty() && it->type != activityFilter_) continue;
            Rows::Row row;
            row.key = it->date + it->type;
            row.cells = {mem::displayDate(it->date), mem::activityTypeLabel(it->type), it->text};
            row.styles.resize(3);
            ui::Icon icon = ui::Icon::Info;
            if (it->type == "conversation") icon = ui::Icon::Comment;
            else if (it->type == "connaissance") icon = ui::Icon::Library;
            else if (it->type == "fichier") icon = ui::Icon::Document;
            else if (it->type == "regle") icon = ui::Icon::Wrench;
            else if (it->type == "nettoyage") icon = ui::Icon::Clear;
            else if (it->type == "suppression") icon = ui::Icon::Delete;
            row.styles[1] = toneStyle(it->type == "suppression" ? ui::Tone::Warning : ui::Tone::Muted, icon);
            row.styles[1].fgTone = ui::Tone::None;
            row.tip = it->text;
            m.rows.push_back(std::move(row));
        }
        m.reset();
        activityPage_->invalidate();
    }
}

void AiMemoryDialog::syncWidgets() {
    syncing_ = true;
    if (learning_) {
        auto& p = *learning_;
        std::vector<ui::DropDown::Item> cats{{"Toutes les cat\xC3\xA9gories", "", {}, true}};
        int sel = 0;
        for (const auto& c : mem::kCategories) {
            if (c.key == catFilter_) sel = static_cast<int>(cats.size());
            cats.push_back({std::string(c.label), std::string(c.key), {}, true});
        }
        p.catBox->setItems(std::move(cats));
        p.catBox->setSelectedIndex(sel);
        std::vector<ui::DropDown::Item> projs{{"Tous les projets", "", {}, true}, {"Communes \xC3\xA0 tous les projets", "*", {}, true}};
        sel = projectFilter_ == "*" ? 1 : 0;
        std::vector<std::string> names = registry_.projects();
        if (!spec_.project.empty() && std::find(names.begin(), names.end(), spec_.project) == names.end()) names.push_back(spec_.project);
        std::sort(names.begin(), names.end());
        for (const auto& n : names) {
            if (n == projectFilter_) sel = static_cast<int>(projs.size());
            projs.push_back({"Projet " + n + (n == spec_.project ? " (ouvert)" : ""), n, {}, true});
        }
        p.projBox->setItems(std::move(projs));
        p.projBox->setSelectedIndex(sel);
        const auto* k = selectedKnowledge();
        p.view->setEnabled(k != nullptr);
        p.edit->setEnabled(k && !k->builtIn);
        p.edit->setTooltip(k && k->builtIn ? "Int\xC3\xA9gr\xC3\xA9" "e au logiciel : elle ne se modifie pas (elle se d\xC3\xA9sactive)." : std::string{});
        p.toggle->setEnabled(k != nullptr);
        p.toggle->setText(k && !k->active ? "Activer" : "D\xC3\xA9sactiver");
        p.remove->setEnabled(k && !k->builtIn);
        p.remove->setTooltip(k && k->builtIn ? "Int\xC3\xA9gr\xC3\xA9" "e au logiciel : elle se d\xC3\xA9sactive, elle ne se supprime pas." : std::string{});
        learning_->invalidateLayout();
    }
    if (memory_) {
        auto& p = *memory_;
        if (p.search->text() != search_) p.search->setText(search_);
        const std::string key = selectedMemory_;
        std::string one = "Supprimer\xE2\x80\xA6";
        bool canOne = false;
        if (key.rfind("c:", 0) == 0) {
            one = "Supprimer la conversation\xE2\x80\xA6";
            canOne = true;
        } else if (key.rfind("m:", 0) == 0) {
            one = "Supprimer sa conversation\xE2\x80\xA6";
            canOne = true;
        } else if (key.rfind("p:", 0) == 0) {
            one = "Oublier ce projet\xE2\x80\xA6";
            canOne = true;
        } else if (key.rfind("k:", 0) == 0) {
            one = "Supprimer cette m\xC3\xA9moire\xE2\x80\xA6";
            const std::string id = key.substr(2, key.find('#') == std::string::npos ? std::string::npos : key.find('#') - 2);
            const auto* k = registry_.find(id);
            canOne = k && !k->builtIn;
        } else if (key.rfind("a:", 0) == 0) {
            one = "Supprimer la pi\xC3\xA8" "ce jointe\xE2\x80\xA6";
            canOne = true;
        }
        p.removeOne->setText(one);
        p.removeOne->setEnabled(canOne);
        const bool canAll = memCat_ != MemCat::Search && memCat_ != MemCat::Projects && memoryRowCount() > 0;
        p.removeAll->setEnabled(canAll);
        if (key.rfind("k:", 0) == 0 && !canOne) p.hint_ = "Une connaissance du logiciel se d\xC3\xA9sactive (onglet Apprentissage), elle ne se supprime pas.";
        else if (key.rfind("c:", 0) == 0 || key.rfind("m:", 0) == 0) p.hint_ = "Ses pi\xC3\xA8" "ces jointes restent (cat\xC3\xA9gorie R\xC3\xA9" "f\xC3\xA9rences aux pi\xC3\xA8" "ces jointes).";
        else if (memCat_ == MemCat::Projects) p.hint_ = "Oublier un projet : ses connaissances et ses conversations.";
        else if (memCat_ == MemCat::Search) p.hint_ = "Choisis un r\xC3\xA9sultat pour le supprimer.";
        else p.hint_.clear();
        memory_->invalidateLayout();
    }
    if (storage_) {
        storage_->where->setText(location());
        storage_->open->setEnabled(!measure_.root.empty());
    }
    if (cleaning_) {
        for (auto& [p, box] : cleaning_->cache) {
            const bool on = std::find(cacheOn_.begin(), cacheOn_.end(), p) != cacheOn_.end();
            box->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
            box->setTooltip(std::string(mem::cacheConsequence(p)));
        }
        // 1.14.2 (captures) : UNE CATEGORIE VIDE NE SE COCHE PAS dans la zone rouge (grisee, son infobulle dit
        // pourquoi) ; Supprimer definitivement reste grise tant qu'aucune categorie non vide n'est cochee.
        const auto plan = mem::persistentPlan(measure_);
        bool any = false;
        for (auto& [p, box] : cleaning_->persistent) {
            const auto& line = plan[static_cast<std::size_t>(p)];
            const bool empty = persistentEmpty(p);
            const bool on = !empty && std::find(persistentOn_.begin(), persistentOn_.end(), p) != persistentOn_.end();
            any = any || on;
            box->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
            box->setEnabled(!empty);
            box->setTooltip(empty ? "Vide (0 fichier) : rien \xC3\xA0 supprimer." : line.consequence);
        }
        cleaning_->understood->setState(understood_ ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        cleaning_->clear->setEnabled(cacheSelection().files > 0);
        cleaning_->erase->setEnabled(understood_ && any);
        cleaning_->erase->setTooltip(!understood_ ? std::string("Coche d'abord \xC2\xAB Je comprends \xC2\xBB.")
                                     : !any       ? std::string("Coche d'abord une cat\xC3\xA9gorie qui contient des donn\xC3\xA9" "es.")
                                                  : std::string{});
        cleaning_->invalidateLayout();
        cleaning_->invalidate();
    }
    if (activityPage_) {
        int sel = 0;
        const auto& items = activityPage_->typeBox->items();
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].value == activityFilter_) sel = static_cast<int>(i);
        activityPage_->typeBox->setSelectedIndex(sel);
    }
    if (yes_ && no_) {
        const auto vis = confirming() ? ui::Visibility::Visible : ui::Visibility::Collapsed;
        yes_->setVisibility(vis);
        no_->setVisibility(vis);
        if (confirming()) yes_->setText(confirm_.yes);
    }
    syncing_ = false;
}

// ---- Apprentissage ----------------------------------------------------------------------------
void AiMemoryDialog::setCategoryFilter(std::string key) {
    catFilter_ = std::move(key);
    rebuildTables();
    syncWidgets();
}

void AiMemoryDialog::setProjectFilter(std::string project) {
    projectFilter_ = std::move(project);
    rebuildTables();
    syncWidgets();
}

std::vector<const mem::Knowledge*> AiMemoryDialog::shownKnowledge() const {
    std::vector<const mem::Knowledge*> out;
    for (const auto& k : registry_.all()) {
        if (!catFilter_.empty() && k.category != catFilter_) continue;
        if (projectFilter_ == "*" && !k.project.empty()) continue;
        if (!projectFilter_.empty() && projectFilter_ != "*" && k.project != projectFilter_) continue;
        out.push_back(&k);
    }
    return out;
}

bool AiMemoryDialog::selectKnowledge(std::string_view idOrName) {
    const mem::Knowledge* found = registry_.find(idOrName);
    const std::string want = folded(idOrName);
    if (!found)
        for (const auto& k : registry_.all())
            if (folded(k.name) == want) found = &k;
    if (!found)
        for (const auto& k : registry_.all())
            if (folded(k.name).rfind(want, 0) == 0) {
                found = &k;
                break;
            }
    if (!found) return false;
    selectedKnowledge_ = found->id;
    // un filtre qui la cache : retire
    bool shown = false;
    for (const auto* k : shownKnowledge()) shown = shown || k == found;
    if (!shown) {
        catFilter_.clear();
        projectFilter_.clear();
        rebuildTables();
    } else if (learning_) {
        selectKey(learning_->table, learning_->model->find(selectedKnowledge_));
    }
    syncWidgets();
    if (learning_) learning_->invalidate();
    return true;
}

const mem::Knowledge* AiMemoryDialog::selectedKnowledge() const { return selectedKnowledge_.empty() ? nullptr : registry_.find(selectedKnowledge_); }

bool AiMemoryDialog::addKnowledge(mem::Knowledge k) {
    std::string why;
    if (!registry_.add(k, &why)) {
        say("Ajout impossible : " + why, true);
        return false;
    }
    const std::string id = k.id, name = k.name;
    refresh();
    selectedKnowledge_ = id;
    if (learning_) selectKey(learning_->table, learning_->model->find(id));
    syncWidgets();
    say("Connaissance ajout\xC3\xA9" "e : " + name + " (" + mem::sizeText(k.size()) + ", jointe aux demandes"
        + (k.project.empty() ? std::string(" de tous les projets)") : " du projet " + k.project + ")"));
    return true;
}

bool AiMemoryDialog::updateSelected(const mem::Knowledge& changes) {
    const auto* k = selectedKnowledge();
    if (!k) return false;
    mem::Knowledge next = changes;
    next.id = k->id;
    std::string why;
    if (!registry_.update(next, &why)) {
        say("Modification refus\xC3\xA9" "e : " + why, true);
        return false;
    }
    const std::string name = next.name;
    refresh();
    say("Connaissance actualis\xC3\xA9" "e : " + name);
    return true;
}

bool AiMemoryDialog::toggleSelected() {
    const auto* k = selectedKnowledge();
    if (!k) return false;
    const bool on = !k->active;
    const std::string name = k->name;
    std::string why;
    if (!registry_.setActive(k->id, on, &why)) {
        say(why, true);
        return false;
    }
    refresh();
    say(std::string(on ? "Activ\xC3\xA9" "e : " : "D\xC3\xA9sactiv\xC3\xA9" "e : ") + name
        + (on ? " (jointe de nouveau aux demandes)" : " (elle ne part plus avec les demandes ; rien n'est effac\xC3\xA9)"));
    return true;
}

bool AiMemoryDialog::deleteSelected(bool now) {
    const auto* k = selectedKnowledge();
    if (!k) return false;
    if (k->builtIn) {
        say("Int\xC3\xA9gr\xC3\xA9" "e au logiciel : elle se d\xC3\xA9sactive, elle ne se supprime pas.", true);
        return false;
    }
    if (now) return doDeleteKnowledge();
    ask(Confirm::DeleteKnowledge, "Supprimer la connaissance \xC2\xAB " + k->name + " \xC2\xBB (" + mem::sizeText(k->size()) + ") ? Son fichier est effac\xC3\xA9 de ce PC.",
        "Oui, supprimer");
    return true;
}

bool AiMemoryDialog::doDeleteKnowledge() {
    const auto* k = selectedKnowledge();
    if (!k) return false;
    const std::string name = k->name;
    std::string why;
    if (!registry_.remove(k->id, &why)) {
        say("Suppression impossible : " + why, true);
        return false;
    }
    selectedKnowledge_.clear();
    refresh();
    say("Connaissance supprim\xC3\xA9" "e : " + name);
    return true;
}

void AiMemoryDialog::fillEditor(const mem::Knowledge* k, bool readOnly) {
    if (!learning_) return;
    auto& p = *learning_;
    editorReadOnly_ = readOnly;
    p.edName->setText(k ? k->name : std::string{});
    p.edDesc->setText(k ? k->description : std::string{});
    p.edProject->setText(k ? k->project : (projectFilter_ == "*" ? std::string{} : projectFilter_));
    // L'editeur de texte ne coupe pas les lignes : en lecture seule, les longues lignes (celles des tables du
    // logiciel) sont coupees pour l'oeil, a l'affichage seulement (rien n'est enregistre).
    p.edContent->setText(k ? (readOnly ? wrapped(k->content, 120) : k->content) : std::string{});
    int cat = static_cast<int>(std::size(mem::kCategories)) - 1;   // Connaissances metier
    const std::string want = k ? k->category : (catFilter_.empty() ? std::string("metier") : catFilter_);
    for (std::size_t i = 0; i < std::size(mem::kCategories); ++i)
        if (mem::kCategories[i].key == want) cat = static_cast<int>(i);
    p.edCat->setSelectedIndex(cat);
    p.edActive->setState(!k || k->active ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    for (ui::InputText* f : {p.edName, p.edDesc, p.edProject}) f->setReadOnly(readOnly);
    p.edContent->setReadOnly(readOnly);
    p.edCat->setEnabled(!readOnly);
    p.edActive->setEnabled(!readOnly);
    p.showEditor(true);
    // les champs montrent leur debut (le curseur au debut), le nom garde le clavier
    for (ui::InputText* f : {p.edProject, p.edDesc, p.edName}) {
        f->takeFocus();
        (void)f->dispatch(ui::KeyDown{ui::Key::Home, {}, false});
    }
}

void AiMemoryDialog::beginAdd() {
    showTab(Tab::Learning);
    editorAdding_ = true;
    fillEditor(nullptr, false);
}

void AiMemoryDialog::beginEdit() {
    const auto* k = selectedKnowledge();
    if (!k || k->builtIn) return;
    showTab(Tab::Learning);
    editorAdding_ = false;
    fillEditor(k, false);
}

void AiMemoryDialog::beginView() {
    const auto* k = selectedKnowledge();
    if (!k) return;
    showTab(Tab::Learning);
    editorAdding_ = false;
    fillEditor(k, true);
}

bool AiMemoryDialog::editorOpen() const noexcept { return learning_ && learning_->editorOn; }

void AiMemoryDialog::closeEditor() {
    if (!learning_) return;
    editorAdding_ = false;
    editorReadOnly_ = false;
    learning_->showEditor(false);
}

bool AiMemoryDialog::saveEditor() {
    if (!learning_ || !learning_->editorOn || editorReadOnly_) return false;
    auto& p = *learning_;
    mem::Knowledge k;
    k.name = p.edName->text();
    k.description = p.edDesc->text();
    k.project = p.edProject->text();
    k.content = p.edContent->text();
    const auto* cat = p.edCat->selectedItem();
    k.category = cat ? cat->value : std::string("metier");
    k.active = p.edActive->isChecked();
    // les espaces autour du nom et du projet ne comptent pas
    const auto trim = [](std::string& s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    };
    trim(k.name);
    trim(k.project);
    if (k.name.empty()) {
        say("Une connaissance a un nom.", true);
        return false;
    }
    const bool ok = editorAdding_ ? addKnowledge(std::move(k)) : updateSelected(k);
    if (ok) closeEditor();
    return ok;
}

// ---- Memoire ----------------------------------------------------------------------------------
std::string AiMemoryDialog::memCatTitle(MemCat c) {
    switch (c) {
        case MemCat::Conversations: return "Conversations";
        case MemCat::Messages: return "Historique des messages";
        case MemCat::Projects: return "Informations par projet";
        case MemCat::Preferences: return "Pr\xC3\xA9" "f\xC3\xA9rences";
        case MemCat::Knowledge: return "Connaissances m\xC3\xA9tier";
        case MemCat::Attachments: return "R\xC3\xA9" "f\xC3\xA9rences aux pi\xC3\xA8" "ces jointes";
        case MemCat::Search: return "R\xC3\xA9sultats de la recherche";
    }
    return {};
}

void AiMemoryDialog::showMemoryCategory(MemCat c) {
    if (c == MemCat::Search && search_.empty()) return;
    memCat_ = c;
    selectedMemory_.clear();
    rebuildTables();
    syncWidgets();
}

void AiMemoryDialog::setSearch(std::string text) {
    search_ = std::move(text);
    hits_ = search_.empty() ? std::vector<mem::Hit>{} : mem::search(search_, conversations_, registry_.all());
    if (!search_.empty()) memCat_ = MemCat::Search;
    else if (memCat_ == MemCat::Search) memCat_ = MemCat::Conversations;
    selectedMemory_.clear();
    rebuildTables();
    syncWidgets();
}

std::size_t AiMemoryDialog::memoryRowCount() const { return memory_ ? memory_->model->rows.size() : 0; }

bool AiMemoryDialog::selectMemoryRow(std::string_view text) {
    if (!memory_) return false;
    const auto& rows = memory_->model->rows;
    const std::string want = folded(text);
    for (std::size_t i = 0; i < rows.size(); ++i)
        for (const auto& cell : rows[i].cells)
            if (folded(cell).find(want) != std::string::npos) {
                selectedMemory_ = rows[i].key;
                selectKey(memory_->table, static_cast<int>(i));
                syncWidgets();
                return true;
            }
    return false;
}

std::string AiMemoryDialog::memoryKeyAt(std::size_t modelRow) const {
    return memory_ && modelRow < memory_->model->rows.size() ? memory_->model->rows[modelRow].key : std::string{};
}

bool AiMemoryDialog::deleteMemorySelection(bool now) {
    std::string key = selectedMemory_;
    if (key.empty()) {
        say("Choisis d'abord une ligne.", true);
        return false;
    }
    if (const auto hash = key.find('#'); hash != std::string::npos) key.erase(hash);
    std::string text;
    if (key.rfind("c:", 0) == 0 || key.rfind("m:", 0) == 0) {
        std::string id = key.substr(2);
        if (key[0] == 'm') id = id.substr(0, id.rfind(':'));
        for (const auto& c : conversations_)
            if (c.id == id)
                text = "Supprimer la conversation \xC2\xAB " + c.title + " \xC2\xBB (" + plural(c.messages.size(), "message", "messages") + ", " + mem::sizeText(c.bytes)
                       + ") ? Ses pi\xC3\xA8" "ces jointes restent.";
        if (text.empty()) return false;
    } else if (key.rfind("p:", 0) == 0) {
        const std::string p = key.substr(2);
        std::size_t convs = 0, know = 0;
        for (const auto& c : conversations_)
            if (c.project == p) ++convs;
        for (const auto& k : registry_.all())
            if (k.project == p && !k.builtIn) ++know;
        text = "Oublier le projet " + p + " : " + plural(know, "connaissance", "connaissances") + " et " + plural(convs, "conversation", "conversations")
               + " supprim\xC3\xA9" "es de ce PC ? Le projet lui-m\xC3\xAAme n'est pas touch\xC3\xA9.";
    } else if (key.rfind("k:", 0) == 0) {
        const auto* k = registry_.find(key.substr(2));
        if (!k || k->builtIn) {
            say("Une connaissance du logiciel se d\xC3\xA9sactive, elle ne se supprime pas.", true);
            return false;
        }
        text = "Supprimer la m\xC3\xA9moire \xC2\xAB " + k->name + " \xC2\xBB (" + mem::sizeText(k->size()) + ") ?";
    } else if (key.rfind("a:", 0) == 0) {
        const std::string id = key.substr(2);
        for (const auto& a : attachments_)
            if (a.id == id)
                text = "Supprimer la pi\xC3\xA8" "ce jointe " + (a.names.empty() ? a.id : a.names) + " (" + mem::sizeText(a.usage.bytes) + ")"
                       + (a.conversation.empty() ? std::string(" ? Aucune conversation ne la cite.") : " ? La conversation \xC2\xAB " + a.conversationTitle + " \xC2\xBB la citera sans pouvoir la rouvrir.");
        if (text.empty()) return false;
    } else {
        return false;
    }
    if (now) return doDeleteMemory();
    ask(Confirm::DeleteMemory, text, "Oui, supprimer");
    return true;
}

bool AiMemoryDialog::doDeleteMemory() {
    std::string key = selectedMemory_;
    if (const auto hash = key.find('#'); hash != std::string::npos) key.erase(hash);
    std::string why, done;
    bool ok = false;
    if (key.rfind("c:", 0) == 0 || key.rfind("m:", 0) == 0) {
        std::string id = key.substr(2);
        if (key[0] == 'm') id = id.substr(0, id.rfind(':'));
        ok = mem::deleteConversation(id, &why);
        done = "Conversation supprim\xC3\xA9" "e.";
    } else if (key.rfind("p:", 0) == 0) {
        const std::string p = key.substr(2);
        const std::size_t know = registry_.removeProject(p);
        std::size_t convs = 0;
        for (const auto& c : conversations_)
            if (c.project == p && mem::deleteConversation(c.id, &why)) ++convs;
        ok = true;
        done = "Projet " + p + " oubli\xC3\xA9 : " + plural(know, "connaissance", "connaissances") + ", " + plural(convs, "conversation", "conversations") + " supprim\xC3\xA9" "es.";
    } else if (key.rfind("k:", 0) == 0) {
        const auto* k = registry_.find(key.substr(2));
        const std::string name = k ? k->name : std::string{};
        ok = registry_.remove(key.substr(2), &why);
        done = "M\xC3\xA9moire supprim\xC3\xA9" "e : " + name;
    } else if (key.rfind("a:", 0) == 0) {
        ok = mem::deleteAttachmentFolder(key.substr(2), &why);
        done = "Pi\xC3\xA8" "ce jointe supprim\xC3\xA9" "e.";
    }
    selectedMemory_.clear();
    refresh();
    if (!ok) {
        say("Suppression impossible : " + why, true);
        return false;
    }
    say(done);
    return true;
}

bool AiMemoryDialog::deleteMemoryCategory(bool now) {
    if (memCat_ == MemCat::Search || memCat_ == MemCat::Projects) {
        say(memCat_ == MemCat::Projects ? "Un projet s'oublie un par un : choisis-le." : "Les r\xC3\xA9sultats d'une recherche ne se suppriment pas d'un coup.", true);
        return false;
    }
    std::string text;
    switch (memCat_) {
        case MemCat::Conversations:
        case MemCat::Messages:
            text = "Supprimer TOUTES les conversations (" + plural(conversations_.size(), "conversation", "conversations") + ", "
                   + plural(messageCount(), "message", "messages") + ", " + mem::sizeText(measure_.of(mem::Part::Conversations).bytes) + ") ? Rien ne pourra \xC3\xAAtre r\xC3\xA9" "cup\xC3\xA9r\xC3\xA9.";
            break;
        case MemCat::Preferences: {
            std::size_t n = 0;
            for (const auto& k : registry_.all())
                if (k.category == "preferences" && !k.builtIn) ++n;
            text = "Supprimer toutes les pr\xC3\xA9" "f\xC3\xA9rences de conception enregistr\xC3\xA9" "es (" + std::to_string(n) + ") ?";
            break;
        }
        case MemCat::Knowledge: {
            std::size_t n = 0;
            for (const auto& k : registry_.all())
                if (!k.builtIn) ++n;
            text = "Supprimer toutes les connaissances ajout\xC3\xA9" "es (" + std::to_string(n) + ", " + mem::sizeText(measure_.of(mem::Part::Knowledge).bytes)
                   + ") ? Celles du logiciel restent.";
            break;
        }
        case MemCat::Attachments:
            text = "Supprimer toutes les pi\xC3\xA8" "ces jointes (" + plural(attachments_.size(), "dossier", "dossiers") + ", "
                   + mem::sizeText(measure_.of(mem::Part::Attachments).bytes) + ") ? Les conversations les citeront sans pouvoir les rouvrir.";
            break;
        default: return false;
    }
    if (now) return doDeleteCategory();
    ask(Confirm::DeleteCategory, text, "Oui, tout supprimer");
    return true;
}

bool AiMemoryDialog::doDeleteCategory() {
    mem::CleanResult r;
    std::string what = memCatTitle(memCat_);
    switch (memCat_) {
        case MemCat::Conversations:
        case MemCat::Messages: r = mem::deletePersistent({mem::Persistent::Conversations}, true); break;
        case MemCat::Knowledge: r = mem::deletePersistent({mem::Persistent::Knowledge}, true); break;
        case MemCat::Attachments: r = mem::deletePersistent({mem::Persistent::Attachments}, true); break;
        case MemCat::Preferences: {
            std::vector<std::string> ids;
            for (const auto& k : registry_.all())
                if (k.category == "preferences" && !k.builtIn) ids.push_back(k.id);
            for (const auto& id : ids) {
                std::string why;
                if (!registry_.remove(id, &why)) {
                    ++r.failures;
                    if (r.firstError.empty()) r.firstError = why;
                }
            }
            r.freed.files = ids.size() - r.failures;
            break;
        }
        default: return false;
    }
    selectedMemory_.clear();
    refresh();
    if (!r.ok()) {
        say("Pas tout supprim\xC3\xA9 : " + r.firstError, true);
        return false;
    }
    say(what + " : supprim\xC3\xA9" "es (" + plural(r.freed.files, "fichier", "fichiers") + ", " + mem::sizeText(r.freed.bytes) + ").");
    return true;
}

// ---- Stockage PC ------------------------------------------------------------------------------
std::string AiMemoryDialog::location() const { return measure_.root; }

void AiMemoryDialog::copyLocation() {
    if (measure_.root.empty()) return say("Aucun dossier IA n'est fix\xC3\xA9.", true);
    ui::setClipboardText(measure_.root);
    say("Emplacement copi\xC3\xA9 : " + measure_.root);
}

bool AiMemoryDialog::openFolder() {
    if (measure_.root.empty()) {
        say("Aucun dossier IA n'est fix\xC3\xA9.", true);
        return false;
    }
    // Le dossier n'existe pas encore (rien d'enregistre) : il est cree, vide, pour s'ouvrir.
    std::error_code ec;
    std::filesystem::create_directories(dossiers::cheminDe(measure_.root), ec);
    const std::string why = spec_.openFolder ? spec_.openFolder(measure_.root) : dossiers::ouvrirDansLeSysteme(measure_.root);
    if (!why.empty()) {
        say(why, true);
        return false;
    }
    say("Dossier ouvert dans l'explorateur : " + measure_.root);
    return true;
}

// ---- Nettoyage --------------------------------------------------------------------------------
void AiMemoryDialog::setCacheChecked(mem::Part p, bool on) {
    if (!mem::partInfo(p).cache) return;
    cacheOn_.erase(std::remove(cacheOn_.begin(), cacheOn_.end(), p), cacheOn_.end());
    if (on) cacheOn_.push_back(p);
    syncWidgets();
}

bool AiMemoryDialog::cacheChecked(mem::Part p) const { return std::find(cacheOn_.begin(), cacheOn_.end(), p) != cacheOn_.end(); }

mem::Usage AiMemoryDialog::cacheSelection() const {
    mem::Usage u;
    for (const auto p : cacheOn_) u += measure_.of(p);
    return u;
}

bool AiMemoryDialog::clearCache(bool now) {
    if (cacheOn_.empty()) {
        say("Coche au moins une cat\xC3\xA9gorie du cache.", true);
        return false;
    }
    const auto u = cacheSelection();
    if (now) return doClearCache();
    std::string names;
    std::size_t n = 0;
    for (const auto p : cacheOn_)
        if (measure_.of(p).files || p != mem::Part::CacheOther) ++n;
    names = plural(n, "cat\xC3\xA9gorie", "cat\xC3\xA9gories");
    ask(Confirm::ClearCache,
        "Vider " + names + " du cache : " + mem::sizeText(u.bytes) + " (" + plural(u.files, "fichier", "fichiers")
            + ") seront lib\xC3\xA9r\xC3\xA9s. Les conversations, connaissances, pi\xC3\xA8" "ces jointes, projets et param\xC3\xA8tres ne sont pas touch\xC3\xA9s.",
        "Oui, vider");
    return true;
}

bool AiMemoryDialog::doClearCache() {
    const auto r = mem::clearCache(cacheOn_);
    refresh();
    if (!r.ok()) {
        say("Cache vid\xC3\xA9 en partie (" + mem::sizeText(r.freed.bytes) + " lib\xC3\xA9r\xC3\xA9s) : " + r.firstError, true);
        return false;
    }
    say("Cache vid\xC3\xA9 : " + mem::sizeText(r.freed.bytes) + " lib\xC3\xA9r\xC3\xA9s (" + plural(r.freed.files, "fichier", "fichiers") + ").");
    return true;
}

bool AiMemoryDialog::persistentEmpty(mem::Persistent p) const {
    const auto plan = mem::persistentPlan(measure_);
    const auto i = static_cast<std::size_t>(p);
    return i >= plan.size() || plan[i].usage.files == 0;
}

void AiMemoryDialog::setPersistentChecked(mem::Persistent p, bool on) {
    persistentOn_.erase(std::remove(persistentOn_.begin(), persistentOn_.end(), p), persistentOn_.end());
    if (on && !persistentEmpty(p)) persistentOn_.push_back(p);   // vide : rien a supprimer, elle ne se coche pas
    syncWidgets();
}

void AiMemoryDialog::setUnderstood(bool on) {
    understood_ = on;
    syncWidgets();
}

bool AiMemoryDialog::deletePersistent(bool now) {
    if (!understood_) {
        say("Coche d'abord \xC2\xAB Je comprends \xC2\xBB : ces donn\xC3\xA9" "es ne se r\xC3\xA9" "cup\xC3\xA8rent pas.", true);
        return false;
    }
    // une categorie devenue vide depuis (supprimee ailleurs) ne compte plus
    persistentOn_.erase(std::remove_if(persistentOn_.begin(), persistentOn_.end(), [&](mem::Persistent p) { return persistentEmpty(p); }),
                        persistentOn_.end());
    if (persistentOn_.empty()) {
        say("Coche les donn\xC3\xA9" "es \xC3\xA0 supprimer (une cat\xC3\xA9gorie vide n'a rien \xC3\xA0 supprimer).", true);
        syncWidgets();
        return false;
    }
    if (now) return doDeletePersistent();
    std::string list;
    mem::Usage total;
    for (const auto& line : mem::persistentPlan(measure_))
        if (std::find(persistentOn_.begin(), persistentOn_.end(), line.what) != persistentOn_.end()) {
            list += (list.empty() ? "" : ", ") + line.label + " (" + mem::sizeText(line.usage.bytes) + ")";
            total += line.usage;
        }
    ask(Confirm::DeletePersistent,
        "Supprimer d\xC3\xA9" "finitivement : " + list + " \xE2\x80\x94 " + plural(total.files, "fichier", "fichiers") + ". Rien ne pourra \xC3\xAAtre r\xC3\xA9" "cup\xC3\xA9r\xC3\xA9.",
        "Oui, supprimer");
    return true;
}

bool AiMemoryDialog::doDeletePersistent() {
    const auto r = mem::deletePersistent(persistentOn_, understood_);
    persistentOn_.clear();
    understood_ = false;
    refresh();
    if (!r.ok()) {
        say("Pas tout supprim\xC3\xA9 : " + r.firstError, true);
        return false;
    }
    say("Donn\xC3\xA9" "es supprim\xC3\xA9" "es : " + mem::sizeText(r.freed.bytes) + " (" + plural(r.freed.files, "fichier", "fichiers") + ").");
    return true;
}

// ---- Activite ---------------------------------------------------------------------------------
void AiMemoryDialog::setActivityFilter(std::string type) {
    activityFilter_ = std::move(type);
    rebuildTables();
    syncWidgets();
}

std::size_t AiMemoryDialog::activityRowCount() const { return activityPage_ ? activityPage_->model->rows.size() : 0; }

// ---- la confirmation, le message ----------------------------------------------------------------
void AiMemoryDialog::ask(Confirm kind, std::string text, std::string yes) {
    confirm_ = {kind, std::move(text), std::move(yes)};
    message_.clear();
    syncWidgets();
    if (body_) {
        body_->invalidateLayout();
        body_->invalidate();
    }
}

bool AiMemoryDialog::confirmYes() {
    const Pending p = confirm_;
    confirm_ = {};
    bool ok = false;
    switch (p.kind) {
        case Confirm::DeleteKnowledge: ok = doDeleteKnowledge(); break;
        case Confirm::DeleteMemory: ok = doDeleteMemory(); break;
        case Confirm::DeleteCategory: ok = doDeleteCategory(); break;
        case Confirm::ClearCache: ok = doClearCache(); break;
        case Confirm::DeletePersistent: ok = doDeletePersistent(); break;
        case Confirm::None: break;
    }
    syncWidgets();
    if (body_) body_->invalidateLayout();
    return ok;
}

void AiMemoryDialog::confirmNo() {
    const bool cache = confirm_.kind == Confirm::ClearCache;
    confirm_ = {};
    say(cache ? "Le cache n'a pas \xC3\xA9t\xC3\xA9 vid\xC3\xA9." : "Rien n'a \xC3\xA9t\xC3\xA9 supprim\xC3\xA9.");
    syncWidgets();
    if (body_) body_->invalidateLayout();
}

void AiMemoryDialog::say(std::string text, bool bad) {
    message_ = std::move(text);
    messageBad_ = bad;
    if (body_) body_->invalidate();
}

ui::TableView* AiMemoryDialog::currentTable() const {
    switch (tab()) {
        case Tab::Learning: return learning_ && !learning_->editorOn ? learning_->table : nullptr;
        case Tab::Memory: return memory_ ? memory_->table : nullptr;
        case Tab::Storage: return storage_ ? storage_->table : nullptr;
        case Tab::Activity: return activityPage_ ? activityPage_->table : nullptr;
        default: return nullptr;
    }
}

bool AiMemoryDialog::selectRow(std::string_view text) {
    switch (tab()) {
        case Tab::Learning: return selectKnowledge(text);
        case Tab::Memory: return selectMemoryRow(text);
        default: break;
    }
    auto* t = currentTable();
    if (!t || !t->model()) return false;
    const std::string want = folded(text);
    for (std::size_t i = 0; i < t->visibleRowCount(); ++i)
        for (std::size_t c = 0; c < t->model()->columnCount(); ++c)
            if (folded(t->model()->cellText(t->viewRow(i), c)).find(want) != std::string::npos) {
                t->selectModelRows({t->viewRow(i)}, true);
                return true;
            }
    return false;
}

void AiMemoryDialog::finish() {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, std::string{}});
}

// ================================================================================== l'ecran ===
core::Status AiMemoryDialog::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    const std::string base = spec_.id;
    auto tabs = std::make_unique<ui::TabControl>(base + ".onglets");
    {
        auto p = std::make_unique<OverviewPage>(*this);
        overview_ = p.get();
        tabs->addTab(ui::TabControl::Tab{tabTitle(Tab::Overview), ui::Icon::Chart}, std::move(p));
    }
    {
        auto p = std::make_unique<LearningPage>(*this);
        learning_ = p.get();
        tabs->addTab(ui::TabControl::Tab{tabTitle(Tab::Learning), ui::Icon::Library}, std::move(p));
    }
    {
        auto p = std::make_unique<MemoryPage>(*this);
        memory_ = p.get();
        tabs->addTab(ui::TabControl::Tab{tabTitle(Tab::Memory), ui::Icon::Document}, std::move(p));
    }
    {
        auto p = std::make_unique<StoragePage>(*this);
        storage_ = p.get();
        tabs->addTab(ui::TabControl::Tab{tabTitle(Tab::Storage), ui::Icon::Folder}, std::move(p));
    }
    {
        auto p = std::make_unique<CleaningPage>(*this);
        cleaning_ = p.get();
        tabs->addTab(ui::TabControl::Tab{tabTitle(Tab::Cleaning), ui::Icon::Clear}, std::move(p));
    }
    {
        auto p = std::make_unique<ActivityPage>(*this);
        activityPage_ = p.get();
        tabs->addTab(ui::TabControl::Tab{tabTitle(Tab::Activity), ui::Icon::History}, std::move(p));
    }
    tabs_ = &static_cast<ui::TabControl&>(body->addChild(std::move(tabs)));
    links_ += tabs_->currentChanged->connect([this](std::size_t) {
        if (tab() == Tab::Overview) refreshRam();
        if (body_) body_->invalidate();
    });
    refreshButton_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Actualiser", base + ".actualiser")));
    refreshButton_->setIcon(ui::Icon::Refresh);
    links_ += refreshButton_->clicked->connect([this] {
        refresh();
        say("Mesures refaites sur les fichiers de " + (measure_.root.empty() ? std::string("IA/") : measure_.root) + ".");
    });
    close_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Fermer", base + ".fermer")));
    close_->setStyle(ui::Button::Style::Primary);
    links_ += close_->clicked->connect([this] { finish(); });
    yes_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Oui, supprimer", base + ".oui")));
    yes_->setStyle(ui::Button::Style::Danger);
    links_ += yes_->clicked->connect([this] { (void)confirmYes(); });
    no_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Non", base + ".non")));
    links_ += no_->clicked->connect([this] { confirmNo(); });

    // ---- Apprentissage
    {
        auto& p = *learning_;
        links_ += p.catBox->selectionChanged->connect([this](int) {
            if (syncing_) return;
            const auto* it = learning_->catBox->selectedItem();
            setCategoryFilter(it ? it->value : std::string{});
        });
        links_ += p.projBox->selectionChanged->connect([this](int) {
            if (syncing_) return;
            const auto* it = learning_->projBox->selectedItem();
            setProjectFilter(it ? it->value : std::string{});
        });
        links_ += p.table->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
            if (syncing_) return;
            const auto& m = *learning_->model;
            selectedKnowledge_ = !rows.empty() && rows.front() < m.rows.size() ? m.rows[rows.front()].key : std::string{};
            syncWidgets();
            learning_->invalidate();
        });
        links_ += p.table->activated->connect([this](ui::RowIndex) { beginView(); });
        links_ += p.add->clicked->connect([this] { beginAdd(); });
        links_ += p.view->clicked->connect([this] { beginView(); });
        links_ += p.edit->clicked->connect([this] { beginEdit(); });
        links_ += p.toggle->clicked->connect([this] { (void)toggleSelected(); });
        links_ += p.remove->clicked->connect([this] { (void)deleteSelected(); });
        links_ += p.edSave->clicked->connect([this] { (void)saveEditor(); });
        links_ += p.edCancel->clicked->connect([this] { closeEditor(); });
        links_ += p.edToEdit->clicked->connect([this] { beginEdit(); });
    }
    // ---- Memoire
    {
        auto& p = *memory_;
        links_ += p.search->textChanged->connect([this](const std::string& t) {
            if (syncing_ || t == search_) return;
            setSearch(t);
        });
        links_ += p.table->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
            if (syncing_) return;
            selectedMemory_ = rows.empty() ? std::string{} : memoryKeyAt(rows.front());
            syncWidgets();
        });
        links_ += p.removeOne->clicked->connect([this] { (void)deleteMemorySelection(); });
        links_ += p.removeAll->clicked->connect([this] { (void)deleteMemoryCategory(); });
    }
    // ---- Stockage PC
    {
        links_ += storage_->copy->clicked->connect([this] { copyLocation(); });
        links_ += storage_->open->clicked->connect([this] { (void)openFolder(); });
    }
    // ---- Nettoyage
    {
        for (auto& [part, box] : cleaning_->cache) {
            const mem::Part pp = part;
            links_ += box->stateChanged->connect([this, pp](ui::Checkbox::State s) {
                if (!syncing_) setCacheChecked(pp, s == ui::Checkbox::State::Checked);
            });
        }
        for (auto& [what, box] : cleaning_->persistent) {
            const mem::Persistent pw = what;
            links_ += box->stateChanged->connect([this, pw](ui::Checkbox::State s) {
                if (!syncing_) setPersistentChecked(pw, s == ui::Checkbox::State::Checked);
            });
        }
        links_ += cleaning_->understood->stateChanged->connect([this](ui::Checkbox::State s) {
            if (!syncing_) setUnderstood(s == ui::Checkbox::State::Checked);
        });
        links_ += cleaning_->clear->clicked->connect([this] { (void)clearCache(); });
        links_ += cleaning_->erase->clicked->connect([this] { (void)deletePersistent(); });
    }
    // ---- Activite
    links_ += activityPage_->typeBox->selectionChanged->connect([this](int) {
        if (syncing_) return;
        const auto* it = activityPage_->typeBox->selectedItem();
        setActivityFilter(it ? it->value : std::string{});
    });

    setRoot(std::move(body));
    rebuildTables();
    syncWidgets();
    return core::ok();
}

ui::EventResult AiMemoryDialog::HandleEvent(const ui::InputEvent& ev) {
    const auto r = menu::WidgetMenu::HandleEvent(ev);
    if (r == ui::EventResult::Consumed) return r;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            if (confirming()) confirmNo();
            else if (editorOpen()) closeEditor();
            else finish();
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return && confirming()) {
            (void)confirmYes();
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::F5) {
            refresh();
            return ui::EventResult::Consumed;
        }
    }
    return r;
}

} // namespace app
