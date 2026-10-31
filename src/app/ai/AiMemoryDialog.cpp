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
#include "AiLearning.hpp"
#include "AiLearningFile.hpp"
#include "AiPaths.hpp"
#include "AiStore.hpp"
#include "../../hmi/HmiModel.hpp"
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
#include <ctime>
#include <filesystem>
#include <iterator>
#include <set>

namespace app {

namespace mem = ai::memory;
namespace ln = ai::learning;

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
            if (mem::hereState(k, d_.here()) == mem::Here::Active) ++here;
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
                        mem::sizeText(reg.bytesHere(d_.here())) + " par demande"});
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
        textRight(ctx, x + w, b.y + 11.f, mem::sizeText(d_.registry_.bytesHere(d_.here())) + " en tout", kSmall, c.text);
        float y = b.y + 36.f;
        std::size_t shown = 0, total = 0;
        for (const auto& k : d_.registry_.all()) {
            if (mem::hereState(k, d_.here()) != mem::Here::Active) continue;
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
    bool               embedded{false};   // 1.17.0 : dans le tableau de bord (son theme tient lieu de categorie)

protected:
    void onLayout() override {
        const auto r = bounds();
        const float x0 = r.x + kPad, W = r.w - 2.f * kPad;
        info_ = embedded ? gfx::Rect{x0, r.y, W, 0.f} : gfx::Rect{x0, r.y + 10.f, W, 34.f};
        const float fy = embedded ? r.y + 2.f : info_.bottom() + 8.f;
        catLabelX_ = x0;
        const float catLw = ui::measureWidth("Cat\xC3\xA9gorie", kSmall) + 8.f;
        catBox->setVisibility(embedded ? ui::Visibility::Collapsed : ui::Visibility::Visible);
        catBox->setBounds({x0 + catLw, fy, 230.f, 30.f});
        projLabelX_ = embedded ? x0 : x0 + catLw + 230.f + 18.f;
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
        if (embedded) {
            // dans le tableau de bord : la liste a gauche, le detail a droite (la place est en largeur)
            const float tw = std::floor(W * 0.56f);
            table->setBounds({x0, top, tw, avail});
            detail_ = {x0 + tw + kGap, top, W - tw - kGap, avail};
            recent_ = {};
        } else {
        const float tableH = std::max(140.f, std::floor(avail * 0.48f));
        table->setBounds({x0, top, W, tableH});
        const float by = top + tableH + 10.f;
        const float bh = r.bottom() - 10.f - by;
        detail_ = {x0, by, std::floor(W * 0.6f), bh};
        recent_ = {detail_.right() + kGap, by, x0 + W - detail_.right() - kGap, bh};
        }
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
        // l'encart franc (dans le tableau de bord : son bandeau le dit deja)
        if (!embedded) {
        ctx.r.fillRoundedRect(info_, c.accent.withAlpha(ctx.theme.isDark() ? 34 : 22), 6.f);
        ctx.r.fillRoundedRect({info_.x, info_.y, 3.f, info_.h}, c.accent, 1.5f);
        ui::drawIcon(ctx.r, ui::Icon::Info, {info_.x + 12.f, info_.y + 10.f, 14.f, 14.f}, c.accent);
        const std::string lead = "Apprendre ici = enrichir la m\xC3\xA9moire locale que l'Assistant joint \xC3\xA0 ses demandes.";
        drawBold(ctx, {info_.x + 34.f, info_.y + (info_.h - ctx.r.lineHeight(kText)) * 0.5f}, fit(ctx, lead, kText, info_.w - 44.f), kText, c.text);
        const float lx = info_.x + 34.f + textW(ctx, lead, kText) + 8.f;
        if (lx < info_.right() - 40.f)
            textMid(ctx, lx, info_, fit(ctx, "Le mod\xC3\xA8le distant n'est pas r\xC3\xA9" "entra\xC3\xAEn\xC3\xA9.", kText, info_.right() - 10.f - lx), kText, c.text);
        }
        // les filtres
        const gfx::Rect fb{0.f, filtersY_, 0.f, 30.f};
        if (!embedded) textMid(ctx, catLabelX_, fb, "Cat\xC3\xA9gorie", kSmall, c.textMuted);
        textMid(ctx, projLabelX_, fb, "Projet", kSmall, c.textMuted);
        {
            std::size_t shown = d_.shownKnowledge().size(), here = 0;
            for (const auto* k : d_.shownKnowledge())
                if (mem::hereState(*k, d_.here()) == mem::Here::Active) ++here;
            drawFit(ctx, {countX_, fb.y + (fb.h - ctx.r.lineHeight(kSmall)) * 0.5f},
                    plural(shown, "connaissance", "connaissances") + kDot + std::to_string(here) + " active(s) ici", kSmall, countW_, c.textMuted);
        }
        if (editorOn) {
            paintEditor(ctx);
            return;
        }
        paintDetail(ctx);
        if (!embedded) paintRecent(ctx);
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
        const auto here = mem::hereState(*k, d_.here());
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

// ============================================================ Apprentissage (1.17.0) : le tableau de bord ===
//  CE QU'ELLE SAIT, ET QU'ELLE PROGRESSE. En haut : le bandeau honnete (la memoire locale ; le modele distant n'est
//  pas reentraine), Importer... et Exporter l'apprentissage... ; quatre tuiles ; deux graphiques mesures sur ce PC
//  (les demandes comprises et les propositions gardees, par semaine ; ce qu'elle a appris, par origine). En bas : les
//  THEMES a gauche, la liste du theme au centre (Rechercher, filtres, tri), le detail modifiable a droite ; en pied,
//  << Essayer une phrase >>. Les connaissances en texte gardent leur page (LearningPage, dans ce cadre).
namespace {
// Les couleurs des series (validees) : clair / sombre.
gfx::Color seriesColor(const ui::PaintContext& ctx, int i) {
    static const std::uint32_t light[] = {0x2a78d6, 0xeb6834, 0x1baf7a};
    static const std::uint32_t dark[] = {0x3987e5, 0xd95926, 0x199e70};
    return gfx::Color::rgb(ctx.theme.isDark() ? dark[i % 3] : light[i % 3]);
}
std::string pct(int part, int whole) {
    if (whole <= 0) return "\xE2\x80\x94";
    return std::to_string(static_cast<int>(std::lround(100.0 * part / whole))) + " %";
}
std::string weekLabel(long long start) {
    std::time_t tt = static_cast<std::time_t>(start);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d/%02d", tm.tm_mday, tm.tm_mon + 1);
    return buf;
}
const char* const kTypeChoices[] = {"BOOL", "INT", "DINT", "UINT", "UDINT", "REAL", "LREAL", "STRING", "TIME", "WORD", "DWORD"};

// La table des membres d'un modele : modifiable (Nom, Type, Description).
class MembersModel final : public ui::ITableModel {
public:
    std::vector<ln::ModelMember> rows;
    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 3; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        return c == 0 ? "Membre" : c == 1 ? "Type" : "Description";
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows.size()) return {};
        return c == 0 ? rows[r].name : c == 1 ? rows[r].type : rows[r].description;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return folded(cellText(a, c)) < folded(cellText(b, c)); }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t) const override { return r < rows.size(); }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override {
        if (r >= rows.size()) return false;
        std::string t(text);
        if (c == 0) rows[r].name = t;
        else if (c == 1) {
            std::string why;
            if (!ln::validMemberType(t, &why)) return false;
            rows[r].type = t;
        } else rows[r].description = t;
        changed->emit();
        return true;
    }
    [[nodiscard]] std::vector<std::string> cellChoices(ui::RowIndex, std::size_t c) const override {
        if (c != 1) return {};
        return std::vector<std::string>(std::begin(kTypeChoices), std::end(kTypeChoices));
    }
    void reset() { modelReset->emit(); }
    const core::SignalPtr<> changed = core::Signal<>::create();
};
} // namespace

