// =============================================================================
//  app/SimCenterPanes.cpp - lot API 8 : Simulation > Forcages, Courbes, Journal
// =============================================================================
#include "SimCenter.hpp"
#include "SimCenterKit.hpp"

#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace app {

using namespace simkit;
namespace ss = simstatus;
using ui::RowIndex;

namespace {

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// Les couleurs des courbes : ni rouge, ni ambre, ni vert d'etat.
gfx::Color seriesColour(const ui::PaintContext& ctx, std::size_t i) {
    const auto& b = ctx.theme.brand;
    switch (i % 8) {
        case 0: return ctx.theme.color.accent;
        case 1: return b.family[1];
        case 2: return b.family[4];
        case 3: return ctx.theme.color.info;
        case 4: return b.family[0];
        case 5: return b.family[2];
        case 6: return b.family[5];
        default: return b.family[3];
    }
}

std::string decimalText(double v) {
    if (std::isnan(v)) return "\xE2\x80\x94";
    char b[48];
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e12) std::snprintf(b, sizeof b, "%.0f", v);
    else std::snprintf(b, sizeof b, "%.4g", v);
    std::string s = b;
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

} // namespace

// ============================================================= les forcages ====
class SimForcingPane::Model final : public ui::ITableModel {
public:
    // Lot API 8 (le moteur) : CProgram, "Le programme dirait" (SimForcing::programSays).
    enum Col : std::size_t { CSource, CWhat, CValue, CProgram, CSince, CWho, CCount };
    explicit Model(const SimForcingPane& pane) : pane_(pane) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows().size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Source", "Variable", "Forc\xC3\xA9" "e \xC3\xA0", "Le programme dirait", "Depuis", "Par"};   // les mots de la maquette
        return c < CCount ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= rows().size()) return {};
        const auto& f = rows()[r];
        switch (c) {
            case CSource: return f.where;
            case CWhat:   return f.what;
            case CValue:  return f.value;
            case CProgram: return f.programSays;
            case CSince:  return f.sinceText;
            case CWho:    return f.who;
            default:      return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= rows().size()) return s;
        const auto& f = rows()[r];
        if (c == CSource) {
            s.icon = f.source == SimForcing::Source::Automate ? ui::Icon::Cpu : f.source == SimForcing::Source::Ihm ? ui::Icon::Screen : ui::Icon::Network;
            s.iconTone = ui::Tone::Warning;
        } else if (c == CValue) {
            s.bold = true;
            s.fgTone = ui::Tone::Warning;
            s.icon = ui::Icon::Force;
            s.iconTone = ui::Tone::Warning;
        } else if (c == CProgram) {
            // Lot API 8 : le programme dit autre chose que le forcage - ca se voit.
            const bool differs = !f.programSays.empty() && f.programSays != f.value;
            s.fgTone = differs ? ui::Tone::Info : ui::Tone::Muted;
            s.bold = differs;
        } else if (c == CSince || c == CWho) {
            s.fgTone = ui::Tone::Muted;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t c) const override {
        const auto& v = rows();
        if (a >= v.size() || b >= v.size()) return a < b;
        if (c == CSince) return v[a].since > v[b].since;      // le plus ancien d'abord
        return lowerAscii(cellText(a, c)) < lowerAscii(cellText(b, c));
    }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= rows().size()) return {};
        const auto& f = rows()[r];
        std::string out = f.what + " forc\xC3\xA9" "e \xC3\xA0 " + f.value;
        if (!f.sinceText.empty()) out += ", depuis " + f.sinceText;
        if (!f.who.empty()) out += ", par " + f.who;
        out += ".\nLe programme n'y \xC3\xA9" "crit plus : elle relit toujours cette valeur. Double-clic (ou Rel\xC3\xA2" "cher) : la rendre au programme.";
        if (!f.programSays.empty())   // lot API 8 (le moteur)
            out += "\nSans le for\xC3\xA7" "age, le programme dirait " + f.programSays + " (ce qu'elle prendra une fois rel\xC3\xA2" "ch\xC3\xA9" "e).";
        return out;
    }
    [[nodiscard]] const std::vector<SimForcing>& rows() const noexcept {
        static const std::vector<SimForcing> none;
        return pane_.model_ ? pane_.model_->forcings : none;
    }

private:
    const SimForcingPane& pane_;
};

SimForcingPane::SimForcingPane(std::string id, std::shared_ptr<SimCenterModel> model, SimCenterHosts hosts)
    : ui::Widget(std::move(id)), model_(std::move(model)), hosts_(std::move(hosts)) {
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".table")));
    tableModel_ = std::make_shared<Model>(*this);
    table_->setModel(tableModel_);
    std::vector<ui::TableView::Column> cols(Model::CCount);
    const float widths[] = {170.f, 340.f, 150.f, 170.f, 130.f, 220.f};   // lot API 8 : + le programme dirait
    for (std::size_t i = 0; i < cols.size(); ++i) {
        cols[i].title = tableModel_->headerText(i);
        cols[i].width = widths[i];
    }
    table_->setColumns(std::move(cols));
    table_->setSelectionMode(ui::SelectionMode::Extended);
    table_->setTooltip("Tous les for\xC3\xA7" "ages de la simulation : l'automate, ceux pos\xC3\xA9s depuis l'IHM, les cases des esclaves simul\xC3\xA9s. "
                       "Double-clic : rel\xC3\xA2" "cher ; Ctrl+clic : en choisir plusieurs.");
    links_ += table_->activated->connect([this](RowIndex row) {
        const auto& rows = tableModel_->rows();
        if (row < rows.size() && hosts_.release) hosts_.release({rows[row].key});
    });
    refresh();
}

SimForcingPane::~SimForcingPane() = default;

std::size_t SimForcingPane::rowCount() const noexcept { return model_ ? model_->forcings.size() : 0; }

void SimForcingPane::refresh() {
    if (!model_) return;
    if (model_->forcingRevision == shown_) return;
    shown_ = model_->forcingRevision;
    // La selection suit les cles (les lignes changent d'ordre et de nombre).
    std::vector<std::string> keep;
    for (const auto r : table_->selectedModelRows())
        if (r < tableModel_->rows().size()) keep.push_back(tableModel_->rows()[r].key);
    tableModel_->modelReset->emit();
    std::vector<RowIndex> again;
    for (std::size_t i = 0; i < model_->forcings.size(); ++i)
        if (std::find(keep.begin(), keep.end(), model_->forcings[i].key) != keep.end()) again.push_back(static_cast<RowIndex>(i));
    table_->selectModelRows(std::move(again), false);
    table_->setVisibility(model_->forcings.empty() ? ui::Visibility::Hidden : ui::Visibility::Visible);
    invalidateLayout();
    invalidate();
}

void SimForcingPane::releaseSelected() {
    std::vector<std::string> keys;
    for (const auto r : table_->selectedModelRows())
        if (r < tableModel_->rows().size()) keys.push_back(tableModel_->rows()[r].key);
    if (keys.empty()) {
        if (hosts_.status) hosts_.status("Choisis d'abord une ligne (ou Tout rel\xC3\xA2" "cher).");
        return;
    }
    if (hosts_.release) hosts_.release(keys);
}

void SimForcingPane::releaseAll() {
    std::vector<std::string> keys;
    if (model_)
        for (const auto& f : model_->forcings) keys.push_back(f.key);
    if (keys.empty()) {
        if (hosts_.status) hosts_.status("Aucun for\xC3\xA7" "age \xC3\xA0 rel\xC3\xA2" "cher.");
        return;
    }
    if (hosts_.release) hosts_.release(keys);
}

bool SimForcingPane::select(std::string_view what) {
    const auto want = lowerAscii(what);
    const auto& rows = tableModel_->rows();
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (lowerAscii(rows[i].what) == want || lowerAscii(rows[i].what).rfind(want, 0) == 0) {
            table_->selectModelRows({static_cast<RowIndex>(i)});
            return true;
        }
    return false;
}

void SimForcingPane::onLayout() {
    const auto b = bounds();
    const float top = 84.f;
    table_->setBounds({b.x + 16.f, b.y + top, std::max(0.f, b.w - 32.f), std::max(0.f, b.h - top - 12.f)});
}

void SimForcingPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.windowBg);
    if (!model_) return;
    refresh();          // la table avant elle-meme (elle se dessine apres ce volet)
    const auto& list = model_->forcings;
    std::size_t plc = 0, ihm = 0, eq = 0;
    const SimForcing* oldest = nullptr;
    for (const auto& f : list) {
        (f.source == SimForcing::Source::Automate ? plc : f.source == SimForcing::Source::Ihm ? ihm : eq) += 1;
        if (f.since >= 0 && (!oldest || f.since < oldest->since)) oldest = &f;
    }
    const gfx::Rect head{b.x + 16.f, b.y + 12.f, b.w - 32.f, 62.f};
    card(ctx, head, false);
    const auto tone = list.empty() ? c.ok : c.warning;
    led(ctx, head.x + 22.f, head.y + 22.f, 6.f, tone, false);
    const std::string big = list.empty() ? std::string("Aucun for\xC3\xA7" "age")
                                         : ss::plural(list.size(), "variable forc\xC3\xA9" "e", "variables forc\xC3\xA9" "es");
    drawBold(ctx, {head.x + 38.f, head.y + 11.f}, big, gfx::FontId{17}, c.text);
    std::string line = list.empty() ? std::string("Le programme \xC3\xA9" "crit librement dans toutes ses variables ; les esclaves simul\xC3\xA9s r\xC3\xA9pondent ce que dit leur m\xC3\xA9moire.")
                                    : "Automate " + std::to_string(plc) + kMid + "depuis l'IHM " + std::to_string(ihm) + kMid + "esclaves simul\xC3\xA9s " + std::to_string(eq);
    if (oldest && model_->now >= oldest->since)
        line += kMid + std::string("le plus ancien : ") + oldest->what + ", depuis " + ss::duration(model_->now - oldest->since);
    ctx.r.drawText({head.x + 38.f, head.y + 36.f}, fit(ctx, line, kSmall, head.w - 52.f), kSmall, c.textMuted);
    if (list.empty()) {
        const float w = std::min(640.f, b.w - 60.f);
        const float x = b.x + (b.w - w) * 0.5f;
        const float y = b.y + 130.f;
        ui::drawIcon(ctx.r, ui::Icon::Force, {x, y, 24.f, 24.f}, c.textMuted);
        drawBold(ctx, {x + 38.f, y}, "Rien n'est forc\xC3\xA9", gfx::FontId{16}, c.text);
        const auto lines = wrap(ctx,
                                "Forcer, c'est imposer une valeur \xC3\xA0 une variable : le programme n'y \xC3\xA9" "crit plus. On force depuis "
                                "Simulation \xE2\x80\xBA Automate (double-clic sur une valeur), depuis l'IHM en marche (Forcer\xE2\x80\xA6), ou une case d'un "
                                "esclave simul\xC3\xA9 (\xC3\x89quipements \xE2\x80\xBA Valeurs simul\xC3\xA9" "es). Tout se retrouve ici, avec qui l'a pos\xC3\xA9 et depuis quand.",
                                kBody, w - 38.f, 5);
        for (std::size_t k = 0; k < lines.size(); ++k)
            ctx.r.drawText({x + 38.f, y + 28.f + static_cast<float>(k) * 18.f}, lines[k], kBody, c.textMuted);
    }
}

// =============================================================== les courbes ====
SimTrendsPane::SimTrendsPane(std::string id, std::shared_ptr<SimCenterModel> model, SimCenterHosts hosts)
    : ui::Widget(std::move(id)), model_(std::move(model)), hosts_(std::move(hosts)) {
    field_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(this->id() + ".ajouter")));
    field_->setPlaceholder("Ajouter une variable (automate ou IHM) : Armoires[0].ana.PT1.mes, Pompe_Marche\xE2\x80\xA6");
    field_->setTooltip("Un chemin de l'automate (un membre, une case d'un tableau) ou une variable de l'IHM (\"ihm:\" devant, si le nom "
                       "existe des deux c\xC3\xB4t\xC3\xA9s). Entr\xC3\xA9" "e l'ajoute ; huit courbes au plus.");
    if (hosts_.assist) hosts_.assist(*field_);
    links_ += field_->editingDone->connect([this](const std::string&) { addTyped(); });
}