class AiMemoryDialog::LearningHub final : public ui::Widget {
public:
    explicit LearningHub(AiMemoryDialog& d) : ui::Widget("ia.memoire.apprentissage.tableau"), d_(d) {
        const std::string base = "ia.memoire.apprentissage.";
        importB = &button(base + "importer", "Importer\xE2\x80\xA6", ui::Icon::Import);
        exportB = &button(base + "exporter", "Exporter l'apprentissage\xE2\x80\xA6", ui::Icon::Export);
        period = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "periode")));
        period->setItems({{"8 semaines", "8"}, {"3 mois", "13"}, {"6 mois", "26"}});
        period->setSelectedIndex(0);
        search = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + "rechercher")));
        search->setPlaceholder("Rechercher : un nom, un mot, un membre, un type\xE2\x80\xA6");
        stateBox = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "etat")));
        stateBox->setItems({{"Actifs et d\xC3\xA9sactiv\xC3\xA9s", ""}, {"Actifs", "actifs"}, {"D\xC3\xA9sactiv\xC3\xA9s", "inactifs"},
                            {"Modifi\xC3\xA9s \xC3\xA0 la main", "main"}});
        stateBox->setSelectedIndex(0);
        sortBox = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "tri")));
        sortBox->setItems({{"Trier : r\xC3\xA9" "cents", "recents"}, {"Trier : nom", "nom"}});
        sortBox->setSelectedIndex(0);
        listModel = std::make_shared<Rows>();
        list = &makeTable(*this, base + "theme", listModel, {});
        // le detail d'un modele
        mName = &input(base + "modele.nom", "T_Modele");
        mWords = &input(base + "modele.mots", "les mots qui l'appellent, s\xC3\xA9par\xC3\xA9s par des virgules");
        members = std::make_shared<MembersModel>();
        mMembers = &makeTable(*this, base + "modele.membres", members,
                              {col("Membre", 120.f), col("Type", 90.f), col("Description", 200.f)});
        mMembers->setSelectionMode(ui::SelectionMode::Single);
        mAdd = &button(base + "modele.ajouterMembre", "+ Membre", ui::Icon::Plus);
        mRemove = &button(base + "modele.retirerMembre", "Retirer", ui::Icon::Delete);
        mUp = &button(base + "modele.monter", "", ui::Icon::ArrowUp);
        mUp->setTooltip("Monter le membre choisi");
        mDown = &button(base + "modele.descendre", "", ui::Icon::ArrowDown);
        mDown->setTooltip("Descendre le membre choisi");
        // le detail d'un mot
        wWord = &input(base + "mot.mot", "le mot (b\xC3\xA2" "che, surpresseur\xE2\x80\xA6)");
        wMeaning = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "mot.sens")));
        wMeaning->setItems({{"un genre d'objet", "genre"}, {"un mod\xC3\xA8le m\xC3\xA9tier", "modele"}, {"un mot \xC3\xA0 ignorer", "ignorer"}});
        wMeaning->setSelectedIndex(0);
        wTarget = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "mot.cible")));
        wTarget->setMaxVisibleRows(14);
        // les boutons communs du detail
        save = &button(base + "enregistrer", "Enregistrer", ui::Icon::None);
        save->setStyle(ui::Button::Style::Primary);
        revert = &button(base + "original", "Revenir \xC3\xA0 l'original", ui::Icon::Undo);
        toggle = &button(base + "basculer", "D\xC3\xA9sactiver", ui::Icon::None);
        remove = &button(base + "supprimer", "Supprimer", ui::Icon::Delete);
        remove->setStyle(ui::Button::Style::Danger);
        addNew = &button(base + "nouveau", "Nouveau mod\xC3\xA8le\xE2\x80\xA6", ui::Icon::Plus);
        // A lui apprendre
        teachLearn = &button(base + "apprendre", "Lui apprendre", ui::Icon::Library);
        teachIgnore = &button(base + "ignorer", "Ignorer", ui::Icon::None);
        teachWord = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "apprendre.mot")));
        // essayer une phrase
        tryIn = &input(base + "essayer", "Essayer une phrase : \xC2\xAB Cr\xC3\xA9" "e un type pour g\xC3\xA9rer les capteurs \xC2\xBB\xE2\x80\xA6");
        tryB = &button(base + "essayerBouton", "Essayer", ui::Icon::Play);
        // la page des connaissances en texte (les autres themes), dans ce cadre
        auto page = std::make_unique<LearningPage>(d);
        page->embedded = true;
        knowledge = &static_cast<LearningPage&>(addChild(std::move(page)));

        links_ += importB->clicked->connect([this] { askImport(); });
        links_ += exportB->clicked->connect([this] { askExport(); });
        links_ += period->selectionChanged->connect([this](int) { refreshData(); });
        links_ += search->textChanged->connect([this](const std::string& t) {
            d_.setLearningSearchQuiet(t);   // les themes en texte : leurs connaissances aussi
            fillList();
        });
        links_ += stateBox->selectionChanged->connect([this](int) { fillList(); });
        links_ += sortBox->selectionChanged->connect([this](int) { fillList(); });
        links_ += list->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
            if (syncing_) return;
            selected = rows.empty() || rows.front() >= listModel->rows.size() ? std::string{} : listModel->rows[rows.front()].key;
            fillDetail();
        });
        links_ += wMeaning->selectionChanged->connect([this](int) { fillTargets(); });
        links_ += save->clicked->connect([this] { (void)saveDetail(); });
        links_ += revert->clicked->connect([this] { (void)revertDetail(); });
        links_ += toggle->clicked->connect([this] { (void)toggleDetail(); });
        links_ += remove->clicked->connect([this] { (void)removeDetail(); });
        links_ += addNew->clicked->connect([this] { beginNew(); });
        links_ += mAdd->clicked->connect([this] {
            members->rows.push_back({"Membre" + std::to_string(members->rows.size() + 1), "BOOL", {}});
            members->reset();
            mMembers->selectModelRows({static_cast<ui::RowIndex>(members->rows.size() - 1)});
        });
        links_ += mRemove->clicked->connect([this] {
            const auto r = mMembers->selectedModelRows();
            if (r.empty() || r.front() >= members->rows.size()) return;
            members->rows.erase(members->rows.begin() + static_cast<std::ptrdiff_t>(r.front()));
            members->reset();
        });
        const auto move = [this](int delta) {
            const auto r = mMembers->selectedModelRows();
            if (r.empty()) return;
            const int i = static_cast<int>(r.front()), j = i + delta;
            if (j < 0 || j >= static_cast<int>(members->rows.size())) return;
            std::swap(members->rows[static_cast<std::size_t>(i)], members->rows[static_cast<std::size_t>(j)]);
            members->reset();
            mMembers->selectModelRows({static_cast<ui::RowIndex>(j)});
        };
        links_ += mUp->clicked->connect([move] { move(-1); });
        links_ += mDown->clicked->connect([move] { move(1); });
        links_ += teachLearn->clicked->connect([this] { (void)teachSelected(); });
        links_ += teachIgnore->clicked->connect([this] { (void)ignoreSelected(); });
        links_ += tryB->clicked->connect([this] { (void)tryNow(tryIn->text()); });
        links_ += tryIn->editingDone->connect([this](const std::string& t) { (void)tryNow(t); });
        refreshData();
    }

    // ---- les donnees du moment (mesurees sur ce PC) ----
    void refreshData() {
        allModels = ln::models(true);
        allWords = ln::words(true);
        measures = ln::readMeasures();
        decided = ln::decisions();
        const long long nowT = static_cast<long long>(ai::now());
        const int n = period && period->selectedItem() ? std::max(4, std::atoi(period->selectedItem()->value.c_str())) : 8;
        weeks = ln::weeks(n, nowT, measures, decided);
        toTeach = ln::notUnderstood(7, nowT);
        // les taux sur 30 jours, et les 30 d'avant
        const auto rate = [&](long long from, long long to, bool understood) {
            int a = 0, b = 0;
            if (understood) {
                for (const auto& m : measures)
                    if (const long long t = static_cast<long long>(ai::parseIso(m.date)); t >= from && t < to) {
                        ++b;
                        if (m.understood) ++a;
                    }
            } else {
                for (const auto& x : decided)
                    if (const long long t = static_cast<long long>(ai::parseIso(x.date)); t >= from && t < to) {
                        ++b;
                        if (x.kept) ++a;
                    }
            }
            return std::pair<int, int>{a, b};
        };
        const long long d30 = 30LL * 86400;
        nowUnderstood = rate(nowT - d30, nowT + 1, true);
        beforeUnderstood = rate(nowT - 2 * d30, nowT - d30, true);
        nowKept = rate(nowT - d30, nowT + 1, false);
        beforeKept = rate(nowT - 2 * d30, nowT - d30, false);
        // le theme : sa liste, son detail
        fillTargets();
        fillList();
        invalidate();
    }

    // Les themes : (cle, libelle, nombre, appris ces 4 semaines).
    struct ThemeLine {
        std::string key, label;
        std::size_t count{0}, recent{0};
        ui::Icon    icon{ui::Icon::Library};
    };
    [[nodiscard]] std::vector<ThemeLine> themes() const {
        std::vector<ThemeLine> out;
        const long long nowT = static_cast<long long>(ai::now());
        const auto recentDate = [&](const std::string& iso) {
            const long long t = static_cast<long long>(ai::parseIso(iso));
            return t > 0 && nowT - t < 28LL * 86400;
        };
        const std::string q = folded(search ? search->text() : std::string{});
        ThemeLine m{"modeles", "Mod\xC3\xA8les m\xC3\xA9tier", 0, 0, ui::Icon::DerivedType};
        for (const auto& x : allModels)
            if (q.empty() || modelMatches(x, q)) {
                ++m.count;
                if (!x.builtIn || x.corrected) m.recent += recentDate(x.modified) ? 1 : 0;
            }
        out.push_back(m);
        ThemeLine w{"vocabulaire", "Vocabulaire et synonymes", 0, 0, ui::Icon::Comment};
        for (const auto& x : allWords)
            if (q.empty() || wordMatches(x, q)) {
                ++w.count;
                if (!x.builtIn) w.recent += recentDate(x.modified) ? 1 : 0;
            }
        out.push_back(w);
        for (const auto& c : mem::kCategories) {
            if (c.key == "modeles" || c.key == "vocabulaire") continue;
            ThemeLine t{std::string(c.key), std::string(c.label), 0, 0, ui::Icon::Document};
            for (const auto& k : d_.registry_.all())
                if (k.category == c.key && (q.empty() || folded(k.name + " " + k.content).find(q) != std::string::npos)) {
                    ++t.count;
                    if (!k.builtIn && recentDate(k.modified.empty() ? k.added : k.modified)) ++t.recent;
                }
            out.push_back(t);
        }
        out.push_back({"apprendre", "\xC3\x80 lui apprendre", toTeach.size(), 0, ui::Icon::Warning});
        return out;
    }
    [[nodiscard]] static bool modelMatches(const ln::BusinessModel& m, const std::string& q) {
        std::string all = m.key + " " + m.name + " " + m.label + " " + m.description;
        for (const auto& w : m.words) all += " " + w;
        for (const auto& x : m.members) all += " " + x.name + " " + x.type + " " + x.description;
        return folded(all).find(q) != std::string::npos;
    }
    [[nodiscard]] static bool wordMatches(const ln::WordEntry& w, const std::string& q) {
        return folded(w.word + " " + w.target + " " + ln::describe(w)).find(q) != std::string::npos;
    }

    bool showTheme(const std::string& key) {
        const auto all = themes();
        if (std::none_of(all.begin(), all.end(), [&](const ThemeLine& t) { return t.key == key; })) return false;
        theme = key;
        selected.clear();
        adding_ = false;
        if (key != "modeles" && key != "vocabulaire" && key != "apprendre") d_.setCategoryFilter(key);
        fillList();
        invalidateLayout();
        invalidate();
        return true;
    }
    [[nodiscard]] bool textTheme() const { return theme != "modeles" && theme != "vocabulaire" && theme != "apprendre"; }

    // ---- la liste du theme ----
    void fillList() {
        if (!listModel) return;
        const std::string q = folded(search ? search->text() : std::string{});
        const std::string state = stateBox && stateBox->selectedItem() ? stateBox->selectedItem()->value : std::string{};
        const bool byName = sortBox && sortBox->selectedItem() && sortBox->selectedItem()->value == "nom";
        auto& m = *listModel;
        m.rows.clear();
        const auto passState = [&](bool active, bool hand) {
            return state.empty() || (state == "actifs" && active) || (state == "inactifs" && !active) || (state == "main" && hand);
        };
        const auto stateCell = [](bool active) { return active ? std::string("\xE2\x9C\x93 Actif") : std::string("D\xC3\xA9sactiv\xC3\xA9"); };
        const auto origin = [](const std::string& source, bool builtIn, bool corrected) {
            if (builtIn) return corrected ? std::string("Logiciel, corrig\xC3\xA9 par toi") : std::string("Logiciel");
            return source == "assistant" ? std::string("Retenu par l'Assistant") : source == "fichier" ? std::string("Lu dans un fichier")
                                                                                                    : std::string("Toi");
        };
        if (theme == "modeles") {
            m.headers = {"Mod\xC3\xA8le", "Ce qu'elle en sait", "D'o\xC3\xB9 \xC3\xA7" "a vient", "Mis \xC3\xA0 jour", "\xC3\x89tat"};
            for (const auto& x : allModels) {
                if (!q.empty() && !modelMatches(x, q)) continue;
                if (!passState(x.active, !x.builtIn || x.corrected)) continue;
                std::string know;
                for (std::size_t i = 0; i < x.members.size(); ++i) know += (i ? kDot : std::string{}) + x.members[i].name + " " + x.members[i].type;
                Rows::Row r;
                r.key = x.key;
                r.cells = {x.name, know, origin(x.source, x.builtIn, x.corrected), x.modified.empty() ? std::string("int\xC3\xA9gr\xC3\xA9") : ai::shortDate(x.modified),
                           stateCell(x.active)};
                r.styles.resize(5);
                r.styles[0].bold = true;
                if (!x.active)
                    for (auto& s : r.styles) s.fgTone = ui::Tone::Muted;
                r.tip = ln::describe(x);
                r.sort = {std::nan(""), std::nan(""), std::nan(""), static_cast<double>(ai::parseIso(x.modified))};
                m.rows.push_back(std::move(r));
            }
        } else if (theme == "vocabulaire") {
            m.headers = {"Mot", "Ce qu'il d\xC3\xA9signe", "D'o\xC3\xB9 \xC3\xA7" "a vient", "Mis \xC3\xA0 jour", "\xC3\x89tat"};
            for (const auto& x : allWords) {
                if (!q.empty() && !wordMatches(x, q)) continue;
                if (!passState(x.active, !x.builtIn)) continue;
                Rows::Row r;
                r.key = x.word;
                std::string meaning = x.means == ln::Meaning::Ignore ? std::string("(un mot \xC3\xA0 ignorer)") : x.target;
                if (x.means == ln::Meaning::Kind)
                    if (const auto k = hmi::kindFromKey(x.target)) meaning = "un objet " + std::string(hmi::kindLabel(*k));
                if (x.means == ln::Meaning::Model) meaning = "le mod\xC3\xA8le " + x.target;
                r.cells = {x.word, meaning, origin(x.source, x.builtIn, false), x.modified.empty() ? std::string("int\xC3\xA9gr\xC3\xA9") : ai::shortDate(x.modified),
                           stateCell(x.active)};
                r.styles.resize(5);
                r.styles[0].bold = !x.builtIn;
                if (!x.active)
                    for (auto& s : r.styles) s.fgTone = ui::Tone::Muted;
                r.tip = ln::describe(x);
                r.sort = {std::nan(""), std::nan(""), std::nan(""), static_cast<double>(ai::parseIso(x.modified))};
                m.rows.push_back(std::move(r));
            }
        } else if (theme == "apprendre") {
            m.headers = {"Demande non comprise", "Quand"};
            for (const auto& x : toTeach) {
                if (!q.empty() && folded(x.request).find(q) == std::string::npos) continue;
                Rows::Row r;
                r.key = x.request;
                r.cells = {x.request, ai::shortDate(x.date)};
                r.tip = x.request;
                m.rows.push_back(std::move(r));
            }
        }
        if (theme == "modeles" || theme == "vocabulaire") {
            std::stable_sort(m.rows.begin(), m.rows.end(), [&](const Rows::Row& a, const Rows::Row& b) {
                if (byName) return folded(a.cells[0]) < folded(b.cells[0]);
                const double x = a.sort.size() > 3 ? a.sort[3] : 0.0, y = b.sort.size() > 3 ? b.sort[3] : 0.0;
                return (std::isnan(x) ? 0.0 : x) > (std::isnan(y) ? 0.0 : y);
            });
        }
        std::vector<ui::TableView::Column> cols;
        if (theme == "apprendre") cols = {col(m.headers[0], 380.f), col(m.headers[1], 110.f)};
        else if (!textTheme()) cols = {col(m.headers[0], 120.f), col(m.headers[1], 250.f), col(m.headers[2], 130.f), col(m.headers[3], 80.f), col(m.headers[4], 90.f)};
        syncing_ = true;
        if (!cols.empty() && colsTheme_ != theme) {
            list->setColumns(std::move(cols));
            colsTheme_ = theme;
        }
        m.reset();
        selectKey(list, m.find(selected));
        if (m.find(selected) < 0) selected.clear();
        syncing_ = false;
        if (!textTheme() && selected.empty() && !m.rows.empty() && !adding_) {
            selected = m.rows.front().key;
            syncing_ = true;
            selectKey(list, 0);
            syncing_ = false;
        }
        fillDetail();
        invalidate();
    }

    // ---- le detail modifiable ----
    void fillTargets() {
        if (!wTarget || !wMeaning) return;
        const std::string means = wMeaning->selectedItem() ? wMeaning->selectedItem()->value : std::string("genre");
        std::vector<ui::DropDown::Item> items;
        if (means == "genre") {
            for (const auto k : hmi::kPlaceableKinds) {
                const std::string label(hmi::kindLabel(k));
                if (label.empty()) continue;
                items.push_back({label, std::string(hmi::kindKey(k)), {}, true});
            }
            std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return folded(a.label) < folded(b.label); });
        } else if (means == "modele") {
            for (const auto& m : allModels) items.push_back({m.name + " (" + m.label + ")", m.key, {}, true});
        }
        wTarget->setItems(std::move(items));
        wTarget->setSelectedIndex(0);
        wTarget->setEnabled(means != "ignorer");
    }
    const ln::BusinessModel* selectedModel() const {
        for (const auto& m : allModels)
            if (m.key == selected) return &m;
        return nullptr;
    }
    const ln::WordEntry* selectedWord() const {
        for (const auto& w : allWords)
            if (w.word == selected) return &w;
        return nullptr;
    }
    void fillDetail() {
        if (theme == "modeles") {
            const auto* m = adding_ ? nullptr : selectedModel();
            mName->setText(m ? m->name : std::string{});
            std::string ws;
            if (m)
                for (std::size_t i = 0; i < m->words.size(); ++i) ws += (i ? ", " : "") + m->words[i];
            mWords->setText(ws);
            members->rows = m ? m->members : std::vector<ln::ModelMember>{{"Marche", "BOOL", "En marche"}};
            members->reset();
            toggle->setText(m && !m->active ? "Activer" : "D\xC3\xA9sactiver");
            const bool hand = m && m->builtIn && m->corrected;
            revert->setEnabled(hand);
            remove->setEnabled(m && !m->builtIn);
            toggle->setEnabled(m != nullptr);
            addNew->setText("Nouveau mod\xC3\xA8le\xE2\x80\xA6");
        } else if (theme == "vocabulaire") {
            const auto* w = adding_ ? nullptr : selectedWord();
            wWord->setText(w ? w->word : std::string{});
            wWord->setReadOnly(w && w->builtIn);
            const int means = !w ? 0 : w->means == ln::Meaning::Kind ? 0 : w->means == ln::Meaning::Model ? 1 : 2;
            wMeaning->setSelectedIndex(means);
            fillTargets();
            if (w)
                for (std::size_t i = 0; i < wTarget->items().size(); ++i)
                    if (wTarget->items()[i].value == w->target) wTarget->setSelectedIndex(static_cast<int>(i));
            toggle->setText(w && !w->active ? "Activer" : "D\xC3\xA9sactiver");
            toggle->setEnabled(w && !w->builtIn);
            remove->setEnabled(w && !w->builtIn);
            save->setEnabled(!w || !w->builtIn);
            addNew->setText("Nouveau mot\xE2\x80\xA6");
        } else if (theme == "apprendre") {
            // les mots de la demande : celui qu'elle n'a pas compris est peut-etre l'un d'eux
            std::vector<ui::DropDown::Item> items;
            std::set<std::string> seen;
            std::string w;
            const auto push = [&] {
                if (w.size() >= 3 && seen.insert(folded(w)).second) items.push_back({w, w, {}, true});
                w.clear();
            };
            for (const char c : selected) {
                if (std::isalnum(static_cast<unsigned char>(c)) || (static_cast<unsigned char>(c) & 0x80) || c == '_') w += c;
                else push();
            }
            push();
            teachWord->setItems(std::move(items));
            teachWord->setSelectedIndex(teachWord->items().empty() ? -1 : 0);
            wMeaning->setSelectedIndex(0);
            fillTargets();
        }
        invalidateLayout();
        invalidate();
    }

    void beginNew() {
        adding_ = true;
        selected.clear();
        syncing_ = true;
        selectKey(list, -1);
        syncing_ = false;
        fillDetail();
        d_.say(theme == "modeles" ? "Nouveau mod\xC3\xA8le : son nom (T_Compresseur), ses mots, ses membres, puis Enregistrer."
                                  : "Nouveau mot : le mot, ce qu'il d\xC3\xA9signe, puis Enregistrer.");
    }

    bool saveDetail() {
        std::string why;
        if (theme == "modeles") {
            ln::BusinessModel m;
            if (const auto* cur = adding_ ? nullptr : selectedModel()) m = *cur;
            m.name = mName->text();
            if (adding_ || m.key.empty()) {
                std::string k = folded(m.name);
                if (k.rfind("t_", 0) == 0) k = k.substr(2);
                m.key = k;
                m.label = k;
            }
            m.words.clear();
            std::string cur;
            for (const char c : mWords->text() + ",") {
                if (c == ',' || c == ';') {
                    while (!cur.empty() && cur.front() == ' ') cur.erase(cur.begin());
                    while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                    if (!cur.empty()) m.words.push_back(cur);
                    cur.clear();
                } else cur += c;
            }
            m.members = members->rows;
            if (!ln::saveModel(m, &why)) {
                d_.say("Refus\xC3\xA9 : " + why, true);
                return false;
            }
            selected = m.key;
            adding_ = false;
            d_.say("Enregistr\xC3\xA9 : " + m.name + ". L'IA int\xC3\xA9gr\xC3\xA9" "e s'en sert d\xC3\xA8s la prochaine demande.");
        } else if (theme == "vocabulaire") {
            ln::WordEntry w;
            w.word = wWord->text();
            const std::string means = wMeaning->selectedItem() ? wMeaning->selectedItem()->value : std::string("genre");
            w.means = means == "modele" ? ln::Meaning::Model : means == "ignorer" ? ln::Meaning::Ignore : ln::Meaning::Kind;
            w.target = wTarget->selectedItem() ? wTarget->selectedItem()->value : std::string{};
            if (!ln::learnWord(w, &why)) {
                d_.say("Refus\xC3\xA9 : " + why, true);
                return false;
            }
            selected = w.word;
            adding_ = false;
            d_.say("Appris : " + ln::describe(w));
        } else {
            return false;
        }
        d_.refresh();
        return true;
    }
    bool revertDetail() {
        std::string why;
        const auto* m = selectedModel();
        if (theme != "modeles" || !m || !ln::revertModel(m->key, &why)) {
            if (!why.empty()) d_.say("Refus\xC3\xA9 : " + why, true);
            return false;
        }
        d_.say(m->name + " : revenu \xC3\xA0 l'original du logiciel.");
        d_.refresh();
        return true;
    }
    bool toggleDetail() {
        std::string why;
        bool ok = false;
        if (theme == "modeles") {
            if (const auto* m = selectedModel()) ok = ln::setModelActive(m->key, !m->active, &why);
        } else if (theme == "vocabulaire") {
            if (const auto* w = selectedWord()) ok = ln::setWordActive(w->word, !w->active, &why);
        }
        if (!ok) {
            if (!why.empty()) d_.say("Refus\xC3\xA9 : " + why, true);
            return false;
        }
        d_.refresh();
        return true;
    }
    bool removeDetail() {
        std::string why;
        bool ok = false;
        std::string what;
        if (theme == "modeles") {
            if (const auto* m = selectedModel()) {
                what = m->name;
                ok = ln::removeModel(m->key, &why);
            }
        } else if (theme == "vocabulaire") {
            if (const auto* w = selectedWord()) {
                what = w->word;
                ok = ln::forgetWord(w->word, &why);
            }
        }
        if (!ok) {
            if (!why.empty()) d_.say("Refus\xC3\xA9 : " + why, true);
            return false;
        }
        selected.clear();
        d_.say(what + " : retir\xC3\xA9.");
        d_.refresh();
        return true;
    }
    // A lui apprendre : le mot choisi de la demande = ce que dit << sens >> (un genre, un modele, un mot a ignorer).
    bool teachSelected() {
        if (theme != "apprendre" || selected.empty() || !teachWord->selectedItem()) {
            d_.say("Choisis une demande, puis le mot qu'elle n'a pas compris.", true);
            return false;
        }
        ln::WordEntry w;
        w.word = teachWord->selectedItem()->value;
        const std::string means = wMeaning->selectedItem() ? wMeaning->selectedItem()->value : std::string("genre");
        w.means = means == "modele" ? ln::Meaning::Model : means == "ignorer" ? ln::Meaning::Ignore : ln::Meaning::Kind;
        w.target = wTarget->selectedItem() ? wTarget->selectedItem()->value : std::string{};
        std::string why;
        if (!ln::learnWord(w, &why)) {
            d_.say("Refus\xC3\xA9 : " + why, true);
            return false;
        }
        const std::string phrase = selected;
        const auto t = ln::tryPhrase(phrase);
        lastTry = {phrase, t};
        if (t.understood) ln::dismissRequest(phrase);
        d_.say("Appris : " + ln::describe(w) + (t.understood ? " La demande est maintenant comprise." : " La demande n'est toujours pas comprise : un autre mot ?"),
               !t.understood);
        d_.refresh();
        return true;
    }
    bool ignoreSelected() {
        if (theme != "apprendre" || selected.empty()) return false;
        ln::dismissRequest(selected);
        d_.say("Ignor\xC3\xA9" "e : \xC2\xAB " + selected + " \xC2\xBB ne sera plus propos\xC3\xA9" "e.");
        selected.clear();
        refreshData();
        return true;
    }
    bool tryNow(const std::string& phrase) {
        if (phrase.empty()) return false;
        lastTry = {phrase, ln::tryPhrase(phrase)};
        invalidate();
        return lastTry.second.understood;
    }

    // Importer : voir d'abord ce que ferait l'import, puis confirmer.
    void askImport() {
        ui::FilePick pick;
        pick.title = "Importer un apprentissage (.xia)";
        pick.filters = {{"Apprentissage de l'IA", "xia"}};
        const std::weak_ptr<int> alive = d_.alive_;
        if (!ui::pickFile(pick, [this, alive](std::string path) {
                if (alive.expired() || path.empty()) return;
                (void)previewImport(path);
            }))
            d_.say("Pas de s\xC3\xA9lecteur de fichiers ici.", true);
    }
    bool previewImport(const std::string& path) {
        ln::ImportReport r;
        std::string why;
        if (!ln::peek(path, r, &why)) {
            d_.say("Import refus\xC3\xA9 : " + why, true);
            return false;
        }
        pendingImport = path;
        std::string text = "Importer l'apprentissage de " + r.from + " ? " + plural(static_cast<std::size_t>(r.added), "connaissance ajout\xC3\xA9" "e", "connaissances ajout\xC3\xA9" "es")
                         + ", " + plural(static_cast<std::size_t>(r.kept), "gard\xC3\xA9" "e (d\xC3\xA9j\xC3\xA0 l\xC3\xA0)", "gard\xC3\xA9" "es (d\xC3\xA9j\xC3\xA0 l\xC3\xA0)")
                         + (r.skipped ? ", " + plural(static_cast<std::size_t>(r.skipped), "ignor\xC3\xA9" "e", "ignor\xC3\xA9" "es") : std::string{}) + ".";
        d_.ask(Confirm::ImportLearning, std::move(text), "Importer");
        return true;
    }
    bool doImport() {
        if (pendingImport.empty()) return false;
        ln::ImportReport r;
        std::string why;
        const bool ok = ln::importFrom(pendingImport, r, false, &why);
        pendingImport.clear();
        if (!ok) {
            d_.say("Import refus\xC3\xA9 : " + why, true);
            return false;
        }
        ln::invalidate();
        d_.say("Apprentissage import\xC3\xA9 de " + r.from + " : " + plural(static_cast<std::size_t>(r.added), "connaissance ajout\xC3\xA9" "e", "connaissances ajout\xC3\xA9" "es")
               + (r.kept ? ", " + plural(static_cast<std::size_t>(r.kept), "gard\xC3\xA9" "e", "gard\xC3\xA9" "es") : std::string{}) + ".");
        d_.refresh();
        return true;
    }
    void askExport() {
        ui::FilePick pick;
        pick.title = "Exporter l'apprentissage (.xia)";
        pick.filters = {{"Apprentissage de l'IA", "xia"}};
        pick.save = true;
        pick.start = "apprentissage.xia";
        const std::weak_ptr<int> alive = d_.alive_;
        if (!ui::pickFile(pick, [this, alive](std::string path) {
                if (alive.expired() || path.empty()) return;
                (void)exportTo(path);
            }))
            d_.say("Pas de s\xC3\xA9lecteur de fichiers ici.", true);
    }
    bool exportTo(std::string path) {
        if (path.size() < 4 || folded(path.substr(path.size() - 4)) != ".xia") path += ".xia";
        std::string why;
        ln::FileOptions o;
        if (!ln::exportTo(path, o, &why)) {
            d_.say("Export refus\xC3\xA9 : " + why, true);
            return false;
        }
        d_.say("Apprentissage export\xC3\xA9 : " + path + " (sans la cl\xC3\xA9 API). Sur un autre PC : Importer\xE2\x80\xA6, ou l'installateur.");
        d_.refresh();
        return true;
    }

    // Une ligne du theme, par sa cle ou le texte de sa premiere case (sans casse ni accents).
    bool selectRow(std::string_view key) {
        if (textTheme()) return false;
        const std::string want = folded(key);
        for (std::size_t i = 0; i < listModel->rows.size(); ++i) {
            const auto& r = listModel->rows[i];
            if (folded(r.key) == want || (!r.cells.empty() && folded(r.cells[0]) == want)) {
                selected = r.key;
                adding_ = false;
                syncing_ = true;
                selectKey(list, static_cast<int>(i));
                syncing_ = false;
                fillDetail();
                return true;
            }
        }
        return false;
    }
    // A lui apprendre, sans les listes : le mot, son sens (genre, modele, ignorer), sa cible.
    bool teach(std::string_view word, std::string_view meaning, std::string_view target) {
        if (theme != "apprendre" || selected.empty()) return false;
        int wi = -1;
        for (std::size_t i = 0; i < teachWord->items().size(); ++i)
            if (folded(teachWord->items()[i].value) == folded(word)) wi = static_cast<int>(i);
        if (wi < 0) {
            d_.say("\xC2\xAB " + std::string(word) + " \xC2\xBB n'est pas un mot de cette demande.", true);
            return false;
        }
        teachWord->setSelectedIndex(wi);
        for (std::size_t i = 0; i < wMeaning->items().size(); ++i)
            if (wMeaning->items()[i].value == meaning) wMeaning->setSelectedIndex(static_cast<int>(i));
        fillTargets();
        for (std::size_t i = 0; i < wTarget->items().size(); ++i)
            if (folded(wTarget->items()[i].value) == folded(target)) wTarget->setSelectedIndex(static_cast<int>(i));
        return teachSelected();
    }
    // Un geste sur une connaissance (la consulter, l'ajouter) : son theme, s'il n'est pas deja montre.
    void followKnowledge(const std::string& category) {
        if (textTheme() && (category.empty() || category == theme)) return;
        std::string want = category;
        if (want.empty() || want == "modeles" || want == "vocabulaire") want = textTheme() ? theme : std::string("metier");
        theme = want;
        selected.clear();
        adding_ = false;
        if (d_.catFilter_ != want) {   // la page des connaissances suit son theme (sans repasser par showTheme)
            d_.catFilter_ = want;
            d_.rebuildTables();
            d_.syncWidgets();
        }
        fillList();
        invalidateLayout();
        invalidate();
    }

    // ---- la place ----
    ui::Button* importB{nullptr};
    ui::Button* exportB{nullptr};
    ui::DropDown* period{nullptr};
    ui::InputText* search{nullptr};
    ui::DropDown* stateBox{nullptr};
    ui::DropDown* sortBox{nullptr};
    std::shared_ptr<Rows> listModel;
    ui::TableView* list{nullptr};
    ui::InputText* mName{nullptr};
    ui::InputText* mWords{nullptr};
    std::shared_ptr<MembersModel> members;
    ui::TableView* mMembers{nullptr};
    ui::Button *mAdd{nullptr}, *mRemove{nullptr}, *mUp{nullptr}, *mDown{nullptr};
    ui::InputText* wWord{nullptr};
    ui::DropDown* wMeaning{nullptr};
    ui::DropDown* wTarget{nullptr};
    ui::Button *save{nullptr}, *revert{nullptr}, *toggle{nullptr}, *remove{nullptr}, *addNew{nullptr};
    ui::Button *teachLearn{nullptr}, *teachIgnore{nullptr};
    ui::DropDown* teachWord{nullptr};
    ui::InputText* tryIn{nullptr};
    ui::Button* tryB{nullptr};
    LearningPage* knowledge{nullptr};

    std::string theme{"modeles"};
    std::string selected;
    std::string pendingImport;
    std::pair<std::string, ln::Trial> lastTry;
    std::vector<ln::BusinessModel> allModels;
    std::vector<ln::WordEntry> allWords;
    std::vector<ln::Measure> measures;
    std::vector<ln::Decision> decided;
    std::vector<ln::Week> weeks;
    std::vector<ln::Measure> toTeach;
    std::pair<int, int> nowUnderstood, beforeUnderstood, nowKept, beforeKept;

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
            if (it->rect.contains(mouse)) return it->text;
        return tipAt(tips_, mouse);
    }
    [[nodiscard]] bool hasTooltip() const override { return true; }

    // Les places (essais) : les tuiles, les graphiques, un theme.
    [[nodiscard]] gfx::Rect tileRect(int i) const { return i >= 0 && i < 4 ? tiles_[i] : gfx::Rect{}; }
    [[nodiscard]] gfx::Rect rateChart() const { return chartA_; }
    [[nodiscard]] gfx::Rect learnChart() const { return chartB_; }
    [[nodiscard]] gfx::Rect themeRect(const std::string& key) const {
        for (const auto& [k, r] : themeRects_)
            if (k == key) return r;
        return {};
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float x0 = r.x + kPad, W = r.w - 2.f * kPad;
        float y = r.y + 8.f;
        // le bandeau, et Importer / Exporter a droite
        const float ew = std::max(200.f, ui::measureWidth(exportB->text(), gfx::FontId{16}) + 50.f);
        const float iw = std::max(110.f, ui::measureWidth(importB->text(), gfx::FontId{16}) + 50.f);
        banner_ = {x0, y, W, 34.f};
        exportB->setBounds({x0 + W - ew - 6.f, y + 3.f, ew, 28.f});
        importB->setBounds({exportB->bounds().x - 8.f - iw, y + 3.f, iw, 28.f});
        y += 34.f + 8.f;
        // les tuiles
        const float tw = (W - 3.f * kGap) / 4.f;
        for (int i = 0; i < 4; ++i) tiles_[i] = {x0 + static_cast<float>(i) * (tw + kGap), y, tw, 62.f};
        y += 62.f + 8.f;
        // les graphiques
        const float ch = std::clamp((r.bottom() - y) * 0.36f, 120.f, 200.f);
        const float aw = std::floor((W - kGap) * 0.56f);
        chartA_ = {x0, y, aw, ch};
        chartB_ = {x0 + aw + kGap, y, W - aw - kGap, ch};
        period->setBounds({chartA_.right() - 136.f, chartA_.y + 6.f, 128.f, 24.f});
        y += ch + 8.f;
        // en pied : essayer une phrase
        const float tryY = r.bottom() - 36.f;
        tryIn->setBounds({x0 + 200.f + kGap, tryY + 3.f, std::max(200.f, W * 0.42f), 28.f});
        tryB->setBounds({tryIn->bounds().right() + 8.f, tryY + 3.f, 100.f, 28.f});
        tryRow_ = {x0, tryY, W, 34.f};
        // en bas : les themes, la liste, le detail
        const float bottom = tryY - 6.f;
        themes_ = {x0, y, 200.f, std::max(80.f, bottom - y)};
        const float detailW = std::clamp(W * 0.32f, 300.f, 420.f);
        detail_ = {x0 + W - detailW, y, detailW, themes_.h};
        center_ = {themes_.right() + kGap, y, detail_.x - kGap - themes_.right() - kGap, themes_.h};
        // la recherche et les filtres, au-dessus de la liste
        const float fx = center_.x;
        const float sw = std::max(160.f, center_.w - 2.f * 160.f - 2.f * 8.f);
        search->setBounds({fx, center_.y, sw, 28.f});
        stateBox->setBounds({fx + sw + 8.f, center_.y, 160.f, 28.f});
        sortBox->setBounds({stateBox->bounds().right() + 8.f, center_.y, 152.f, 28.f});
        const bool text = textTheme();
        const auto show = [](ui::Widget* w, bool on) { w->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed); };
        show(knowledge, text);
        emptyTeach_ = theme == "apprendre" && listModel->rows.empty();
        show(list, !text && !emptyTeach_);
        if (text) {
            knowledge->setBounds({center_.x - 6.f, center_.y + 32.f, detail_.right() - center_.x + 6.f + 6.f, center_.h - 32.f});
        } else {
            list->setBounds({center_.x, center_.y + 34.f, center_.w, center_.h - 34.f});
        }
        // le detail : selon le theme
        const bool models = theme == "modeles", words = theme == "vocabulaire", teach = theme == "apprendre";
        for (ui::Widget* w : {static_cast<ui::Widget*>(mName), static_cast<ui::Widget*>(mWords), static_cast<ui::Widget*>(mMembers),
                              static_cast<ui::Widget*>(mAdd), static_cast<ui::Widget*>(mRemove), static_cast<ui::Widget*>(mUp),
                              static_cast<ui::Widget*>(mDown)})
            show(w, models);
        show(wWord, words);
        show(wMeaning, words || teach);
        show(wTarget, words || teach);
        show(teachWord, teach);
        show(teachLearn, teach);
        show(teachIgnore, teach);
        for (ui::Widget* w : {static_cast<ui::Widget*>(save), static_cast<ui::Widget*>(toggle), static_cast<ui::Widget*>(remove),
                              static_cast<ui::Widget*>(addNew)})
            show(w, models || words);
        show(revert, models);
        const float dx = detail_.x + 12.f, dw = detail_.w - 24.f;
        float dy = detail_.y + 34.f;
        const auto field = [&](ui::Widget* w, float h) {
            fieldsAt_.push_back({dx, dy});
            w->setBounds({dx, dy + 16.f, dw, h});
            dy += 16.f + h + 8.f;
        };
        fieldsAt_.clear();
        const float buttonsY = detail_.bottom() - 74.f;
        if (models) {
            // le nom et les mots cote a cote (la place va aux membres)
            const float nw = std::floor(dw * 0.38f);
            fieldsAt_.push_back({dx, dy});
            fieldsAt_.push_back({dx + nw + 8.f, dy});
            mName->setBounds({dx, dy + 16.f, nw, 26.f});
            mWords->setBounds({dx + nw + 8.f, dy + 16.f, dw - nw - 8.f, 26.f});
            dy += 16.f + 26.f + 8.f;
            fieldsAt_.push_back({dx, dy});
            // les boutons des membres sur la ligne de leur titre, a droite (la table garde la hauteur)
            float bx = dx + dw;
            for (ui::Button* b : {mDown, mUp, mRemove, mAdd}) {
                const float bw = b->text().empty() ? 28.f : ui::measureWidth(b->text(), gfx::FontId{16}) + 36.f;
                bx -= bw;
                b->setBounds({bx, dy - 5.f, bw, 23.f});
                bx -= 4.f;
            }
            membersLabelRoom_ = bx - 4.f - dx;
            const float mh = std::max(56.f, buttonsY - 6.f - (dy + 22.f));
            mMembers->setBounds({dx, dy + 22.f, dw, mh});
        } else if (words) {
            field(wWord, 26.f);
            field(wMeaning, 26.f);
            field(wTarget, 26.f);
        } else if (teach) {
            dy += 40.f;   // la demande, en clair
            field(teachWord, 26.f);
            field(wMeaning, 26.f);
            field(wTarget, 26.f);
            const float bw = (dw - 8.f) / 2.f;
            teachLearn->setBounds({dx, dy + 4.f, bw, 30.f});
            teachIgnore->setBounds({dx + bw + 8.f, dy + 4.f, bw, 30.f});
        }
        if (models || words) {
            // deux rangees de boutons, sans chevauchement
            const float bw = (dw - 8.f) / 2.f;
            save->setBounds({dx, buttonsY, bw, 30.f});
            addNew->setBounds({dx + bw + 8.f, buttonsY, bw, 30.f});
            const int n = models ? 3 : 2;
            const float bw3 = (dw - 8.f * static_cast<float>(n - 1)) / static_cast<float>(n);
            float bx = dx;
            if (models) {
                revert->setBounds({bx, buttonsY + 36.f, bw3, 30.f});
                bx += bw3 + 8.f;
            }
            toggle->setBounds({bx, buttonsY + 36.f, bw3, 30.f});
            remove->setBounds({bx + bw3 + 8.f, buttonsY + 36.f, bw3, 30.f});
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const TipScope tips(tips_);
        hits_.clear();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(bounds(), c.windowBg);
        // le bandeau honnete
        ctx.r.fillRoundedRect(banner_, c.accent.withAlpha(ctx.theme.isDark() ? 34 : 22), 6.f);
        ctx.r.fillRoundedRect({banner_.x, banner_.y, 3.f, banner_.h}, c.accent, 1.5f);
        ui::drawIcon(ctx.r, ui::Icon::Info, {banner_.x + 12.f, banner_.y + 10.f, 14.f, 14.f}, c.accent);
        drawFit(ctx, {banner_.x + 34.f, banner_.y + (banner_.h - ctx.r.lineHeight(kSmall)) * 0.5f},
                "Elle apprend sur ce PC : tes corrections, tes mots, tes mod\xC3\xA8les m\xC3\xA9tier. L'IA int\xC3\xA9gr\xC3\xA9" "e s'en sert directement ; "
                "les IA connect\xC3\xA9" "es les re\xC3\xA7oivent avec chaque demande. Le mod\xC3\xA8le distant n'est pas r\xC3\xA9" "entra\xC3\xAEn\xC3\xA9.",
                kSmall, importB->bounds().x - 12.f - (banner_.x + 34.f), c.text);
        paintTiles(ctx);
        paintRates(ctx);
        paintLearned(ctx);
        paintThemes(ctx);
        if (!textTheme()) paintDetail(ctx);
        if (emptyTeach_)
            for (const auto& [i, line] : enumerateLines(ctx, search->text().empty() ? "Rien \xC3\xA0 lui apprendre : les demandes de ces 7 jours ont toutes \xC3\xA9t\xC3\xA9 "
                                                                                    "comprises (ou tu les as ignor\xC3\xA9" "es)."
                                                                                  : "Aucune demande non comprise ne contient ce texte.",
                                                       center_.w - 8.f))
                ctx.r.drawText({center_.x + 4.f, center_.y + 44.f + 18.f * static_cast<float>(i)}, line, kSmall, c.textMuted);
        // en pied
        textMid(ctx, tryRow_.x, tryRow_, "ESSAYER UNE PHRASE", kCaption, c.textMuted);
        if (!lastTry.first.empty()) {
            const float rx = tryB->bounds().right() + 12.f;
            const auto& t = lastTry.second;
            const std::string res = (t.understood ? "\xE2\x9C\x93 Comprise \xE2\x86\x92 " : "\xE2\x9C\x97 Pas comprise : ") + t.summary;
            drawFit(ctx, {rx, tryRow_.y + (tryRow_.h - ctx.r.lineHeight(kSmall)) * 0.5f}, res, kSmall, tryRow_.right() - rx, t.understood ? c.ok : c.warning);
        } else if (!ln::hasPhraseTester()) {
            const float rx = tryB->bounds().right() + 12.f;
            drawFit(ctx, {rx, tryRow_.y + (tryRow_.h - ctx.r.lineHeight(kSmall)) * 0.5f}, "(un projet ouvert : l'IA int\xC3\xA9gr\xC3\xA9" "e la lit sans rien appliquer)",
                    kSmall, tryRow_.right() - rx, c.textMuted);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && m->button == ui::MouseButton::Left) {
            for (const auto& [key, rect] : themeRects_)
                if (rect.contains(m->pos)) {
                    (void)showTheme(key);
                    return ui::EventResult::Consumed;
                }
            if (tiles_[3].contains(m->pos)) {
                (void)showTheme("apprendre");
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    ui::Button& button(const std::string& id, const std::string& text, ui::Icon icon) {
        auto& b = static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>(text, id)));
        if (icon != ui::Icon::None) b.setIcon(icon);
        return b;
    }
    ui::InputText& input(const std::string& id, const std::string& placeholder) {
        auto& t = static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(id)));
        t.setPlaceholder(placeholder);
        return t;
    }

    void tile(const ui::PaintContext& ctx, const gfx::Rect& r, const std::string& title, const std::string& big, const std::string& unit,
              const std::string& foot, gfx::Color footColor, bool warm) {
        const auto& c = ctx.theme.color;
        card(ctx, r, warm ? c.warning : c.border);
        drawFit(ctx, {r.x + 12.f, r.y + 7.f}, title, kSmall, r.w - 24.f, c.textMuted);
        drawBold(ctx, {r.x + 12.f, r.y + 22.f}, big, kBig, c.text);
        const float bw = textW(ctx, big, kBig) + 8.f;
        drawFit(ctx, {r.x + 12.f + bw, r.y + 30.f}, unit, kSmall, r.w - 24.f - bw, c.text);
        drawFit(ctx, {r.x + 12.f, r.y + r.h - 16.f}, foot, kCaption, r.w - 24.f, footColor);
    }
    void paintTiles(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        std::size_t total = 0, themeCount = 0, recent = 0, mine = 0;
        const long long nowT = static_cast<long long>(ai::now());
        for (const auto& t : themes()) {
            if (t.key == "apprendre") continue;
            total += t.count;
            if (t.count) ++themeCount;
        }
        for (const auto& k : d_.registry_.all()) {
            if (k.builtIn) continue;
            const long long t = static_cast<long long>(ai::parseIso(k.modified.empty() ? k.added : k.modified));
            if (t > 0 && nowT - t < 28LL * 86400) {
                ++recent;
                if (k.source == "utilisateur") ++mine;
            }
        }
        tile(ctx, tiles_[0], "Ce qu'elle sait", std::to_string(total), "\xC3\xA9l\xC3\xA9ments, en " + std::to_string(themeCount) + " th\xC3\xA8mes",
             recent ? "+" + std::to_string(recent) + " en 4 semaines, dont " + std::to_string(mine) + " de ta main" : std::string("rien appris ces 4 semaines"),
             recent ? c.ok : c.textMuted, false);
        const auto deltaText = [](std::pair<int, int> now, std::pair<int, int> before) -> std::string {
            if (now.second == 0 || before.second == 0) return {};
            const int d = static_cast<int>(std::lround(100.0 * now.first / now.second - 100.0 * before.first / before.second));
            return (d >= 0 ? "+" : "") + std::to_string(d) + " pts sur les 30 jours d'avant";
        };
        {
            const auto& n = nowUnderstood;
            const std::string d = deltaText(n, beforeUnderstood);
            tile(ctx, tiles_[1], "Demandes comprises par l'IA int\xC3\xA9gr\xC3\xA9" "e", pct(n.first, n.second),
                 n.second ? "sur " + plural(static_cast<std::size_t>(n.second), "demande", "demandes") + " (30 jours)" : std::string{},
                 n.second ? (d.empty() ? std::string("pas encore de mesure plus ancienne") : d) : std::string("pas encore de mesure"),
                 c.textMuted, false);
        }
        {
            const auto& n = nowKept;
            const std::string d = deltaText(n, beforeKept);
            tile(ctx, tiles_[2], "Propositions gard\xC3\xA9" "es", pct(n.first, n.second),
                 n.second ? "appliqu\xC3\xA9" "es sur " + std::to_string(n.second) + " d\xC3\xA9" "cid\xC3\xA9" "es (30 jours)" : std::string{},
                 n.second ? (d.empty() ? std::string("appliqu\xC3\xA9" "es, pas refus\xC3\xA9" "es") : d) : std::string("pas encore de proposition d\xC3\xA9" "cid\xC3\xA9" "e"),
                 c.textMuted, false);
        }
        tile(ctx, tiles_[3], "\xC3\x80 lui apprendre", std::to_string(toTeach.size()),
             toTeach.size() > 1 ? "demandes non comprises" : "demande non comprise",
             toTeach.empty() ? std::string("cette semaine, tout a \xC3\xA9t\xC3\xA9 compris") : std::string("cette semaine \xC2\xB7 un clic pour lui montrer"),
             toTeach.empty() ? c.textMuted : c.warning, !toTeach.empty());
    }

    // (a) les deux taux par semaine : un seul axe en %, une legende, la valeur au dernier point, une infobulle par point.
    void paintRates(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        card(ctx, chartA_, c.border);
        drawFit(ctx, {chartA_.x + 12.f, chartA_.y + 8.f}, "Elle progresse : demandes comprises et propositions gard\xC3\xA9" "es, par semaine", kSmall,
                period->bounds().x - 16.f - chartA_.x, c.text);
        // la legende
        float lx = chartA_.x + 12.f;
        const float ly = chartA_.y + 28.f;
        for (int s = 0; s < 2; ++s) {
            const std::string label = s == 0 ? "Demandes comprises (IA int\xC3\xA9gr\xC3\xA9" "e)" : "Propositions gard\xC3\xA9" "es";
            ctx.r.line({lx, ly + 7.f}, {lx + 16.f, ly + 7.f}, seriesColor(ctx, s), 2.f);
            ctx.r.drawText({lx + 20.f, ly}, label, kCaption, c.textMuted);
            lx += 32.f + textW(ctx, label, kCaption);
        }
        const gfx::Rect plot{chartA_.x + 48.f, chartA_.y + 48.f, chartA_.w - 100.f, chartA_.h - 70.f};
        int measured = 0;
        for (const auto& w : weeks) measured += w.asked;
        if (measured < 3) {
            const std::string msg = "La courbe appara\xC3\xAEtra apr\xC3\xA8s quelques jours d'utilisation : "
                                  + plural(static_cast<std::size_t>(measured), "demande mesur\xC3\xA9" "e", "demandes mesur\xC3\xA9" "es") + " pour l'instant.";
            for (const auto& [i, line] : enumerateLines(ctx, msg, plot.w))
                ctx.r.drawText({plot.x, plot.y + 20.f + 18.f * static_cast<float>(i)}, line, kSmall, c.textMuted);
            return;
        }
        // l'axe : 0 a 100 %, des lignes discretes
        for (int g = 0; g <= 4; ++g) {
            const float y = plot.bottom() - plot.h * static_cast<float>(g) / 4.f;
            ctx.r.line({plot.x, y}, {plot.right(), y}, c.border, 1.f);
            const std::string t = std::to_string(g * 25) + " %";
            ctx.r.drawText({plot.x - 6.f - textW(ctx, t, kCaption), y - 6.f}, t, kCaption, c.textMuted);
        }
        const std::size_t n = weeks.size();
        const auto xAt = [&](std::size_t i) { return plot.x + (n <= 1 ? 0.f : plot.w * static_cast<float>(i) / static_cast<float>(n - 1)); };
        for (std::size_t i = 0; i < n; ++i)
            if (n <= 10 || i % 2 == (n - 1) % 2) {
                const std::string t = weekLabel(weeks[i].start);
                ctx.r.drawText({xAt(i) - textW(ctx, t, kCaption) * 0.5f, plot.bottom() + 4.f}, t, kCaption, c.textMuted);
            }
        for (int s = 0; s < 2; ++s) {
            const gfx::Color col = seriesColor(ctx, s);
            bool have = false;
            gfx::Point prev{};
            gfx::Point last{};
            int lastValue = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const int whole = s == 0 ? weeks[i].asked : weeks[i].decided;
                if (whole <= 0) continue;
                const int part = s == 0 ? weeks[i].understood : weeks[i].kept;
                const float v = static_cast<float>(part) / static_cast<float>(whole);
                const gfx::Point p{xAt(i), plot.bottom() - plot.h * v};
                if (have) ctx.r.line(prev, p, col, 2.f);
                ctx.r.fillRoundedRect({p.x - 4.f, p.y - 4.f, 8.f, 8.f}, c.panelBg, 4.f);
                ctx.r.fillRoundedRect({p.x - 3.f, p.y - 3.f, 6.f, 6.f}, col, 3.f);
                hits_.push_back({{p.x - 8.f, p.y - 8.f, 16.f, 16.f},
                                 std::string(s == 0 ? "Demandes comprises" : "Propositions gard\xC3\xA9" "es") + ", semaine du " + weekLabel(weeks[i].start) + " : "
                                     + pct(part, whole) + " (" + std::to_string(part) + " sur " + std::to_string(whole) + ")"});
                prev = p;
                last = p;
                lastValue = static_cast<int>(std::lround(100.0 * v));
                have = true;
            }
            if (have) ctx.r.drawText({last.x + 8.f, last.y - 7.f + static_cast<float>(s) * 12.f}, std::to_string(lastValue) + " %", kCaption, c.text);
        }
    }
    // (b) ce qu'elle a appris par semaine : des barres empilees par origine, le total au-dessus.
    void paintLearned(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        card(ctx, chartB_, c.border);
        int total = 0, peak = 1;
        for (const auto& w : weeks) {
            const int t = w.byUser + w.byAssistant + w.byFile;
            total += t;
            peak = std::max(peak, t);
        }
        drawFit(ctx, {chartB_.x + 12.f, chartB_.y + 8.f}, "Ce qu'elle a appris chaque semaine" + kDot + plural(static_cast<std::size_t>(total), "\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments"),
                kSmall, chartB_.w - 24.f, c.text);
        float lx = chartB_.x + 12.f;
        const float ly = chartB_.y + 28.f;
        const char* labels[] = {"Tes corrections", "Retenu par l'Assistant", "Lu dans tes fichiers"};
        for (int s = 0; s < 3; ++s) {
            if (lx + 30.f + textW(ctx, labels[s], kCaption) > chartB_.right() - 8.f) break;
            ctx.r.fillRoundedRect({lx, ly + 2.f, 10.f, 10.f}, seriesColor(ctx, s), 2.f);
            ctx.r.drawText({lx + 14.f, ly}, labels[s], kCaption, c.textMuted);
            lx += 26.f + textW(ctx, labels[s], kCaption);
        }
        const gfx::Rect plot{chartB_.x + 16.f, chartB_.y + 52.f, chartB_.w - 32.f, chartB_.h - 72.f};
        ctx.r.line({plot.x, plot.bottom()}, {plot.right(), plot.bottom()}, c.border, 1.f);
        if (total == 0) {
            drawFit(ctx, {plot.x, plot.y + 20.f}, "Rien d'appris sur cette p\xC3\xA9riode : corrige un mod\xC3\xA8le, apprends-lui un mot.", kSmall, plot.w,
                    c.textMuted);
            return;
        }
        const std::size_t n = weeks.size();
        const float slot = plot.w / static_cast<float>(std::max<std::size_t>(1, n));
        const float bw = std::max(4.f, std::min(28.f, slot * 0.6f));
        for (std::size_t i = 0; i < n; ++i) {
            const auto& w = weeks[i];
            const int parts[] = {w.byUser, w.byAssistant, w.byFile};
            const float x = plot.x + slot * (static_cast<float>(i) + 0.5f) - bw * 0.5f;
            float y = plot.bottom();
            int sum = 0;
            for (int s = 0; s < 3; ++s) {
                if (parts[s] <= 0) continue;
                const float h = plot.h * static_cast<float>(parts[s]) / static_cast<float>(peak);
                ctx.r.fillRect({x, y - h, bw, std::max(1.f, h - 2.f)}, seriesColor(ctx, s));
                y -= h;
                sum += parts[s];
            }
            if (sum > 0) {
                const std::string t = std::to_string(sum);
                ctx.r.drawText({x + bw * 0.5f - textW(ctx, t, kCaption) * 0.5f, y - 14.f}, t, kCaption, c.text);
                hits_.push_back({{x - 4.f, y - 16.f, bw + 8.f, plot.bottom() - y + 16.f},
                                 "Semaine du " + weekLabel(w.start) + " : " + std::to_string(sum) + " (" + std::to_string(w.byUser) + " de toi, "
                                     + std::to_string(w.byAssistant) + " par l'Assistant, " + std::to_string(w.byFile) + " de tes fichiers)"});
            }
            if (n <= 10 || i % 2 == (n - 1) % 2) {
                const std::string t = weekLabel(w.start);
                if (plot.x + slot * (static_cast<float>(i) + 0.5f) - textW(ctx, t, kCaption) * 0.5f >= chartB_.x + 4.f)
                    ctx.r.drawText({plot.x + slot * (static_cast<float>(i) + 0.5f) - textW(ctx, t, kCaption) * 0.5f, plot.bottom() + 4.f}, t, kCaption, c.textMuted);
            }
        }
    }
    void paintThemes(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        card(ctx, themes_, c.border);
        caption(ctx, themes_.x + 10.f, themes_.y + 10.f, ui::Icon::Library, "PAR TH\xC3\x88ME", c.textMuted);
        themeRects_.clear();
        float y = themes_.y + 32.f;
        const float rowH = 24.f;
        for (const auto& t : themes()) {
            if (y + rowH > themes_.bottom() - 4.f) break;
            const gfx::Rect r{themes_.x + 4.f, y, themes_.w - 8.f, rowH};
            const bool on = t.key == theme;
            if (on) ctx.r.fillRoundedRect(r, c.accent.withAlpha(ctx.theme.isDark() ? 50 : 30), 4.f);
            ui::drawIcon(ctx.r, t.icon, {r.x + 6.f, r.y + 5.f, 14.f, 14.f}, t.key == "apprendre" && t.count ? c.warning : c.textMuted);
            const std::string count = std::to_string(t.count) + (t.recent ? " +" + std::to_string(t.recent) : std::string{});
            const float cw = textW(ctx, count, kSmall);
            drawFit(ctx, {r.x + 26.f, r.y + (rowH - ctx.r.lineHeight(kSmall)) * 0.5f}, t.label, kSmall, r.w - 34.f - cw, on ? c.text : c.text);
            ctx.r.drawText({r.right() - 6.f - cw, r.y + (rowH - ctx.r.lineHeight(kSmall)) * 0.5f}, count, kSmall, t.recent ? c.ok : c.textMuted);
            themeRects_.emplace_back(t.key, r);
            addTip(r, t.label + " : " + std::to_string(t.count) + (t.recent ? " (+" + std::to_string(t.recent) + " ces 4 semaines)" : std::string{}));
            y += rowH;
        }
    }
    void paintDetail(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        card(ctx, detail_, c.border);
        const float x = detail_.x + 12.f, w = detail_.w - 24.f;
        std::string title, sub;
        std::vector<std::string> labels;
        if (theme == "modeles") {
            const auto* m = adding_ ? nullptr : selectedModel();
            title = m ? m->name : std::string("Nouveau mod\xC3\xA8le");
            sub = !m ? std::string("un mod\xC3\xA8le de toi") : m->builtIn ? (m->corrected ? std::string("du logiciel, corrig\xC3\xA9 par toi") : std::string("du logiciel"))
                                                                          : std::string("ajout\xC3\xA9 par toi");
            labels = {"Nom du type", "Les mots qui l'appellent (virgules)", "Ses membres"};
        } else if (theme == "vocabulaire") {
            const auto* wd = adding_ ? nullptr : selectedWord();
            title = wd ? "\xC2\xAB " + wd->word + " \xC2\xBB" : std::string("Nouveau mot");
            sub = !wd ? std::string("un mot de toi") : wd->builtIn ? std::string("du logiciel (en lecture seule)") : std::string("appris");
            labels = {"Le mot", "Il d\xC3\xA9signe", "Lequel"};
        } else {
            title = selected.empty() ? std::string("\xC3\x80 lui apprendre") : std::string("Lui apprendre");
            sub = selected.empty() ? std::string("choisis une demande non comprise") : std::string{};
            labels = {"Le mot qu'elle n'a pas compris", "Il d\xC3\xA9signe", "Lequel"};
            if (!selected.empty())
                for (const auto& [i, line] : enumerateLines(ctx, "\xC2\xAB " + selected + " \xC2\xBB", w))
                    if (i < 2) ctx.r.drawText({x, detail_.y + 34.f + 16.f * static_cast<float>(i)}, line, kSmall, c.text);
        }
        drawBold(ctx, {x, detail_.y + 10.f}, fit(ctx, title, kText, w * 0.6f), kText, c.text);
        const float tw = textW(ctx, fit(ctx, title, kText, w * 0.6f), kText) + 10.f;
        drawFit(ctx, {x + tw, detail_.y + 12.f}, sub, kSmall, w - tw, c.textMuted);
        for (std::size_t i = 0; i < labels.size() && i < fieldsAt_.size(); ++i) {
            float room = i + 1 < fieldsAt_.size() && fieldsAt_[i + 1].y == fieldsAt_[i].y ? fieldsAt_[i + 1].x - 8.f - fieldsAt_[i].x
                                                                                          : detail_.right() - 12.f - fieldsAt_[i].x;
            if (theme == "modeles" && i == 2) room = membersLabelRoom_;
            drawFit(ctx, fieldsAt_[i], labels[i], kCaption, room, c.textMuted);
        }
    }
    static std::vector<std::pair<std::size_t, std::string>> enumerateLines(const ui::PaintContext& ctx, const std::string& s, float w) {
        std::vector<std::pair<std::size_t, std::string>> out;
        std::size_t i = 0;
        for (auto& l : wrap(ctx, s, kSmall, w, 3)) out.emplace_back(i++, std::move(l));
        return out;
    }

    AiMemoryDialog& d_;
    bool syncing_{false};
    bool adding_{false};
    bool emptyTeach_{false};
    std::string colsTheme_;
    float membersLabelRoom_{0.f};
    gfx::Rect banner_{}, tiles_[4]{}, chartA_{}, chartB_{}, themes_{}, center_{}, detail_{}, tryRow_{};
    std::vector<gfx::Point> fieldsAt_;
    std::vector<std::pair<std::string, gfx::Rect>> themeRects_;
    std::vector<Tip> tips_;
    std::vector<Tip> hits_;
    core::ConnectionScope links_;
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