void SimTrendsPane::onLayout() {
    const auto b = bounds();
    field_->setBounds({b.x + 16.f, b.y + 12.f, std::clamp(b.w * 0.36f, 220.f, 460.f), 30.f});
}

void SimTrendsPane::addTyped() {
    std::string path = field_->text();
    while (!path.empty() && path.back() == ' ') path.pop_back();
    while (!path.empty() && path.front() == ' ') path.erase(path.begin());
    if (path.empty()) return;
    std::string why;
    if (hosts_.addTrend && hosts_.addTrend(path, &why)) {
        field_->setText({});
        invalidate();
    } else if (hosts_.status) {
        hosts_.status("Courbe non ajout\xC3\xA9" "e : " + (why.empty() ? path + " est inconnue" : why) + ".");
    }
}

double SimTrendsPane::viewEnd() const {
    if (!model_) return 0.0;
    return model_->frozen() ? model_->frozenAt() : model_->now;
}

double SimTrendsPane::timeAt(float x) const {
    if (plot_.w <= 0.f || !model_) return viewEnd();
    const double w = model_->window();
    return viewEnd() - w + w * static_cast<double>((x - plot_.x) / plot_.w);
}

float SimTrendsPane::xOf(double t) const {
    if (!model_) return plot_.x;
    const double w = model_->window();
    return plot_.x + plot_.w * static_cast<float>((t - (viewEnd() - w)) / w);
}

void SimTrendsPane::setCursor(double secondsAgo) {
    cursorPinned_ = secondsAgo >= 0.0;
    cursor_ = secondsAgo >= 0.0 ? viewEnd() - secondsAgo : -1.0;
    invalidate();
}

std::vector<std::string> SimTrendsPane::readoutAt(double at) const {
    std::vector<std::string> out;
    if (!model_) return out;
    const auto& times = model_->times();
    // L'instant le plus proche.
    std::size_t k = times.size();
    if (!times.empty()) {
        const auto it = std::lower_bound(times.begin(), times.end(), at);
        k = it == times.end() ? times.size() - 1 : static_cast<std::size_t>(it - times.begin());
        if (k > 0 && std::fabs(times[k - 1] - at) < std::fabs(times[k] - at)) --k;
    }
    for (const auto& t : model_->trends()) {
        const double v = k < t.values.size() ? t.values[k] : std::numeric_limits<double>::quiet_NaN();
        out.push_back(t.path + " = " + (t.boolean && !std::isnan(v) ? std::string(v > 0.5 ? "TRUE" : "FALSE") : decimalText(v)));
    }
    return out;
}

std::vector<std::string> SimTrendsPane::cursorReadout() const { return readoutAt(cursor_ >= 0.0 ? cursor_ : viewEnd()); }

gfx::Rect SimTrendsPane::partRect(std::string_view key) const {
    for (const auto& h : hits_)
        if (h.key == key) return h.rect;
    return {};
}

int SimTrendsPane::hitAt(gfx::Point p) const {
    for (std::size_t i = 0; i < hits_.size(); ++i)
        if (hits_[i].rect.contains(p)) return static_cast<int>(i);
    return -1;
}

std::string SimTrendsPane::liveTooltip(gfx::Point mouse) const {
    const int h = hitAt(mouse);
    if (h >= 0) return hits_[static_cast<std::size_t>(h)].tip;
    if (plot_.contains(mouse) && model_ && !model_->trends().empty()) {
        const double at = cursorPinned_ ? cursor_ : timeAt(mouse.x);
        std::string out = "\xE2\x88\x92" + ss::duration(std::max(0.0, viewEnd() - at)) + " :";
        for (const auto& l : readoutAt(at)) out += "\n" + l;
        return out + (cursorPinned_ ? "\nUn clic enl\xC3\xA8ve le curseur." : "\nUn clic pose le curseur.");
    }
    return {};
}

void SimTrendsPane::act(const std::string& key) {
    if (!model_) return;
    if (key.rfind("fenetre:", 0) == 0) {
        model_->setWindow(std::atof(key.c_str() + 8));
    } else if (key == "figer") {
        model_->setFrozen(!model_->frozen(), model_->now);
    } else if (key == "ajouter") {
        addTyped();
    } else if (key.rfind("retirer:", 0) == 0) {
        (void)model_->removeTrend(key.substr(8));
    }
    invalidate();
}

void SimTrendsPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.windowBg);
    hits_.clear();
    if (!hovered()) hover_ = -1;
    if (!model_) return;
    const auto& trends = model_->trends();
    // La barre du volet : Ajouter, la fenetre, Figer, le compte.
    float x = field_->bounds().right() + 8.f;
    const float cy = b.y + 27.f;
    {
        const bool hot = hover_ == static_cast<int>(hits_.size());
        const auto br = button(ctx, x, cy, "Ajouter", hot, true, c.accent, false, kSmall, 30.f);
        hits_.push_back({br, "ajouter", "Ajouter la variable tap\xC3\xA9" "e (Entr\xC3\xA9" "e fait de m\xC3\xAA" "me)"});
        x = br.right() + 18.f;
    }
    {
        const char* const labels[3] = {"30 s", "2 min", "10 min"};
        const double secs[3] = {30.0, 120.0, 600.0};
        float total = 0.f;
        float w[3];
        for (int i = 0; i < 3; ++i) {
            w[i] = textWidth(ctx, labels[i], kSmall) + 20.f;
            total += w[i];
        }
        const gfx::Rect seg{x, cy - 14.f, total + 4.f, 28.f};
        ctx.r.fillRoundedRect(seg, c.border, 6.f);
        ctx.r.fillRoundedRect({seg.x + 1.f, seg.y + 1.f, seg.w - 2.f, seg.h - 2.f}, c.windowBg, 5.f);
        float sx = seg.x + 2.f;
        for (int i = 0; i < 3; ++i) {
            const gfx::Rect cell{sx, seg.y + 2.f, w[i], seg.h - 4.f};
            const bool on = std::fabs(model_->window() - secs[i]) < 0.5;
            const bool hot = hover_ == static_cast<int>(hits_.size());
            hits_.push_back({cell, "fenetre:" + std::to_string(static_cast<int>(secs[i])), std::string("Montrer les ") + labels[i] + " derni\xC3\xA8res"});
            if (on) ctx.r.fillRoundedRect(cell, c.selectionBg, 4.f);
            else if (hot) ctx.r.fillRoundedRect(cell, ctx.theme.brand.hover, 4.f);
            ctx.r.drawText({cell.x + 10.f, cell.y + (cell.h - ctx.r.lineHeight(kSmall)) * 0.5f}, labels[i], kSmall, on ? c.selectionText : c.textMuted);
            sx += w[i];
        }
        x = seg.right() + 12.f;
    }
    {
        const bool hot = hover_ == static_cast<int>(hits_.size());
        const auto br = button(ctx, x, cy, model_->frozen() ? "Reprendre" : "Figer", hot, model_->frozen(), c.warning, false, kSmall, 30.f);
        hits_.push_back({br, "figer", model_->frozen() ? "Les courbes repartent avec le temps (les \xC3\xA9" "chantillons n'ont pas cess\xC3\xA9)"
                                                       : "Arr\xC3\xAAter l'affichage ici pour lire \xC3\xA0 loisir (la mesure continue)"});
        x = br.right() + 14.f;
    }
    const std::string count = std::to_string(trends.size()) + " / " + std::to_string(SimCenterModel::kMaxTrends) + " courbes"
                            + (model_->frozen() ? kMid + std::string("fig\xC3\xA9") : std::string{});
    ctx.r.drawText({std::min(x, b.right() - 16.f - textWidth(ctx, count, kSmall)), cy - ctx.r.lineHeight(kSmall) * 0.5f}, count, kSmall, c.textMuted);

    const float top = b.y + 54.f;
    const gfx::Rect area{b.x + 16.f, top, b.w - 32.f, b.bottom() - top - 30.f};
    if (trends.empty()) {
        plot_ = {};
        card(ctx, area, false);
        const float w = std::min(620.f, area.w - 60.f);
        const float lx = area.x + (area.w - w) * 0.5f, ly = area.y + std::max(30.f, area.h * 0.25f);
        ui::drawIcon(ctx.r, ui::Icon::Chart, {lx, ly, 24.f, 24.f}, c.textMuted);
        drawBold(ctx, {lx + 38.f, ly}, "Aucune courbe", gfx::FontId{16}, c.text);
        const auto lines = wrap(ctx,
                                "Ajoute jusqu'\xC3\xA0 huit variables, de l'automate ou de l'IHM : chacune a sa piste, dans le temps. Les bool\xC3\xA9" "ens "
                                "en marches d'escalier, les nombres \xC3\xA0 leur \xC3\xA9" "chelle. Figer arr\xC3\xAAte l'affichage, un clic pose un curseur "
                                "qui lit toutes les valeurs \xC3\xA0 cet instant, Exporter (CSV) ouvre le tout dans Excel.",
                                kBody, w - 38.f, 5);
        for (std::size_t k = 0; k < lines.size(); ++k)
            ctx.r.drawText({lx + 38.f, ly + 28.f + static_cast<float>(k) * 18.f}, lines[k], kBody, c.textMuted);
        return;
    }
    card(ctx, area, false);
    const float labelW = std::clamp(area.w * 0.24f, 170.f, 280.f);
    plot_ = {area.x + labelW, area.y + 8.f, area.w - labelW - 12.f, area.h - 16.f};
    // Les pistes : un booleen est plus bas qu'un nombre.
    float want = 0.f;
    for (const auto& t : trends) want += t.boolean ? 44.f : 88.f;
    const float scale = want > plot_.h ? plot_.h / want : 1.f;
    const auto& times = model_->times();
    const double end = viewEnd(), begin = end - model_->window();
    // Les instants visibles (et un de chaque cote, pour relier les traits).
    std::size_t k0 = static_cast<std::size_t>(std::lower_bound(times.begin(), times.end(), begin) - times.begin());
    std::size_t k1 = static_cast<std::size_t>(std::upper_bound(times.begin(), times.end(), end) - times.begin());
    if (k0 > 0) --k0;
    if (k1 < times.size()) ++k1;
    // Le quadrillage vertical : toutes les 5 s (30 s), 30 s (2 min), 1 min (10 min).
    const double step = model_->window() <= 30.0 ? 5.0 : model_->window() <= 120.0 ? 30.0 : 60.0;
    for (double t = std::ceil(begin / step) * step; t <= end; t += step) {
        const float gx = xOf(t);
        ctx.r.fillRect({gx, plot_.y, 1.f, plot_.h}, c.border.withAlpha(110));
    }
    const double cursorT = cursor_ >= 0.0 && cursorPinned_ ? cursor_ : -1.0;
    std::size_t cursorK = times.size();
    if (cursorT >= 0.0 && !times.empty()) {
        const auto it = std::lower_bound(times.begin(), times.end(), cursorT);
        cursorK = it == times.end() ? times.size() - 1 : static_cast<std::size_t>(it - times.begin());
    }
    float y = plot_.y;
    for (std::size_t i = 0; i < trends.size(); ++i) {
        const auto& tr = trends[i];
        const float laneH = (tr.boolean ? 44.f : 88.f) * scale;
        const gfx::Rect lane{plot_.x, y, plot_.w, laneH};
        const auto colour = seriesColour(ctx, i);
        if (i > 0) ctx.r.fillRect({area.x + 8.f, y, area.w - 16.f, 1.f}, c.border);
        // L'etiquette : la couleur, le nom, la valeur (au curseur, sinon la derniere).
        ctx.r.fillRoundedRect({area.x + 10.f, y + 6.f, 4.f, std::max(8.f, laneH - 12.f)}, colour, 2.f);
        const float nameW = labelW - 44.f;
        const auto name = fit(ctx, (tr.hmi ? std::string("IHM ") : std::string{}) + tr.path, kSmall, nameW);
        ctx.r.drawText({area.x + 22.f, y + 6.f}, name, kSmall, c.text);
        std::string value = tr.last;
        if (cursorK < times.size() && cursorK < tr.values.size())
            value = tr.boolean && !std::isnan(tr.values[cursorK]) ? (tr.values[cursorK] > 0.5 ? "TRUE" : "FALSE") : decimalText(tr.values[cursorK]);
        if (laneH >= 34.f) drawBold(ctx, {area.x + 22.f, y + 22.f}, fit(ctx, value.empty() ? std::string("\xE2\x80\x94") : value, kBody, nameW), kBody, colour);
        {
            const gfx::Rect xr{area.x + labelW - 30.f, y + 4.f, 18.f, 18.f};
            const bool hot = hover_ == static_cast<int>(hits_.size());
            hits_.push_back({xr, "retirer:" + tr.path, "Retirer " + tr.path + " des courbes"});
            if (hot) ctx.r.fillRoundedRect(xr, ctx.theme.brand.hover, 4.f);
            ctx.r.line({xr.x + 5.f, xr.y + 5.f}, {xr.right() - 5.f, xr.bottom() - 5.f}, c.textMuted, 1.5f);
            ctx.r.line({xr.x + 5.f, xr.bottom() - 5.f}, {xr.right() - 5.f, xr.y + 5.f}, c.textMuted, 1.5f);
        }
        hits_.push_back({{area.x + 8.f, y, labelW - 40.f, laneH}, "piste:" + tr.path, tr.path + (tr.hmi ? " (IHM)" : " (automate)")});
        // L'echelle : ce qui se voit, avec un peu de marge.
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (std::size_t k = k0; k < k1 && k < tr.values.size(); ++k)
            if (!std::isnan(tr.values[k])) {
                lo = std::min(lo, tr.values[k]);
                hi = std::max(hi, tr.values[k]);
            }
        if (tr.boolean) {
            lo = 0.0;
            hi = 1.0;
        } else if (!(lo <= hi)) {
            lo = 0.0;
            hi = 1.0;
        } else if (hi - lo < 1e-9) {
            lo -= 1.0;
            hi += 1.0;
        } else {
            const double m = (hi - lo) * 0.08;
            lo -= m;
            hi += m;
        }
        const float padY = tr.boolean ? 8.f : 6.f;
        const auto yOf = [&](double v) { return lane.bottom() - padY - (lane.h - 2.f * padY) * static_cast<float>((v - lo) / (hi - lo)); };
        if (!tr.boolean && laneH >= 50.f) {
            ctx.r.drawText({lane.x + 4.f, lane.y + 2.f}, decimalText(hi), kTiny, c.textMuted);
            ctx.r.drawText({lane.x + 4.f, lane.bottom() - 15.f}, decimalText(lo), kTiny, c.textMuted);
        }
        ctx.r.pushClip(lane);
        // Une colonne de pixels : le plus bas et le plus haut de ses instants ;
        // relies d'une colonne a la suivante.
        bool have = false;
        float lastX = 0.f, lastY = 0.f;
        const float cols = std::max(1.f, lane.w);
        std::size_t k = k0;
        for (float px = 0.f; px < cols && k < k1; px += 1.f) {
            const double tEnd = begin + model_->window() * static_cast<double>((px + 1.f) / cols);
            double mn = std::numeric_limits<double>::infinity(), mx = -mn, lastV = std::numeric_limits<double>::quiet_NaN();
            while (k < k1 && k < tr.values.size() && times[k] <= tEnd) {
                const double v = tr.values[k];
                if (!std::isnan(v)) {
                    mn = std::min(mn, v);
                    mx = std::max(mx, v);
                    lastV = v;
                }
                ++k;
            }
            if (std::isnan(lastV)) continue;
            const float cx = lane.x + px;
            const float yMin = yOf(mn), yMax = yOf(mx), yLast = yOf(lastV);
            if (have) {
                if (tr.boolean) {
                    ctx.r.line({lastX, lastY}, {cx, lastY}, colour, 2.f);
                    if (std::fabs(yLast - lastY) > 0.5f || std::fabs(yMax - yMin) > 0.5f) ctx.r.line({cx, std::min(yMin, lastY)}, {cx, std::max(yMax, lastY)}, colour, 2.f);
                } else {
                    ctx.r.line({lastX, lastY}, {cx, (yMin + yMax) * 0.5f}, colour, 2.f);
                }
            }
            if (std::fabs(yMax - yMin) > 0.5f) ctx.r.line({cx, yMin}, {cx, yMax}, colour, 2.f);
            have = true;
            lastX = cx;
            lastY = yLast;
        }
        ctx.r.popClip();
        y += laneH;
    }
    // L'axe du temps.
    for (double t = std::ceil(begin / step) * step; t <= end + 1e-6; t += step) {
        const double ago = end - t;
        std::string lab = ago < 0.5 ? (model_->frozen() ? std::string("fig\xC3\xA9") : std::string("maintenant")) : "\xE2\x88\x92" + ss::duration(ago);
        const float gx = xOf(t);
        const float w = textWidth(ctx, lab, kTiny);
        ctx.r.drawText({std::clamp(gx - w * 0.5f, plot_.x, plot_.right() - w), area.bottom() + 6.f}, lab, kTiny, c.textMuted);
    }
    // Le curseur.
    const double showT = cursorPinned_ ? cursor_ : (hovered() && cursor_ >= 0.0 ? cursor_ : -1.0);
    if (showT >= begin && showT <= end) {
        const float cx = xOf(showT);
        ctx.r.fillRect({cx, plot_.y, 1.5f, plot_.h}, cursorPinned_ ? c.accent : c.textMuted);
        const std::string lab = "\xE2\x88\x92" + ss::duration(end - showT);
        const float w = textWidth(ctx, lab, kTiny) + 10.f;
        const gfx::Rect tag{std::clamp(cx - w * 0.5f, plot_.x, plot_.right() - w), plot_.y - 2.f, w, 16.f};
        ctx.r.fillRoundedRect(tag, cursorPinned_ ? c.accent : c.borderStrong, 4.f);
        ctx.r.drawText({tag.x + 5.f, tag.y + 1.f}, lab, kTiny, cursorPinned_ ? c.selectionText : c.text);
    }
}