// ============================================================================ Souvenirs (1.21.0) ===
//  LE GESTIONNAIRE DE LA MEMOIRE PERSISTANTE. En haut, le bandeau honnete (le modele distant n'est pas reentraine) ;
//  la recherche et les filtres (portee, statut, projet) ; a gauche les souvenirs (epingles et conflits d'abord), a
//  droite le souvenir choisi - sa portee, son statut, son projet, sa provenance, ses dates, sa confiance, son contenu,
//  ses conflits, son historique - et ses gestes ; en bas, les sauvegardes de la memoire.
class AiMemoryDialog::SouvenirsPage final : public ui::Widget {
public:
    explicit SouvenirsPage(AiMemoryDialog& d) : ui::Widget("ia.memoire.souvenirs"), d_(d) {
        const std::string base = "ia.memoire.souvenirs.";
        search = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + "recherche")));
        search->setPlaceholder("Rechercher dans les souvenirs");
        scopeBox = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "portee")));
        {
            std::vector<ui::DropDown::Item> items{{"Toutes les port\xC3\xA9" "es", "", {}, true}};
            for (const auto& c : mem::kScopes) items.push_back({std::string(c.label), std::string(c.key), {}, true});
            scopeBox->setItems(std::move(items));
            scopeBox->setSelectedIndex(0);
        }
        statusBox = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "statut")));
        {
            std::vector<ui::DropDown::Item> items{{"Tous les statuts", "", {}, true}};
            for (const auto& c : mem::kStatuses) items.push_back({std::string(c.label), std::string(c.key), {}, true});
            items.push_back({"En conflit", "conflits", {}, true});
            statusBox->setItems(std::move(items));
            statusBox->setSelectedIndex(0);
        }
        projBox = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + "projet")));
        model = std::make_shared<Rows>();
        model->headers = {"Souvenir", "Port\xC3\xA9" "e", "Statut", "Projet", "Modifi\xC3\xA9"};
        table = &makeTable(*this, base + "liste", model,
                           {col("Souvenir", 230.f), col("Port\xC3\xA9" "e", 105.f), col("Statut", 120.f), col("Projet", 115.f), col("Modifi\xC3\xA9", 90.f)});
        const auto button = [&](const char* text, const std::string& id) {
            return &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>(text, base + id)));
        };
        confirm = button("Confirmer", "confirmer");
        confirm->setStyle(ui::Button::Style::Primary);
        hypothesis = button("Hypoth\xC3\xA8se", "hypothese");
        pin = button("\xC3\x89pingler", "epingler");
        pin->setIcon(ui::Icon::Pin);
        keep = button("Garder", "garder");
        associate = button("Associer au projet ouvert", "associer");
        associate->setIcon(ui::Icon::Link);
        edit = button("Modifier", "modifier");
        edit->setIcon(ui::Icon::Rename);
        reject = button("Refuser", "refuser");
        remove = button("Supprimer\xE2\x80\xA6", "supprimer");
        remove->setStyle(ui::Button::Style::Danger);
        remove->setIcon(ui::Icon::Delete);
        backup = button("Sauvegarder maintenant", "sauvegarder");
        backup->setIcon(ui::Icon::Save);
        restore = button("Restaurer la plus r\xC3\xA9" "cente\xE2\x80\xA6", "restaurer");
        restore->setIcon(ui::Icon::Undo);
        edName = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + "ednom")));
        edName->setPlaceholder("Un titre court");
        edContent = &static_cast<ui::MultiLineText&>(addChild(std::make_unique<ui::MultiLineText>(base + "edcontenu")));
        edContent->setWordWrap(true);
        edContent->setShowLineNumbers(false);
        edContent->setLanguage(ui::Language::PlainText);
        edSave = button("Enregistrer", "enregistrer");
        edSave->setStyle(ui::Button::Style::Primary);
        edCancel = button("Annuler", "annuler");
        showEditor(false);
    }

    void showEditor(bool on) {
        editorOn = on;
        const auto v = [](bool b) { return b ? ui::Visibility::Visible : ui::Visibility::Collapsed; };
        for (ui::Widget* w : {static_cast<ui::Widget*>(edName), static_cast<ui::Widget*>(edContent), static_cast<ui::Widget*>(edSave),
                              static_cast<ui::Widget*>(edCancel)})
            w->setVisibility(v(on));
        for (ui::Widget* w : {static_cast<ui::Widget*>(confirm), static_cast<ui::Widget*>(hypothesis), static_cast<ui::Widget*>(pin),
                              static_cast<ui::Widget*>(keep), static_cast<ui::Widget*>(associate), static_cast<ui::Widget*>(edit),
                              static_cast<ui::Widget*>(reject), static_cast<ui::Widget*>(remove)})
            if (on) w->setVisibility(ui::Visibility::Collapsed);
        invalidateLayout();
        invalidate();
    }

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override { return tipAt(tips_, mouse); }
    [[nodiscard]] bool hasTooltip() const override { return !tips_.empty() || ui::Widget::hasTooltip(); }

    ui::InputText*        search{nullptr};
    ui::DropDown*         scopeBox{nullptr};
    ui::DropDown*         statusBox{nullptr};
    ui::DropDown*         projBox{nullptr};
    std::shared_ptr<Rows> model;
    ui::TableView*        table{nullptr};
    ui::Button*           confirm{nullptr};
    ui::Button*           hypothesis{nullptr};
    ui::Button*           pin{nullptr};
    ui::Button*           keep{nullptr};
    ui::Button*           associate{nullptr};
    ui::Button*           edit{nullptr};
    ui::Button*           reject{nullptr};
    ui::Button*           remove{nullptr};
    ui::Button*           backup{nullptr};
    ui::Button*           restore{nullptr};
    ui::InputText*        edName{nullptr};
    ui::MultiLineText*    edContent{nullptr};
    ui::Button*           edSave{nullptr};
    ui::Button*           edCancel{nullptr};
    bool                  editorOn{false};