ui::EventResult SimTrendsPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = hitAt(m->pos);
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        if (!cursorPinned_) {
            const double t = plot_.contains(m->pos) ? timeAt(m->pos.x) : -1.0;
            if (t != cursor_) {
                cursor_ = t;
                invalidate();
            }
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && m->button == ui::MouseButton::Left) {
        if (const int h = hitAt(m->pos); h >= 0) {
            const auto key = hits_[static_cast<std::size_t>(h)].key;
            if (key.rfind("piste:", 0) == 0) return ui::EventResult::Ignored;
            act(key);
            return ui::EventResult::Consumed;
        }
        if (plot_.contains(m->pos)) {
            cursorPinned_ = !cursorPinned_;
            cursor_ = cursorPinned_ ? timeAt(m->pos.x) : -1.0;
            invalidate();
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

// =============================================================== le journal ====
class SimJournalPane::Model final : public ui::ITableModel {
public:
    enum Col : std::size_t { CClock, CCycle, CSource, CSeverity, CText, CGo, CCount };
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Heure", "Cycle", "Qui", "Gravit\xC3\xA9", "Quoi", "Aller \xC3\xA0"};   // les mots de la maquette
        return c < CCount ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= rows_.size()) return {};
        const auto& e = rows_[r];
        switch (c) {
            case CClock:    return e.clock;
            case CCycle:    return ss::thousands(e.cycle);
            case CSource:   return SimJournal::sourceName(e.source);
            case CSeverity: return SimJournal::severityName(e.severity);
            case CText:     return e.repeats > 1 ? e.text + "  (x " + std::to_string(e.repeats) + ")" : e.text;
            case CGo:       return e.go.empty() ? std::string{} : goLabel(e.go);
            default:        return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= rows_.size()) return s;
        const auto& e = rows_[r];
        const auto tone = e.severity == SimSeverity::Error ? ui::Tone::Error : e.severity == SimSeverity::Warning ? ui::Tone::Warning
                        : e.severity == SimSeverity::Ok ? ui::Tone::Ok : ui::Tone::Info;
        if (c == CSource) {
            s.icon = sourceIcon(e.source);
            s.iconTone = ui::Tone::Muted;
        } else if (c == CSeverity) {
            s.icon = e.severity == SimSeverity::Error ? ui::Icon::Error : e.severity == SimSeverity::Warning ? ui::Icon::Warning
                   : e.severity == SimSeverity::Ok ? ui::Icon::Ok : ui::Icon::Info;
            s.iconTone = tone;
            s.fgTone = tone;
        } else if (c == CClock || c == CCycle) {
            s.fgTone = ui::Tone::Muted;
        } else if (c == CGo) {
            s.fgTone = ui::Tone::Accent;
        } else if (c == CText && e.severity == SimSeverity::Error) {
            s.fgTone = ui::Tone::Error;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t c) const override {
        if (a >= rows_.size() || b >= rows_.size()) return a < b;
        if (c == CClock || c == CCycle) return rows_[a].id < rows_[b].id;
        if (c == CSeverity) return static_cast<int>(rows_[a].severity) < static_cast<int>(rows_[b].severity);
        return lowerAscii(cellText(a, c)) < lowerAscii(cellText(b, c));
    }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= rows_.size()) return {};
        const auto& e = rows_[r];
        std::string out = e.text + "\nCe que \xC3\xA7" "a veut dire : " + SimJournal::explanationOf(e);
        if (!e.go.empty()) out += "\nDouble-clic : " + goLabel(e.go);
        return out;
    }
    std::vector<SimEvent> rows_;     // une copie : le journal bouge pendant qu'on le lit
};

SimJournalPane::SimJournalPane(std::string id, std::shared_ptr<SimCenterModel> model, SimCenterHosts hosts)
    : ui::Widget(std::move(id)), model_(std::move(model)), hosts_(std::move(hosts)) {
    search_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(this->id() + ".chercher")));
    search_->setPlaceholder("Chercher dans le journal\xE2\x80\xA6");
    search_->setTooltip("Dans le texte, le genre et l'explication de chaque \xC3\xA9v\xC3\xA9nement, sans casse ni accents.");
    links_ += search_->textChanged->connect([this](const std::string&) {
        shownRevision_ = ~std::uint64_t{0};
        refresh();
    });
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".table")));
    tableModel_ = std::make_shared<Model>();
    table_->setModel(tableModel_);
    std::vector<ui::TableView::Column> cols(Model::CCount);
    const float widths[] = {84.f, 90.f, 130.f, 120.f, 520.f, 220.f};
    for (std::size_t i = 0; i < cols.size(); ++i) {
        cols[i].title = tableModel_->headerText(i);
        cols[i].width = widths[i];
        cols[i].sortable = false;
    }
    cols[Model::CCycle].align = ui::Align::End;
    table_->setColumns(std::move(cols));
    table_->setSelectionMode(ui::SelectionMode::Single);
    links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>&) {
        const auto rows = table_->selectedModelRows();
        selectedId_ = !rows.empty() && rows.front() < tableModel_->rows_.size() ? tableModel_->rows_[rows.front()].id : 0;
        invalidate();
    });
    links_ += table_->activated->connect([this](RowIndex row) {
        if (row < tableModel_->rows_.size()) {
            const auto go = tableModel_->rows_[row].go;
            if (!go.empty() && hosts_.go) hosts_.go(go);
        }
    });
    refresh();
}

SimJournalPane::~SimJournalPane() = default;

std::size_t SimJournalPane::rowCount() const noexcept { return tableModel_ ? tableModel_->rows_.size() : 0; }