protected:
    void onLayout() override {
        const auto r = bounds();
        const float x0 = r.x + kPad, W = r.w - 2.f * kPad;
        info_ = {x0, r.y + 10.f, W, 48.f};
        const float fy = info_.bottom() + 8.f;
        filtersY_ = fy;
        const float sw = std::min(250.f, std::floor(W * 0.22f));
        search->setBounds({x0, fy, sw, 30.f});
        float x = x0 + sw + 14.f;
        const auto place = [&](ui::DropDown* box, const char* label, float w, float& labelX) {
            labelX = x;
            const float lw = ui::measureWidth(label, kSmall) + 8.f;
            box->setBounds({x + lw, fy, w, 30.f});
            x += lw + w + 14.f;
        };
        place(scopeBox, "Port\xC3\xA9" "e", 150.f, scopeLabelX_);
        place(statusBox, "Statut", 165.f, statusLabelX_);
        place(projBox, "Projet", 200.f, projLabelX_);
        countX_ = x;
        countW_ = x0 + W - x;
        // les sauvegardes, en bas
        backups_ = {x0, r.bottom() - 10.f - 36.f, W, 36.f};
        float bx = backups_.right() - 6.f;
        for (ui::Button* b : {restore, backup}) {
            const float bw = std::max(120.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 48.f);
            bx -= bw;
            b->setBounds({bx, backups_.y + 3.f, bw, 30.f});
            bx -= 8.f;
        }
        backupsTextW_ = bx - backups_.x - 34.f - 8.f;
        const float top = fy + 40.f;
        const float h = backups_.y - 8.f - top;
        const float tw = std::floor(W * 0.55f);
        table->setBounds({x0, top, tw, h});
        detail_ = {x0 + tw + kGap, top, W - tw - kGap, h};
        // le detail : ses gestes sur deux rangees, en bas
        const auto row = [&](std::initializer_list<ui::Button*> buttons, float y) {
            float rx = detail_.x + 12.f;
            for (ui::Button* b : buttons) {
                if (!b->visible()) continue;
                const float bw = std::max(84.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 44.f);
                b->setBounds({rx, y, std::max(0.f, std::min(bw, detail_.right() - 12.f - rx)), 30.f});
                rx += bw + 8.f;
            }
        };
        row({confirm, hypothesis, pin, keep}, detail_.bottom() - 80.f);
        row({associate, edit, reject, remove}, detail_.bottom() - 42.f);
        if (editorOn) {
            edName->setBounds({detail_.x + 12.f, detail_.y + 52.f, detail_.w - 24.f, 30.f});
            edContent->setBounds({detail_.x + 12.f, detail_.y + 110.f, detail_.w - 24.f, detail_.bottom() - 56.f - (detail_.y + 110.f)});
            float ex = detail_.right() - 12.f;
            for (ui::Button* b : {edSave, edCancel}) {
                const float bw = std::max(110.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 36.f);
                ex -= bw;
                b->setBounds({ex, detail_.bottom() - 44.f, bw, 32.f});
                ex -= 10.f;
            }
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const TipScope tips(tips_);
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(bounds(), c.windowBg);
        // le bandeau honnete
        ctx.r.fillRoundedRect(info_, c.accent.withAlpha(ctx.theme.isDark() ? 34 : 22), 6.f);
        ctx.r.fillRoundedRect({info_.x, info_.y, 3.f, info_.h}, c.accent, 1.5f);
        ui::drawIcon(ctx.r, ui::Icon::Info, {info_.x + 12.f, info_.y + 10.f, 14.f, 14.f}, c.accent);
        {
            // deux lignes : ce que c'est, puis ce que ce n'est pas (la phrase entiere, jamais coupee a cette largeur)
            const std::string lead = "Souvenirs : la m\xC3\xA9moire persistante de l'Assistant, sur ce PC.";
            const std::string honest(mem::kNotRetrained);
            drawBold(ctx, {info_.x + 34.f, info_.y + 6.f}, fit(ctx, lead, kText, info_.w - 44.f), kText, c.text);
            const std::string shown = fit(ctx, honest, kSmall, info_.w - 44.f);
            ctx.r.drawText({info_.x + 34.f, info_.y + 28.f}, shown, kSmall, c.text);
            if (shown != honest) addTip(info_, honest);
        }
        // les filtres
        const gfx::Rect fb{0.f, filtersY_, 0.f, 30.f};
        textMid(ctx, scopeLabelX_, fb, "Port\xC3\xA9" "e", kSmall, c.textMuted);
        textMid(ctx, statusLabelX_, fb, "Statut", kSmall, c.textMuted);
        textMid(ctx, projLabelX_, fb, "Projet", kSmall, c.textMuted);
        {
            const auto shown = d_.shownSouvenirs();
            std::size_t pending = 0, conflicts = 0;
            for (const auto* k : shown) {
                pending += k->provisional() ? 1 : 0;
                conflicts += k->conflicted() ? 1 : 0;
            }
            std::string count = plural(shown.size(), "souvenir", "souvenirs");
            if (pending) count += kDot + std::to_string(pending) + " \xC3\xA0 valider";
            if (conflicts) count += kDot + std::to_string(conflicts) + " en conflit";
            drawFit(ctx, {countX_, fb.y + (fb.h - ctx.r.lineHeight(kSmall)) * 0.5f}, count, kSmall, countW_, conflicts ? c.error : c.textMuted);
        }
        paintDetail(ctx);
        paintBackups(ctx);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            if (k->key == ui::Key::Delete && !editorOn && table->focused()) {
                (void)d_.deleteSelectedSouvenir();
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    void paintDetail(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        card(ctx, detail_, c.border);
        const auto* k = d_.selectedSouvenir();
        const float x = detail_.x + 12.f, w = detail_.w - 24.f;
        float y = detail_.y + 12.f;
        if (editorOn) {
            caption(ctx, x, y, ui::Icon::Rename, "MODIFIER LE SOUVENIR", c.textMuted);
            ctx.r.drawText({x, detail_.y + 34.f}, "Titre", kSmall, c.textMuted);
            drawFit(ctx, {x, detail_.y + 92.f}, "Contenu \xE2\x80\x94 ce qui sera relu et envoy\xC3\xA9 avec les demandes", kSmall, w, c.textMuted);
            return;
        }
        if (!k) {
            const std::string empty = d_.shownSouvenirs().empty()
                                          ? "Aucun souvenir avec ces filtres. L'Assistant en propose (\xC3\xA0 valider ici) ; l'onglet Apprentissage en ajoute."
                                          : "Choisis un souvenir dans la liste.";
            for (const auto& l : wrap(ctx, empty, kText, w, 3)) {
                ctx.r.drawText({x, y}, l, kText, c.textMuted);
                y += 20.f;
            }
            return;
        }
        ui::drawIcon(ctx.r, k->pinned ? ui::Icon::Pin : k->conflicted() ? ui::Icon::Warning : ui::Icon::Document, {x, y + 1.f, 16.f, 16.f},
                     k->conflicted() ? c.error : c.textMuted);
        {
            const std::string name = fit(ctx, k->name, kText, w - 24.f);
            drawBold(ctx, {x + 24.f, y}, name, kText, c.text);
            if (name != k->name) addTip({x + 24.f, y - 2.f, w - 24.f, ctx.r.lineHeight(kText) + 4.f}, k->name);
        }
        y += 24.f;
        float px = x;
        const auto chip = [&](const std::string& s, gfx::Color ink) {
            const float cw = textW(ctx, s, kSmall) + 16.f;
            if (px + cw > x + w) return;
            const gfx::Rect cr{px, y, cw, 22.f};
            ctx.r.fillRoundedRect(cr, ink.withAlpha(ctx.theme.isDark() ? 40 : 28), 11.f);
            ctx.r.drawText({cr.x + 8.f, cr.y + (cr.h - ctx.r.lineHeight(kSmall)) * 0.5f}, s, kSmall, c.text);
            px += cw + 6.f;
        };
        const auto here = mem::hereState(*k, d_.here());
        chip(mem::statusLabel(k->status), k->status == "confirme" ? c.ok : k->status == "provisoire" ? c.info : c.warning);
        chip(mem::scopeLabel(k->scopeKey()), ctx.theme.onSurface(ctx.theme.brand.family[2]));
        chip(k->project.empty() && k->projectId.empty() ? std::string("Tous les projets") : "Projet " + (k->project.empty() ? k->projectId : k->project),
             ctx.theme.onSurface(ctx.theme.brand.family[4]));
        chip(mem::hereLabel(here), here == mem::Here::Active ? c.ok : here == mem::Here::Inactive ? c.textDisabled : c.warning);
        if (k->conflicted()) chip("En conflit", c.error);
        y += 30.f;
        drawFit(ctx, {x, y}, "Source : " + k->provenance(), kSmall, w, c.text);
        y += 18.f;
        drawFit(ctx, {x, y},
                "Cr\xC3\xA9\xC3\xA9 le " + dateOrDash(k->added) + kDot + "modifi\xC3\xA9 le " + dateOrDash(k->modified) + kDot + "confiance "
                    + std::to_string(static_cast<int>(k->confidence * 100.0 + 0.5)) + " %",
                kSmall, w, c.textMuted);
        y += 20.f;
        if (here == mem::Here::Pending) {
            drawFit(ctx, {x, y}, "Propos\xC3\xA9 par l'Assistant : il ne part pas avec les demandes tant qu'il n'est pas valid\xC3\xA9.", kSmall, w,
                    ctx.theme.onSurface(c.info));
            y += 18.f;
        } else if (here == mem::Here::Unbound) {
            drawFit(ctx, {x, y}, "Li\xC3\xA9 par le nom seul, et ce nom est port\xC3\xA9 par deux projets : associe-le pour qu'il reparte.", kSmall, w,
                    ctx.theme.onSurface(c.warning));
            y += 18.f;
        }
        for (const auto* o : d_.registry_.conflictsOf(k->id)) {
            if (y + 36.f > detail_.bottom() - 150.f) break;
            drawFit(ctx, {x, y}, "En conflit avec \xC2\xAB " + o->name + " \xC2\xBB (" + mem::statusLabel(o->status) + ", " + o->provenance() + ")", kSmall, w,
                    c.error);
            y += 18.f;
        }
        // le contenu, puis l'historique
        const float historyH = k->history.empty() ? 0.f : 22.f + 16.f * static_cast<float>(std::min<std::size_t>(3, k->history.size()));
        const float bottom = detail_.bottom() - 90.f - historyH;
        if (y + 20.f < bottom) {
            const gfx::Rect box{x, y + 2.f, w, bottom - y - 4.f};
            ctx.r.fillRoundedRect(box, c.inputBg, 4.f);
            ctx.r.pushClip(box);
            float ly = box.y + 6.f;
            for (const auto& l : wrap(ctx, k->content, kSmall, box.w - 16.f, 40)) {
                if (ly + 15.f > box.bottom()) break;
                ctx.r.drawText({box.x + 8.f, ly}, l, kSmall, c.text);
                ly += 16.f;
            }
            ctx.r.popClip();
            addTip(box, k->content.size() > 600 ? k->content.substr(0, 600) + kDots : k->content);
        }
        if (historyH > 0.f) {
            float hy = detail_.bottom() - 88.f - historyH + 2.f;
            caption(ctx, x, hy, ui::Icon::History, "HISTORIQUE", c.textMuted);
            hy += 20.f;
            const std::size_t n = std::min<std::size_t>(3, k->history.size());
            for (std::size_t i = k->history.size() - n; i < k->history.size(); ++i) {
                drawFit(ctx, {x, hy}, dateOrDash(k->history[i].date) + kDot + k->history[i].text, kCaption, w, c.textMuted);
                hy += 16.f;
            }
        }
    }

    void paintBackups(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        card(ctx, backups_, c.border);
        ui::drawIcon(ctx.r, ui::Icon::Save, {backups_.x + 10.f, backups_.y + 10.f, 16.f, 16.f}, c.textMuted);
        std::string text;
        if (d_.backups_.empty())
            text = "Aucune sauvegarde de la m\xC3\xA9moire : une copie est faite avant chaque migration et chaque restauration.";
        else
            text = "Sauvegardes : " + std::to_string(d_.backups_.size()) + kDot + "la plus r\xC3\xA9" "cente du " + dateOrDash(d_.backups_.front().date)
                   + " (" + d_.backups_.front().reason + ")";
        const std::string shown = fit(ctx, text, kSmall, backupsTextW_);
        ctx.r.drawText({backups_.x + 34.f, backups_.y + (backups_.h - ctx.r.lineHeight(kSmall)) * 0.5f}, shown, kSmall, c.textMuted);
        if (shown != text) addTip(backups_, text);
    }

    AiMemoryDialog& d_;
    std::vector<Tip> tips_;
    gfx::Rect info_{}, detail_{}, backups_{};
    float filtersY_{0}, scopeLabelX_{0}, statusLabelX_{0}, projLabelX_{0}, countX_{0}, countW_{0}, backupsTextW_{0};
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
    // 1.21.0 : les sauvegardes de la memoire, les projets connus (leur identifiant)
    backups_ = mem::listBackups();
    projects_ = mem::knownProjects();
    if (!selectedSouvenir_.empty() && !registry_.find(selectedSouvenir_)) selectedSouvenir_.clear();
    // la zone rouge : une categorie devenue vide se decoche (elle se grise, syncWidgets)
    persistentOn_.erase(std::remove_if(persistentOn_.begin(), persistentOn_.end(), [&](mem::Persistent p) { return persistentEmpty(p); }),
                        persistentOn_.end());
    hits_ = search_.empty() ? std::vector<mem::Hit>{} : mem::search(search_, conversations_, registry_.all());
    if (!selectedKnowledge_.empty() && !registry_.find(selectedKnowledge_)) selectedKnowledge_.clear();
    rebuildTables();
    syncWidgets();
    if (hub_) hub_->refreshData();
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
        case Tab::Souvenirs: return "Souvenirs";
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


// ---- Apprentissage : le tableau de bord (1.17.0) -------------------------------------------------
void AiMemoryDialog::setLearningSearchQuiet(std::string text) {
    if (text == learnSearch_) return;
    learnSearch_ = std::move(text);
    rebuildTables();
    syncWidgets();
}
bool AiMemoryDialog::showLearningTheme(std::string_view key) {
    showTab(Tab::Learning);
    return hub_ && hub_->showTheme(std::string(key));
}
std::string AiMemoryDialog::learningTheme() const { return hub_ ? hub_->theme : std::string{}; }
void AiMemoryDialog::setLearningSearch(std::string text) {
    if (hub_ && hub_->search) hub_->search->setText(text);   // textChanged suit (la liste, les connaissances)
    setLearningSearchQuiet(std::move(text));
    if (hub_) hub_->fillList();
}
std::size_t AiMemoryDialog::learningRowCount() const {
    if (!hub_) return 0;
    return hub_->textTheme() ? shownKnowledge().size() : hub_->listModel->rows.size();
}
bool AiMemoryDialog::selectLearningRow(std::string_view key) { return hub_ && hub_->selectRow(key); }
bool AiMemoryDialog::beginLearningNew() {
    if (!hub_ || (hub_->theme != "modeles" && hub_->theme != "vocabulaire")) return false;
    hub_->beginNew();
    return true;
}
bool AiMemoryDialog::saveLearningDetail() { return hub_ && hub_->saveDetail(); }
bool AiMemoryDialog::revertLearningDetail() { return hub_ && hub_->revertDetail(); }
bool AiMemoryDialog::toggleLearningDetail() { return hub_ && hub_->toggleDetail(); }
bool AiMemoryDialog::removeLearningDetail() { return hub_ && hub_->removeDetail(); }
bool AiMemoryDialog::teachLearning(std::string_view word, std::string_view meaning, std::string_view target) {
    return hub_ && hub_->teach(word, meaning, target);
}
bool AiMemoryDialog::ignoreLearningRequest() { return hub_ && hub_->ignoreSelected(); }
bool AiMemoryDialog::tryLearningPhrase(const std::string& phrase) {
    if (!hub_) return false;
    if (hub_->tryIn) hub_->tryIn->setText(phrase);
    return hub_->tryNow(phrase);
}
std::string AiMemoryDialog::learningTrial() const {
    if (!hub_ || hub_->lastTry.first.empty()) return {};
    return (hub_->lastTry.second.understood ? "Comprise : " : "Pas comprise : ") + hub_->lastTry.second.summary;
}
bool AiMemoryDialog::exportLearning(const std::string& path) { return hub_ && hub_->exportTo(path); }
bool AiMemoryDialog::importLearning(const std::string& path) { return hub_ && hub_->previewImport(path); }

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
            const auto here = mem::hereState(*k, this->here());
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
                const auto here = mem::hereState(k, this->here());
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
    rebuildSouvenirs();
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
    syncSouvenirs();
    if (yes_ && no_) {
        const auto vis = confirming() ? ui::Visibility::Visible : ui::Visibility::Collapsed;
        yes_->setVisibility(vis);
        no_->setVisibility(vis);
        if (confirming()) yes_->setText(confirm_.yes);
        // 1.17.0 : importer n'efface rien - le bouton n'est pas rouge
        yes_->setStyle(confirm_.kind == Confirm::ImportLearning || confirm_.kind == Confirm::RestoreBackup ? ui::Button::Style::Primary
                                                                                                          : ui::Button::Style::Danger);
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
        if (!learnSearch_.empty() && folded(k.name + " " + k.description + " " + k.content).find(folded(learnSearch_)) == std::string::npos) continue;
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
    if (hub_) hub_->followKnowledge(found->category);
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
    // 1.21.0 : pour le projet ouvert, liee par son identifiant (jamais melangee avec un autre projet du meme nom)
    if (k.projectId.empty() && !k.project.empty() && k.project == spec_.project) k.projectId = spec_.projectId;
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
    // 1.21.0 : ce que l'editeur montre, sur la connaissance telle qu'elle est (son statut, sa portee, son epingle, son
    // historique restent) ; un projet change : lie a l'identifiant du projet ouvert quand c'est son nom.
    mem::Knowledge next = *k;
    next.name = changes.name;
    next.description = changes.description;
    next.category = changes.category.empty() ? k->category : changes.category;
    next.content = changes.content;
    next.active = changes.active;
    if (changes.project != k->project) {
        next.project = changes.project;
        next.projectId = !spec_.projectId.empty() && changes.project == spec_.project ? spec_.projectId : std::string{};
        next.scope.clear();   // deduite de nouveau (un projet : portee projet)
        if (k->scopeKey() == "conversation") next.scope = "conversation";
    }
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
    if (hub_) hub_->followKnowledge(catFilter_);
    editorAdding_ = true;
    fillEditor(nullptr, false);
}

void AiMemoryDialog::beginEdit() {
    const auto* k = selectedKnowledge();
    if (!k || k->builtIn) return;
    showTab(Tab::Learning);
    if (hub_) hub_->followKnowledge(k->category);
    editorAdding_ = false;
    fillEditor(k, false);
}

void AiMemoryDialog::beginView() {
    const auto* k = selectedKnowledge();
    if (!k) return;
    showTab(Tab::Learning);
    if (hub_) hub_->followKnowledge(k->category);
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
        case Confirm::ImportLearning: ok = hub_ && hub_->doImport(); break;
        case Confirm::DeleteSouvenir: ok = doDeleteSouvenir(); break;
        case Confirm::RestoreBackup: ok = doRestoreBackup(); break;
        case Confirm::None: break;
    }
    syncWidgets();
    if (body_) body_->invalidateLayout();
    return ok;
}

void AiMemoryDialog::confirmNo() {
    const bool cache = confirm_.kind == Confirm::ClearCache;
    const bool import = confirm_.kind == Confirm::ImportLearning;
    const bool restore = confirm_.kind == Confirm::RestoreBackup;
    confirm_ = {};
    if (import && hub_) hub_->pendingImport.clear();
    say(import ? "Rien n'a \xC3\xA9t\xC3\xA9 import\xC3\xA9." : restore ? "Rien n'a \xC3\xA9t\xC3\xA9 restaur\xC3\xA9." : cache ? "Le cache n'a pas \xC3\xA9t\xC3\xA9 vid\xC3\xA9." : "Rien n'a \xC3\xA9t\xC3\xA9 supprim\xC3\xA9.");
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
        case Tab::Learning:
            if (hub_ && !hub_->textTheme()) return hub_->list;
            return learning_ && !learning_->editorOn ? learning_->table : nullptr;
        case Tab::Memory: return memory_ ? memory_->table : nullptr;
        case Tab::Souvenirs: return souvenirs_ && !souvenirEditing() ? souvenirsTable() : nullptr;
        case Tab::Storage: return storage_ ? storage_->table : nullptr;
        case Tab::Activity: return activityPage_ ? activityPage_->table : nullptr;
        default: return nullptr;
    }
}

bool AiMemoryDialog::selectRow(std::string_view text) {
    switch (tab()) {
        case Tab::Learning: return hub_ && !hub_->textTheme() ? selectLearningRow(text) : selectKnowledge(text);
        case Tab::Memory: return selectMemoryRow(text);
        case Tab::Souvenirs: return selectSouvenir(text);
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

// ---- Souvenirs (1.21.0) -----------------------------------------------------------------------
mem::Context AiMemoryDialog::here() const {
    mem::Context c;
    c.projectId = spec_.projectId;
    c.projectName = spec_.project;
    c.nameAmbiguous = spec_.projectAmbiguous;
    c.conversationId = spec_.conversationId;
    return c;
}

ui::TableView* AiMemoryDialog::souvenirsTable() const { return souvenirs_ ? souvenirs_->table : nullptr; }

std::vector<const mem::Knowledge*> AiMemoryDialog::shownSouvenirs() const {
    std::vector<const mem::Knowledge*> out;
    const ui::SearchQuery q(souvSearch_);
    for (const auto& k : registry_.all()) {
        if (k.builtIn) continue;
        if (!souvScope_.empty() && k.scopeKey() != souvScope_) continue;
        if (souvStatus_ == "conflits") {
            if (!k.conflicted()) continue;
        } else if (!souvStatus_.empty() && k.status != souvStatus_) {
            continue;
        }
        if (souvProject_ == "ici") {
            const bool mine = (!k.projectId.empty() && !spec_.projectId.empty() && k.projectId == spec_.projectId)
                              || (k.projectId.empty() && !k.project.empty() && k.project == spec_.project);
            if (!mine) continue;
        } else if (souvProject_ == "*") {
            if (!k.projectId.empty() || !k.project.empty()) continue;
        } else if (!souvProject_.empty() && k.projectId != souvProject_) {
            continue;
        }
        if (!q.empty() && !q.matches({k.name, k.content, k.description, k.subject, k.project})) continue;
        out.push_back(&k);
    }
    // les epingles, puis les conflits, puis les propositions a valider ; les plus recents d'abord
    std::stable_sort(out.begin(), out.end(), [](const mem::Knowledge* a, const mem::Knowledge* b) {
        const auto rank = [](const mem::Knowledge* k) { return k->pinned ? 0 : k->conflicted() ? 1 : k->provisional() ? 2 : 3; };
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return a->modified > b->modified;
    });
    return out;
}

void AiMemoryDialog::rebuildSouvenirs() {
    if (!souvenirs_) return;
    auto& m = *souvenirs_->model;
    m.rows.clear();
    const auto shown = shownSouvenirs();
    if (!selectedSouvenir_.empty() && std::none_of(shown.begin(), shown.end(), [&](const mem::Knowledge* k) { return k->id == selectedSouvenir_; }))
        selectedSouvenir_ = shown.empty() ? std::string{} : shown.front()->id;
    for (const auto* k : shown) {
        Rows::Row row;
        row.key = k->id;
        const std::string project = !k->project.empty() ? k->project : !k->projectId.empty() ? k->projectId : std::string("Tous");
        row.cells = {k->name, mem::scopeLabel(k->scopeKey()), mem::statusLabel(k->status) + (k->conflicted() ? kDot + "conflit" : std::string{}), project,
                     dateOrDash(k->modified).substr(0, 10)};
        row.styles.resize(5);
        row.styles[0].icon = k->pinned ? ui::Icon::Pin : k->conflicted() ? ui::Icon::Warning : k->provisional() ? ui::Icon::Assistant : ui::Icon::Document;
        row.styles[0].iconTone = k->conflicted() ? ui::Tone::Error : k->provisional() ? ui::Tone::Info : ui::Tone::Muted;
        row.styles[2] = toneStyle(k->conflicted() ? ui::Tone::Error : k->status == "confirme" ? ui::Tone::Ok : k->status == "provisoire" ? ui::Tone::Info : ui::Tone::Warning);
        row.tip = k->name + "\n" + k->provenance() + "\n" + (k->content.size() > 300 ? k->content.substr(0, 300) + kDots : k->content);
        m.rows.push_back(std::move(row));
    }
    m.reset();
    selectKey(souvenirs_->table, m.find(selectedSouvenir_));
    souvenirs_->invalidate();
}

void AiMemoryDialog::syncSouvenirs() {
    if (!souvenirs_) return;
    auto& p = *souvenirs_;
    if (p.search->text() != souvSearch_) p.search->setText(souvSearch_);
    const auto select = [](ui::DropDown* box, const std::string& value) {
        int sel = 0;
        const auto& items = box->items();
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].value == value) sel = static_cast<int>(i);
        box->setSelectedIndex(sel);
    };
    select(p.scopeBox, souvScope_);
    select(p.statusBox, souvStatus_);
    {
        std::vector<ui::DropDown::Item> items{{"Tous les projets", "", {}, true},
                                              {"Ce projet" + (spec_.project.empty() ? std::string(" (aucun ouvert)") : " (" + spec_.project + ")"), "ici", {}, true},
                                              {"Communs \xC3\xA0 tous les projets", "*", {}, true}};
        for (const auto& pr : projects_)
            items.push_back({"Projet " + pr.name + (pr.ambiguous ? " (" + pr.path + ")" : std::string{}), pr.id, {}, true});
        p.projBox->setItems(std::move(items));
        select(p.projBox, souvProject_);
    }
    const auto* k = selectedSouvenir();
    const bool any = k != nullptr && !p.editorOn;
    const auto vis = [&](ui::Widget* w, bool on) { w->setVisibility(on && !p.editorOn ? ui::Visibility::Visible : ui::Visibility::Collapsed); };
    const bool provisional = k && k->provisional();
    vis(p.confirm, true);
    vis(p.hypothesis, true);
    vis(p.pin, true);
    vis(p.keep, k && k->conflicted());
    vis(p.associate, true);
    vis(p.edit, true);
    vis(p.reject, provisional);
    vis(p.remove, !provisional);
    p.confirm->setText(provisional ? "Valider" : "Confirmer");
    p.confirm->setEnabled(any && k->status != "confirme");
    p.confirm->setTooltip(provisional ? "Valider la proposition de l'Assistant : elle partira avec les demandes." : "Marquer l'information comme confirm\xC3\xA9" "e.");
    p.hypothesis->setText(provisional ? "Garder comme hypoth\xC3\xA8se" : "Hypoth\xC3\xA8se");
    p.hypothesis->setEnabled(any && k->status != "hypothese");
    p.hypothesis->setTooltip("Une hypoth\xC3\xA8se part avec les demandes, dite comme telle (pas comme un fait).");
    p.pin->setText(k && k->pinned ? "D\xC3\xA9s\xC3\xA9pingler" : "\xC3\x89pingler");
    p.pin->setEnabled(any);
    p.pin->setTooltip("Un souvenir \xC3\xA9pingl\xC3\xA9 part en premier avec les demandes.");
    p.keep->setEnabled(any);
    p.keep->setTooltip("Garder celui-ci (confirm\xC3\xA9) ; ceux qui le contredisent sont d\xC3\xA9sactiv\xC3\xA9s, gard\xC3\xA9s pour m\xC3\xA9moire.");
    const bool here = k && !spec_.projectId.empty() && k->projectId == spec_.projectId;
    p.associate->setText(here ? "\xC3\x80 tous les projets" : "Associer au projet ouvert");
    p.associate->setEnabled(any && (here || !spec_.projectId.empty()));
    p.associate->setTooltip(here ? "Le d\xC3\xA9lier de ce projet : il vaudra pour tous les projets."
                            : spec_.projectId.empty() ? "Aucun projet enregistr\xC3\xA9 n'est ouvert : rien \xC3\xA0 quoi l'associer."
                                                      : "L'associer \xC3\xA0 " + spec_.project + " (par son identifiant) : il ne partira qu'avec les demandes de ce projet.");
    p.edit->setEnabled(any);
    p.reject->setEnabled(any);
    p.reject->setTooltip("Refuser la proposition de l'Assistant : elle est supprim\xC3\xA9" "e.");
    p.remove->setEnabled(any);
    p.restore->setEnabled(!backups_.empty());
    p.backup->setEnabled(!measure_.root.empty());
    souvenirs_->invalidateLayout();
    souvenirs_->invalidate();
}

void AiMemoryDialog::setSouvenirSearch(std::string text) {
    souvSearch_ = std::move(text);
    rebuildSouvenirs();
    syncWidgets();
}

void AiMemoryDialog::setSouvenirFilter(std::string scope, std::string status, std::string project) {
    souvScope_ = std::move(scope);
    souvStatus_ = std::move(status);
    souvProject_ = std::move(project);
    rebuildSouvenirs();
    syncWidgets();
}

bool AiMemoryDialog::selectSouvenir(std::string_view idOrName) {
    const std::string want = folded(idOrName);
    const auto shown = shownSouvenirs();
    const mem::Knowledge* hit = nullptr;
    for (const auto* k : shown)
        if (k->id == idOrName) hit = k;
    if (!hit)
        for (const auto* k : shown)
            if (!hit && (folded(k->name) == want || folded(k->name).rfind(want, 0) == 0)) hit = k;
    if (!hit) return false;
    selectedSouvenir_ = hit->id;
    if (souvenirs_) selectKey(souvenirs_->table, souvenirs_->model->find(selectedSouvenir_));
    syncWidgets();
    return true;
}

const mem::Knowledge* AiMemoryDialog::selectedSouvenir() const {
    if (selectedSouvenir_.empty()) return nullptr;
    const auto* k = registry_.find(selectedSouvenir_);
    return k && !k->builtIn ? k : nullptr;
}

bool AiMemoryDialog::pinSelected(bool on) {
    const auto* k = selectedSouvenir();
    std::string why;
    if (!k || !registry_.setPinned(k->id, on, &why)) {
        say(k ? "\xC3\x89pingler : " + why : std::string("Choisis d'abord un souvenir."), true);
        return false;
    }
    const std::string name = k->name;
    refresh();
    say(std::string(on ? "\xC3\x89pingl\xC3\xA9 : " : "D\xC3\xA9s\xC3\xA9pingl\xC3\xA9 : ") + name + (on ? " (il part en premier avec les demandes)." : "."));
    return true;
}

bool AiMemoryDialog::setSelectedStatus(std::string_view status) {
    const auto* k = selectedSouvenir();
    std::string why;
    if (!k || !registry_.setStatus(k->id, status, &why)) {
        say(k ? why : std::string("Choisis d'abord un souvenir."), true);
        return false;
    }
    const std::string name = k->name;
    refresh();
    say(name + " : " + mem::statusLabel(status) + ".");
    return true;
}

bool AiMemoryDialog::associateSelectedHere() {
    const auto* k = selectedSouvenir();
    if (!k) return false;
    if (spec_.projectId.empty()) {
        say("Aucun projet enregistr\xC3\xA9 n'est ouvert : enregistre le projet pour lui associer des souvenirs.", true);
        return false;
    }
    std::string why;
    if (!registry_.associate(k->id, spec_.projectId, spec_.project, &why)) {
        say(why, true);
        return false;
    }
    const std::string name = k->name;
    refresh();
    say(name + " : associ\xC3\xA9 au projet " + spec_.project + " (il ne part qu'avec ses demandes).");
    return true;
}

bool AiMemoryDialog::associateSelectedAll() {
    const auto* k = selectedSouvenir();
    std::string why;
    if (!k || !registry_.associate(k->id, {}, {}, &why)) {
        if (k) say(why, true);
        return false;
    }
    const std::string name = k->name;
    refresh();
    say(name + " : vaut maintenant pour tous les projets.");
    return true;
}

bool AiMemoryDialog::keepSelectedInConflict() {
    const auto* k = selectedSouvenir();
    if (!k || !k->conflicted()) return false;
    const std::string keepId = k->id, name = k->name;
    const std::vector<std::string> others = k->conflicts;
    std::string why;
    for (const auto& o : others)
        if (registry_.find(o) && !registry_.resolveConflict(keepId, o, false, &why)) {
            say(why, true);
            refresh();
            return false;
        }
    refresh();
    say("Gard\xC3\xA9 : " + name + " ; " + plural(others.size(), "souvenir qui le contredisait d\xC3\xA9sactiv\xC3\xA9", "souvenirs qui le contredisaient d\xC3\xA9sactiv\xC3\xA9s") + ".");
    return true;
}

bool AiMemoryDialog::rejectSelected() {
    const auto* k = selectedSouvenir();
    if (!k || !k->provisional()) return false;
    const std::string name = k->name;
    std::string why;
    if (!mem::rejectMemory(registry_, k->id, &why)) {
        say(why, true);
        return false;
    }
    selectedSouvenir_.clear();
    refresh();
    say("Proposition refus\xC3\xA9" "e : " + name + ".");
    return true;
}

bool AiMemoryDialog::editSelectedSouvenir(const std::string& name, const std::string& content) {
    const auto* k = selectedSouvenir();
    if (!k) return false;
    mem::Knowledge next = *k;
    next.name = name;
    next.content = content;
    std::string why;
    if (!registry_.update(next, &why)) {
        say(why, true);
        return false;
    }
    refresh();
    say("Souvenir modifi\xC3\xA9 : " + name + ".");
    return true;
}

void AiMemoryDialog::beginSouvenirEdit() {
    const auto* k = selectedSouvenir();
    if (!souvenirs_ || !k) return;
    souvenirs_->edName->setText(k->name);
    souvenirs_->edContent->setText(k->content);
    souvenirs_->showEditor(true);
    syncWidgets();
}

bool AiMemoryDialog::saveSouvenirEdit() {
    if (!souvenirs_ || !souvenirs_->editorOn) return false;
    const std::string name = souvenirs_->edName->text(), content = souvenirs_->edContent->text();
    if (name.empty() || content.find_first_not_of(" \t\r\n") == std::string::npos) {
        say("Un souvenir a un titre et un contenu.", true);
        return false;
    }
    souvenirs_->showEditor(false);
    const bool ok = editSelectedSouvenir(name, content);
    syncWidgets();
    return ok;
}

void AiMemoryDialog::closeSouvenirEdit() {
    if (!souvenirs_) return;
    souvenirs_->showEditor(false);
    syncWidgets();
}

bool AiMemoryDialog::souvenirEditing() const noexcept { return souvenirs_ && souvenirs_->editorOn; }

bool AiMemoryDialog::deleteSelectedSouvenir(bool now) {
    const auto* k = selectedSouvenir();
    if (!k) return false;
    if (now) return doDeleteSouvenir();
    ask(Confirm::DeleteSouvenir,
        "Supprimer le souvenir \xC2\xAB " + k->name + " \xC2\xBB ? Il ne partira plus avec les demandes" + (backups_.empty() ? std::string(".") : " (les sauvegardes le gardent)."),
        "Oui, supprimer");
    return true;
}

bool AiMemoryDialog::doDeleteSouvenir() {
    const auto* k = selectedSouvenir();
    if (!k) return false;
    const std::string name = k->name;
    std::string why;
    if (!registry_.remove(k->id, &why)) {
        say(why, true);
        return false;
    }
    selectedSouvenir_.clear();
    refresh();
    say("Souvenir supprim\xC3\xA9 : " + name + ".");
    return true;
}

bool AiMemoryDialog::backupNow() {
    std::string name, why;
    if (!mem::backupMemory("\xC3\xA0 la demande", &name, &why)) {
        say("Sauvegarde impossible : " + why, true);
        return false;
    }
    refresh();
    say("M\xC3\xA9moire sauvegard\xC3\xA9" "e : " + name + ".");
    return true;
}

bool AiMemoryDialog::restoreLatestBackup(bool now) {
    if (backups_.empty()) {
        say("Aucune sauvegarde \xC3\xA0 restaurer.", true);
        return false;
    }
    if (now) return doRestoreBackup();
    ask(Confirm::RestoreBackup,
        "Restaurer la m\xC3\xA9moire telle qu'elle \xC3\xA9tait le " + dateOrDash(backups_.front().date) + " (" + backups_.front().reason
            + ") ? L'\xC3\xA9tat actuel est sauvegard\xC3\xA9 d'abord.",
        "Oui, restaurer");
    return true;
}

bool AiMemoryDialog::doRestoreBackup() {
    if (backups_.empty()) return false;
    const std::string name = backups_.front().name;
    std::string why;
    if (!mem::restoreBackup(name, &why)) {
        say("Restauration impossible : " + why, true);
        return false;
    }
    refresh();
    say("M\xC3\xA9moire restaur\xC3\xA9" "e depuis " + name + " (l'\xC3\xA9tat d'avant est sauvegard\xC3\xA9).");
    return true;
}

void AiMemoryDialog::connectSouvenirs() {
    if (!souvenirs_) return;
    auto& p = *souvenirs_;
    links_ += p.search->textChanged->connect([this](const std::string& t) {
        if (syncing_ || t == souvSearch_) return;
        setSouvenirSearch(t);
    });
    const auto value = [](ui::DropDown* box) {
        const auto* it = box->selectedItem();
        return it ? it->value : std::string{};
    };
    links_ += p.scopeBox->selectionChanged->connect([this, value](int) {
        if (!syncing_) setSouvenirFilter(value(souvenirs_->scopeBox), souvStatus_, souvProject_);
    });
    links_ += p.statusBox->selectionChanged->connect([this, value](int) {
        if (!syncing_) setSouvenirFilter(souvScope_, value(souvenirs_->statusBox), souvProject_);
    });
    links_ += p.projBox->selectionChanged->connect([this, value](int) {
        if (!syncing_) setSouvenirFilter(souvScope_, souvStatus_, value(souvenirs_->projBox));
    });
    links_ += p.table->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (syncing_) return;
        const auto& m = *souvenirs_->model;
        selectedSouvenir_ = !rows.empty() && rows.front() < m.rows.size() ? m.rows[rows.front()].key : std::string{};
        syncWidgets();
    });
    links_ += p.table->activated->connect([this](ui::RowIndex) { beginSouvenirEdit(); });
    links_ += p.confirm->clicked->connect([this] { (void)setSelectedStatus("confirme"); });
    links_ += p.hypothesis->clicked->connect([this] { (void)setSelectedStatus("hypothese"); });
    links_ += p.pin->clicked->connect([this] {
        const auto* k = selectedSouvenir();
        (void)pinSelected(!(k && k->pinned));
    });
    links_ += p.keep->clicked->connect([this] { (void)keepSelectedInConflict(); });
    links_ += p.associate->clicked->connect([this] {
        const auto* k = selectedSouvenir();
        if (k && !spec_.projectId.empty() && k->projectId == spec_.projectId) (void)associateSelectedAll();
        else (void)associateSelectedHere();
    });
    links_ += p.edit->clicked->connect([this] { beginSouvenirEdit(); });
    links_ += p.reject->clicked->connect([this] { (void)rejectSelected(); });
    links_ += p.remove->clicked->connect([this] { (void)deleteSelectedSouvenir(); });
    links_ += p.backup->clicked->connect([this] { (void)backupNow(); });
    links_ += p.restore->clicked->connect([this] { (void)restoreLatestBackup(); });
    links_ += p.edSave->clicked->connect([this] { (void)saveSouvenirEdit(); });
    links_ += p.edCancel->clicked->connect([this] { closeSouvenirEdit(); });
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
        auto p = std::make_unique<LearningHub>(*this);   // 1.17.0 : le tableau de bord (la page des connaissances dedans)
        hub_ = p.get();
        learning_ = p->knowledge;
        tabs->addTab(ui::TabControl::Tab{tabTitle(Tab::Learning), ui::Icon::Library}, std::move(p));
    }
    {
        auto p = std::make_unique<SouvenirsPage>(*this);   // 1.21.0 : le gestionnaire de la memoire persistante
        souvenirs_ = p.get();
        tabs->addTab(ui::TabControl::Tab{tabTitle(Tab::Souvenirs), ui::Icon::Pin}, std::move(p));
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
    // ---- Souvenirs (1.21.0)
    connectSouvenirs();
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
            else if (souvenirEditing() && tab() == Tab::Souvenirs) closeSouvenirEdit();
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