void SimJournalPane::refresh() {
    const auto* j = model_ ? model_->journal : nullptr;
    const std::uint64_t rev = j ? j->revision() : 0;
    if (rev == shownRevision_) return;
    shownRevision_ = rev;
    rebuild();
}

void SimJournalPane::rebuild() {
    const auto* j = model_ ? model_->journal : nullptr;
    tableModel_->rows_.clear();
    if (j) {
        const unsigned sources = source_ < 0 ? 0u : SimJournal::bit(static_cast<SimSource>(source_));
        const auto sev = severity_ == 2 ? SimSeverity::Error : severity_ == 1 ? SimSeverity::Warning : SimSeverity::Info;
        for (const auto* e : j->filtered(sources, sev, search_ ? search_->text() : std::string{})) tableModel_->rows_.push_back(*e);
    }
    tableModel_->modelReset->emit();
    std::vector<RowIndex> again;
    for (std::size_t i = 0; i < tableModel_->rows_.size(); ++i)
        if (tableModel_->rows_[i].id == selectedId_) again.push_back(static_cast<RowIndex>(i));
    table_->selectModelRows(std::move(again), false);
    invalidate();
}

bool SimJournalPane::setSource(std::string_view source) {
    const auto s = lowerAscii(source);
    int want = -2;
    if (s.empty() || s == "tout" || s == "toutes" || s == "tous") want = -1;
    else if (s.rfind("auto", 0) == 0) want = static_cast<int>(SimSource::Automate);
    else if (s.rfind("ihm", 0) == 0) want = static_cast<int>(SimSource::Ihm);
    else if (s.rfind("equip", 0) == 0 || s.rfind("\xC3\xA9quip", 0) == 0) want = static_cast<int>(SimSource::Equipements);
    else if (s.rfind("deb", 0) == 0 || s.rfind("d\xC3\xA9" "b", 0) == 0) want = static_cast<int>(SimSource::Debogage);
    else if (s.rfind("simu", 0) == 0) want = static_cast<int>(SimSource::Simulation);
    if (want == -2) return false;
    source_ = want;
    rebuild();
    return true;
}

bool SimJournalPane::setSeverity(std::string_view severity) {
    const auto s = lowerAscii(severity);
    int want = -1;
    if (s.empty() || s == "tout" || s == "toutes" || s == "info") want = 0;
    else if (s.rfind("surv", 0) == 0 || s.rfind("\xC3\xA0 surv", 0) == 0 || s.rfind("a surv", 0) == 0 || s.rfind("aver", 0) == 0) want = 1;
    else if (s.rfind("err", 0) == 0) want = 2;
    if (want < 0) return false;
    severity_ = want;
    rebuild();
    return true;
}

void SimJournalPane::setSearch(const std::string& text) {
    if (search_) search_->setText(text);
}

std::vector<std::string> SimJournalPane::lines(std::size_t max) const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < tableModel_->rows_.size() && i < max; ++i) out.push_back(SimJournal::line(tableModel_->rows_[i]));
    return out;
}

const SimEvent* SimJournalPane::selected() const {
    for (const auto& e : tableModel_->rows_)
        if (e.id == selectedId_) return &e;
    return nullptr;
}

bool SimJournalPane::selectRow(std::size_t row) {
    if (row >= tableModel_->rows_.size()) return false;
    table_->selectModelRows({static_cast<RowIndex>(row)});
    selectedId_ = tableModel_->rows_[row].id;
    invalidate();
    return true;
}

bool SimJournalPane::goSelected() {
    const auto* e = selected();
    if (!e && !tableModel_->rows_.empty()) e = &tableModel_->rows_.front();
    if (!e || e->go.empty() || !hosts_.go) return false;
    const auto go = e->go;
    hosts_.go(go);
    return true;
}

gfx::Rect SimJournalPane::partRect(std::string_view key) const {
    for (const auto& h : hits_)
        if (h.key == key) return h.rect;
    return {};
}

int SimJournalPane::hitAt(gfx::Point p) const {
    for (std::size_t i = 0; i < hits_.size(); ++i)
        if (hits_[i].rect.contains(p)) return static_cast<int>(i);
    return -1;
}

void SimJournalPane::act(const std::string& key) {
    if (key.rfind("source:", 0) == 0) (void)setSource(key.substr(7));
    else if (key.rfind("gravite:", 0) == 0) (void)setSeverity(key.substr(8));
    else if (key == "aller") (void)goSelected();
}

void SimJournalPane::onLayout() {
    const auto b = bounds();
    search_->setBounds({b.x + 16.f, b.y + 12.f, std::clamp(b.w * 0.22f, 180.f, 300.f), 30.f});
    const float top = 54.f, detail = 92.f;
    table_->setBounds({b.x + 16.f, b.y + top, std::max(0.f, b.w - 32.f), std::max(0.f, b.h - top - detail - 8.f)});
}

void SimJournalPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.windowBg);
    hits_.clear();
    if (!hovered()) hover_ = -1;
    refresh();
    const auto* j = model_ ? model_->journal : nullptr;
    // Les pastilles des sources (et leur nombre), puis la gravite, a droite.
    float x = search_->bounds().right() + 14.f;
    const float cy = b.y + 27.f;
    struct Chip { std::string label, key; int source; std::size_t n; };
    std::vector<Chip> chips;
    chips.push_back({"Tout", "source:tout", -1, j ? j->size() : 0});
    for (const auto s : {SimSource::Automate, SimSource::Ihm, SimSource::Equipements, SimSource::Debogage, SimSource::Simulation}) {
        const std::size_t n = j ? j->count(s) : 0;
        if (n == 0 && s == SimSource::Simulation) continue;
        chips.push_back({SimJournal::sourceName(s), "source:" + lowerAscii(SimJournal::sourceName(s)), static_cast<int>(s), n});
    }
    // "equipements" et "debogage" : des cles sans accents.
    for (auto& ch : chips) {
        if (ch.source == static_cast<int>(SimSource::Equipements)) ch.key = "source:equipements";
        if (ch.source == static_cast<int>(SimSource::Debogage)) ch.key = "source:debogage";
    }
    for (const auto& ch : chips) {
        const std::string label = ch.label + "  " + std::to_string(ch.n);
        const float w = textWidth(ctx, label, kSmall) + 20.f;
        const gfx::Rect r{x, cy - 13.f, w, 26.f};
        const bool on = source_ == ch.source;
        const bool hot = hover_ == static_cast<int>(hits_.size());
        hits_.push_back({r, ch.key, "Montrer " + (ch.source < 0 ? std::string("tous les \xC3\xA9v\xC3\xA9nements") : "ceux de : " + ch.label)});
        ctx.r.fillRoundedRect(r, on ? c.selectionBg : hot ? ctx.theme.brand.hover : c.border, 13.f);
        if (!on) ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, hot ? ctx.theme.brand.hover : c.panelBg, 12.f);
        ctx.r.drawText({r.x + 10.f, r.y + (r.h - ctx.r.lineHeight(kSmall)) * 0.5f}, label, kSmall, on ? c.selectionText : c.text);
        x = r.right() + 6.f;
    }
    {
        const char* const labels[3] = {"Tout", "\xC3\x80 surveiller", "Erreurs"};
        const char* const keys[3] = {"gravite:tout", "gravite:surveiller", "gravite:erreurs"};
        float total = 0.f, w[3];
        for (int i = 0; i < 3; ++i) {
            w[i] = textWidth(ctx, labels[i], kSmall) + 20.f;
            total += w[i];
        }
        const gfx::Rect seg{std::max(x + 12.f, b.right() - 16.f - total - 4.f), cy - 14.f, total + 4.f, 28.f};
        ctx.r.fillRoundedRect(seg, c.border, 6.f);
        ctx.r.fillRoundedRect({seg.x + 1.f, seg.y + 1.f, seg.w - 2.f, seg.h - 2.f}, c.windowBg, 5.f);
        float sx = seg.x + 2.f;
        for (int i = 0; i < 3; ++i) {
            const gfx::Rect cell{sx, seg.y + 2.f, w[i], seg.h - 4.f};
            const bool on = severity_ == i;
            const bool hot = hover_ == static_cast<int>(hits_.size());
            hits_.push_back({cell, keys[i], i == 0 ? "Toutes les gravit\xC3\xA9s" : i == 1 ? "Les avertissements et les erreurs" : "Les erreurs seules"});
            if (on) ctx.r.fillRoundedRect(cell, c.selectionBg, 4.f);
            else if (hot) ctx.r.fillRoundedRect(cell, ctx.theme.brand.hover, 4.f);
            ctx.r.drawText({cell.x + 10.f, cell.y + (cell.h - ctx.r.lineHeight(kSmall)) * 0.5f}, labels[i], kSmall, on ? c.selectionText : c.textMuted);
            sx += w[i];
        }
    }
    // Rien : le dire.
    if (tableModel_->rows_.empty()) {
        const auto t = table_->bounds();
        const std::string none = j && !j->empty() ? "Aucun \xC3\xA9v\xC3\xA9nement ne passe ces filtres."
                                                  : "Le journal est vide : il se remplit d\xC3\xA8s que la simulation d\xC3\xA9marre (Simuler).";
        ctx.r.drawText({t.x + 16.f, t.y + 44.f}, fit(ctx, none, kBody, t.w - 32.f), kBody, c.textMuted);
    }
    // Le detail de la ligne choisie : ce qui s'est passe, ce que ca veut dire, Aller a.
    const gfx::Rect d{b.x + 16.f, b.bottom() - 92.f, b.w - 32.f, 82.f};
    card(ctx, d, false);
    const auto* e = selected();
    if (!e) {
        ctx.r.drawText({d.x + 16.f, d.y + 14.f}, "Choisis une ligne : ce qu'elle veut dire s'\xC3\xA9" "crit ici, avec le bouton qui y m\xC3\xA8ne "
                                                  "(double-clic : y aller).", kBody, c.textMuted);
        return;
    }
    const auto tone = severityColor(ctx, e->severity);
    ctx.r.fillRoundedRect({d.x + 1.f, d.y + 1.f, 5.f, d.h - 2.f}, tone, 2.f);
    float textRight = d.right() - 16.f;
    if (!e->go.empty()) {
        const bool hot = hover_ == static_cast<int>(hits_.size());
        const auto br = button(ctx, d.right() - 16.f, d.y + d.h * 0.5f, "Aller \xC3\xA0 : " + goLabel(e->go), hot, true, c.accent, true, kSmall, 30.f);
        hits_.push_back({br, "aller", "Aller \xC3\xA0 : " + goLabel(e->go)});
        textRight = br.x - 14.f;
    }
    std::string head = e->clock + kMid + SimJournal::sourceName(e->source) + kMid + e->text;
    if (e->repeats > 1) head += "  (x " + std::to_string(e->repeats) + ")";
    drawBold(ctx, {d.x + 18.f, d.y + 12.f}, fit(ctx, head, kBody, textRight - d.x - 18.f), kBody, c.text);
    const auto lines = wrap(ctx, "Ce que \xC3\xA7" "a veut dire : " + SimJournal::explanationOf(*e), kSmall, textRight - d.x - 18.f, 2);
    for (std::size_t k = 0; k < lines.size(); ++k)
        ctx.r.drawText({d.x + 18.f, d.y + 36.f + static_cast<float>(k) * 16.f}, lines[k], kSmall, c.textMuted);
}

ui::EventResult SimJournalPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = hitAt(m->pos);
        if (h != hover_) {
            hover_ = h;
            setTooltip(h >= 0 ? hits_[static_cast<std::size_t>(h)].tip : std::string{});
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && m->button == ui::MouseButton::Left) {
        pressed_ = hitAt(m->pos);
        if (pressed_ >= 0) return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseUp>(&ev); m && m->button == ui::MouseButton::Left) {
        const int h = hitAt(m->pos);
        const bool click = h >= 0 && h == pressed_;
        pressed_ = -1;
        if (click) {
            const auto key = hits_[static_cast<std::size_t>(h)].key;
            act(key);
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

} // namespace app
