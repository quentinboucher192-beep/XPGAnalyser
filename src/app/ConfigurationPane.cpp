// app/ConfigurationPane.cpp - Configuration : processeur, racks, voies, reseau, memoire (lot API 4).
#include "ConfigurationPane.hpp"

#include "ApiPanes.hpp"
#include "RackView.hpp"
#include "ViewModels.hpp"
#include "hmi/HmiPanels.hpp"

#include "../project/ApiCommands.hpp"
#include "../ui/Icons.hpp"
#include "../ui/widgets/Containers.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace app {

using ui::RowIndex;
using PG = ui::PropertyGrid;
namespace io = project::io;

namespace {

const gfx::FontId kSmall{13};

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string joined(const std::vector<std::string>& v, std::size_t max = 4) {
    std::string out;
    for (std::size_t i = 0; i < v.size() && i < max; ++i) out += (out.empty() ? "" : ", ") + v[i];
    if (v.size() > max) out += " +" + std::to_string(v.size() - max);
    return out;
}

std::string thousands(std::size_t n) {
    std::string d = std::to_string(n), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += d[i];
    }
    return out;
}

} // namespace

// ======================================================= voies et adresses ====
class ConfigurationPane::ChannelsModel final : public ui::ITableModel {
public:
    explicit ChannelsModel(ConfigurationPane& pane) : pane_(pane) {}
    enum Col : std::size_t { CAddress, CModule, CPlace, CChannel, CDirection, CSize, CUsers, CVerdict, CCount };
    [[nodiscard]] std::size_t rowCount() const override { return pane_.channelRows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Adresse", "Module", "Rack.empl.", "Voie", "Sens", "Taille", "Utilis\xC3\xA9" "e par", "V\xC3\xA9rification"};
        return c < CCount ? kTitles[c] : "";
    }
    [[nodiscard]] const io::Address* at(RowIndex r) const {
        return r < pane_.channelRows_.size() ? &pane_.report_.addresses[pane_.channelRows_[r]] : nullptr;
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        const auto* a = at(r);
        if (!a) return {};
        switch (c) {
            case CAddress: return a->text;
            case CModule: return a->module.empty() ? std::string(pane_.report_.hardware ? "aucun module" : "\xE2\x80\x94") : a->module;
            case CPlace: return std::to_string(a->rack) + "." + std::to_string(a->slot);
            case CChannel: return std::to_string(a->channel) + (a->sub >= 0 ? "." + std::to_string(a->sub) : std::string{});
            case CDirection: return a->direction == io::Direction::Input ? "entr\xC3\xA9" "e" : "sortie";
            case CSize: return a->word ? "mot" : "bit";
            case CUsers: return joined(a->users, 3);
            default: {
                const auto v = io::verdict(*a);
                return v == "ok" ? std::string("ok") : v;
            }
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        const auto* a = at(r);
        if (!a) return s;
        const bool bad = a->faulty();
        if (c == CAddress) s.monospace = true;
        if (c == CVerdict) {
            s.fgTone = bad ? ui::Tone::Warning : (a->status == io::Address::Status::Ok ? ui::Tone::Ok : ui::Tone::Muted);
            if (bad) s.icon = ui::Icon::Warning, s.iconTone = ui::Tone::Warning;
            return s;
        }
        if (c == CModule && a->module.empty() && pane_.report_.hardware) {
            s.fgTone = ui::Tone::Error;
            return s;
        }
        if (bad) s.fgTone = ui::Tone::Muted;
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
private:
    ConfigurationPane& pane_;
};

// ================================================================ reseau ====
class ConfigurationPane::NetworkModel final : public ui::ITableModel {
public:
    explicit NetworkModel(ConfigurationPane& pane) : pane_(pane) {}
    [[nodiscard]] std::size_t rowCount() const override { return pane_.ports_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 6; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Module", "Rack.empl.", "Voie", "Protocole", "R\xC3\xB4le (.XHW)", "T\xC3\xA2" "che"};
        return c < 6 ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= pane_.ports_.size()) return {};
        const auto& p = pane_.ports_[r];
        switch (c) {
            case 0: return p.module;
            case 1: return std::to_string(p.rack) + "." + std::to_string(p.slot);
            case 2: return std::to_string(p.channel);
            case 3: return p.protocol;
            case 4: return p.role;
            default: return p.task;
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex, std::size_t c) const override {
        ui::CellStyle s;
        if (c == 0) s.icon = ui::Icon::Network, s.iconTone = ui::Tone::Info;
        if (c == 4) s.fgTone = ui::Tone::Muted, s.monospace = true;
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
private:
    ConfigurationPane& pane_;
};

// =========================================================== plan memoire ====
//  Lot API 5 : les TROIS zones. En haut, une carte par zone (%M, %MW, %KW) : sa
//  taille complete, ses bornes de lecture, son utilisation (une barre et le
//  pourcentage), ce qui cloche. Un clic sur une carte montre sa zone. Dessous,
//  la zone montree entre ses bornes : un carre par cellule (bit ou mot), une
//  couleur par variable, le rouge pour une cellule que deux variables se
//  disputent, un trait pour une cellule que le code nomme en direct sans
//  variable, un liseré rouge au-dela de la taille configuree.
class ConfigurationPane::MemoryMapView final : public ui::Widget {
public:
    MemoryMapView(ConfigurationPane& pane, std::string id) : ui::Widget(std::move(id)), pane_(pane) {}
    const core::SignalPtr<std::uint32_t> wordChosen = core::Signal<std::uint32_t>::create();
    const core::SignalPtr<std::size_t>   zoneChosen = core::Signal<std::size_t>::create();
    [[nodiscard]] bool wordRect(std::uint32_t word, gfx::Rect& out) const {
        const auto* m = zone();
        if (!m || word < m->from || word > m->to || cols_ == 0) return false;
        const auto k = word - m->from;
        out = {grid_.x + static_cast<float>(k % cols_) * cell_, grid_.y + static_cast<float>(k / cols_) * cell_ - scroll_, cell_ - gap(), cell_ - gap()};
        return true;
    }
    [[nodiscard]] gfx::Rect cardRect(std::size_t zone) const { return zone < cards_.size() ? cards_[zone] : gfx::Rect{}; }
    void resetScroll() { scroll_ = 0.f; }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        r.fillRect(b, c.windowBg);
        const auto& zones = pane_.zones_;
        if (zones.size() < 3) return;
        // ---- les trois cartes ----------------------------------------------
        cards_.clear();
        const float cardH = 86.f, pad = 16.f;
        const float cardW = std::max(160.f, (b.w - pad * 4.f) / 3.f);
        static const char* const kTitles[] = {"bits internes", "mots internes", "constantes"};
        for (std::size_t z = 0; z < 3; ++z) {
            const auto& m = zones[z];
            const gfx::Rect card{b.x + pad + static_cast<float>(z) * (cardW + pad), b.y + 12.f, cardW, cardH};
            cards_.push_back(card);
            const bool on = z == pane_.zone_;
            const bool trouble = !m.outside.empty() || !m.overlaps.empty() || m.dynamicUnknown > 0;
            r.fillRoundedRect(card, on ? c.accent.withAlpha(34) : c.panelBg, 6.f);
            r.strokeRect(card, on ? c.accent : c.border, on ? 1.5f : 1.f);
            r.drawText({card.x + 12.f, card.y + 8.f}, m.prefix, gfx::FontId{17}, c.text);
            const float px = card.x + 14.f + r.measure(m.prefix, gfx::FontId{17}).width;
            r.drawText({px + 6.f, card.y + 12.f}, kTitles[z], kSmall, c.textMuted);
            const std::string size = thousands(m.size) + " " + m.units + (m.configured ? " (.XHW)" : " (sans .XHW : jusqu'\xC3\xA0 la derni\xC3\xA8re employ\xC3\xA9" "e)");
            r.drawText({card.x + 12.f, card.y + 32.f}, fit(r, size, kSmall, card.w - 24.f), kSmall, c.textMuted);
            // La barre : l'utilisation dans les bornes.
            const gfx::Rect bar{card.x + 12.f, card.y + 54.f, card.w - 96.f, 8.f};
            r.fillRoundedRect(bar, c.gridLine, 4.f);
            const float fill = static_cast<float>(std::min(100.0, m.percent()) / 100.0) * bar.w;
            const auto tone = ctx.theme.tone(m.percent() >= 90.0 ? ui::Tone::Warning : ui::familyTone(1 + static_cast<int>(z)), c.accent);
            if (m.used) r.fillRoundedRect({bar.x, bar.y, std::max(3.f, fill), bar.h}, tone, 4.f);
            const std::string pct = io::percentText(m.percent());
            r.drawText({card.right() - 12.f - r.measure(pct, gfx::FontId{15}).width, card.y + 49.f}, pct, gfx::FontId{15}, c.text);
            std::string foot = m.bounded ? "bornes " + m.cell(m.from) + " \xE2\x86\x92 " + m.cell(m.to) : std::string("toute la zone");
            foot += " \xC2\xB7 " + thousands(m.used) + " / " + thousands(m.span()) + " " + m.units;
            if (m.dynamicUnknown) foot = std::to_string(m.dynamicUnknown) + " index\xC3\xA9" "e" + (m.dynamicUnknown > 1 ? "s" : "") + " dynamique" + (m.dynamicUnknown > 1 ? "s" : "") + " \xC2\xB7 " + foot;
            if (!m.outside.empty()) foot = std::to_string(m.outside.size()) + " hors zone \xC2\xB7 " + foot;
            r.drawText({card.x + 12.f, card.y + 67.f}, fit(r, foot, kSmall, card.w - 24.f), kSmall, trouble ? c.warning : c.textMuted);
        }
        // ---- la zone montree ------------------------------------------------
        const auto* m = zone();
        const float top = b.y + 12.f + cardH + 14.f;
        const std::uint32_t count = m->span();
        const std::string title = m->cell(m->from) + " \xC3\x80 " + m->cell(m->to) + "  \xC2\xB7  UN CARR\xC3\x89 = UN " + (m->zone == domain::MemoryZone::Bits ? "BIT" : "MOT")
                                + "  \xC2\xB7  " + std::to_string(m->variables.size()) + (m->variables.size() == 1 ? " VARIABLE SITU\xC3\x89" "E" : " VARIABLES SITU\xC3\x89" "ES")
                                + "  \xC2\xB7  " + io::percentText(m->percent()) + " EMPLOY\xC3\x89S"
                                + (m->overlaps.empty() ? std::string{} : "  \xC2\xB7  " + std::to_string(m->overlaps.size()) + " CHEVAUCHEMENTS");
        r.drawText({b.x + 20.f, top}, title, kSmall, c.textMuted);
        const gfx::Rect area{b.x + 20.f, top + 24.f, std::max(10.f, b.w - 40.f), std::max(10.f, b.bottom() - top - 24.f - 34.f)};
        // La taille d'un carre : tout tient si possible (entre 6 et 20 px) ; sinon on defile.
        const float fitCell = count ? std::floor(std::sqrt(area.w * area.h / static_cast<float>(count))) : 20.f;
        cell_ = std::clamp(fitCell, 6.f, 20.f);
        cols_ = static_cast<std::uint32_t>(std::max(1.f, std::floor(area.w / cell_)));
        if (cols_ >= 20) cols_ -= cols_ % 10;
        grid_ = {area.x, area.y, static_cast<float>(cols_) * cell_, 0.f};
        rows_ = cols_ ? (count + cols_ - 1) / cols_ : 0;
        viewH_ = area.h;
        // Le proprietaire de chaque cellule : -1 libre, -2 disputee, sinon la variable.
        std::vector<int> owner(count, -1);
        for (std::size_t i = 0; i < m->variables.size(); ++i) {
            const auto& v = m->variables[i];
            for (std::uint32_t w = v.first; w < v.first + std::max<std::uint32_t>(1, v.words); ++w) {
                if (w < m->from || w > m->to) continue;
                auto& o = owner[w - m->from];
                o = o == -1 ? static_cast<int>(i) : -2;
            }
        }
        r.pushClip(area);
        const float g = gap();
        const std::uint32_t firstRow = static_cast<std::uint32_t>(std::max(0.f, scroll_ / cell_));
        for (std::uint32_t k = firstRow * cols_; k < count; ++k) {
            const gfx::Rect cell{grid_.x + static_cast<float>(k % cols_) * cell_, grid_.y + static_cast<float>(k / cols_) * cell_ - scroll_, cell_ - g, cell_ - g};
            if (cell.y > area.bottom()) break;
            const int o = owner[k];
            const std::uint32_t word = m->from + k;
            const bool direct = std::binary_search(m->direct.begin(), m->direct.end(), word);
            const bool written = direct && std::binary_search(m->written.begin(), m->written.end(), word);
            const bool dynamic = std::binary_search(m->dynamicCells.begin(), m->dynamicCells.end(), word);
            const bool dim = pane_.overlapsOnly_ && o != -2;
            const float radius = cell_ >= 12.f ? 2.f : 0.f;
            if (o == -2) {
                r.fillRoundedRect(cell, c.error, radius);
            } else if (o >= 0) {
                auto col = ctx.theme.tone(ui::familyTone(1 + o % 4), c.accent);
                if (dim) col = col.withAlpha(50);
                r.fillRoundedRect(cell, col, radius);
            } else if (direct) {
                // Ecrite en direct (%MW100 := 50) : elle prend la place d'une variable.
                r.fillRoundedRect(cell, c.textMuted.withAlpha(dim ? 30 : written ? 190 : 100), radius);
            } else {
                r.fillRoundedRect(cell, c.gridLine, radius);
            }
            if (dynamic && o < 0) {
                // Une plage indexee dynamique (%MW600[i], FOR i := 0 TO 9) : hachuree.
                const auto dyn = ctx.theme.tone(ui::Tone::Warning, c.warning);
                r.fillRoundedRect(cell, dyn.withAlpha(dim ? 20 : 70), radius);
                r.line({cell.x + 1.f, cell.bottom() - 1.f}, {cell.right() - 1.f, cell.y + 1.f}, dyn.withAlpha(dim ? 60 : 200), 1.f);
            }
            if (unknownBase(word)) {
                const auto dyn = ctx.theme.tone(ui::Tone::Warning, c.warning);
                r.strokeRect(cell, dyn, cell_ >= 12.f ? 2.f : 1.f);
                if (cell_ >= 14.f) r.drawText({cell.x + cell.w * 0.5f - 3.f, cell.y + 1.f}, "?", kSmall, dyn);
            }
            if (m->configured && word >= m->size) r.strokeRect(cell, c.error, 1.f);
            if (pane_.showGap_ && m->gapSize() > 0 && word >= m->gapFrom && word <= m->gapTo) r.strokeRect(cell, c.ok, cell_ >= 12.f ? 2.f : 1.f);
            if (pane_.hasWord_ && selectedOwner(word, owner, o)) r.strokeRect({cell.x - 1.f, cell.y - 1.f, cell.w + 2.f, cell.h + 2.f}, c.text, 2.f);
        }
        r.popClip();
        // ---- le pied ---------------------------------------------------------
        std::string foot;
        bool warn = false;
        if (!m->outside.empty()) {
            foot = std::to_string(m->outside.size()) + " hors zone - variables situ\xC3\xA9" "es, adresses du code, plages index\xC3\xA9" "es - au-del\xC3\xA0 des "
                 + thousands(m->size) + " " + m->units + " configur\xC3\xA9s (" + m->outside.front() + (m->outside.size() > 1 ? "\xE2\x80\xA6" : "")
                 + ") : Control Expert refuserait les variables, l'automate passerait en d\xC3\xA9" "faut sur un indice hors zone.";
            warn = true;
        } else if (!m->overlaps.empty()) {
            foot = std::to_string(m->overlaps.size()) + " cellules disput\xC3\xA9" "es (en rouge) : deux variables situ\xC3\xA9" "es s'y recouvrent.";
            warn = true;
        } else if (m->dynamicUnknown) {
            foot = std::to_string(m->dynamicUnknown) + (m->dynamicUnknown == 1 ? " adresse index\xC3\xA9" "e" : " adresses index\xC3\xA9" "es")
                 + " dynamique" + (m->dynamicUnknown == 1 ? "" : "s") + " sans plage connue (cadre orange, \xC2\xAB ? \xC2\xBB) : "
                 + "la m\xC3\xA9moire touch\xC3\xA9" "e d\xC3\xA9pend de l'indice \xC3\xA0 l'ex\xC3\xA9" "cution \xC2\xB7 hachur\xC3\xA9 : plage d\xC3\xA9" "duite d'une boucle FOR.";
            warn = true;
        } else {
            foot = "Aucun chevauchement \xC2\xB7 gris fonc\xC3\xA9 : \xC3\xA9" "crit en direct (%MW100 := 50), gris clair : lu \xC2\xB7 hachur\xC3\xA9 : index\xC3\xA9 dynamique \xC2\xB7 plus grande place libre : "
                 + (m->gapSize() ? thousands(m->gapSize()) + " " + m->units + " (" + m->cell(m->gapFrom) + ")" : std::string("aucune"));
        }
        r.drawText({b.x + 20.f, b.bottom() - 24.f}, fit(r, foot, kSmall, b.w - 40.f), kSmall, warn ? c.warning : c.textMuted);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
            if (!bounds().contains(w->pos)) return ui::EventResult::Ignored;
            const float maxScroll = std::max(0.f, static_cast<float>(rows_) * cell_ - viewH_);
            scroll_ = std::clamp(scroll_ - w->dy * cell_ * 3.f, 0.f, maxScroll);
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (!bounds().contains(d->pos)) return ui::EventResult::Ignored;
            for (std::size_t z = 0; z < cards_.size(); ++z)
                if (cards_[z].contains(d->pos)) {
                    zoneChosen->emit(z);
                    return ui::EventResult::Consumed;
                }
            const auto* m = zone();
            if (!m || cols_ == 0) return ui::EventResult::Consumed;
            const float lx = d->pos.x - grid_.x, ly = d->pos.y - grid_.y + scroll_;
            if (lx < 0.f || ly < 0.f || d->pos.y < grid_.y) return ui::EventResult::Consumed;
            const auto col = static_cast<std::uint32_t>(lx / cell_), row = static_cast<std::uint32_t>(ly / cell_);
            if (col >= cols_) return ui::EventResult::Consumed;
            const std::uint64_t k = static_cast<std::uint64_t>(row) * cols_ + col;
            if (k < m->span()) wordChosen->emit(m->from + static_cast<std::uint32_t>(k));
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
private:
    [[nodiscard]] const io::ZoneMap* zone() const {
        return pane_.zone_ < pane_.zones_.size() ? &pane_.zones_[pane_.zone_] : nullptr;
    }
    [[nodiscard]] float gap() const noexcept { return cell_ >= 12.f ? 4.f : 1.f; }
    // La base d'une adresse indexee dynamique dont la plage est inconnue.
    [[nodiscard]] bool unknownBase(std::uint32_t cell) const {
        const auto* m = zone();
        if (!m || !m->dynamicUnknown) return false;
        for (const auto& in : m->indexed)
            if (in.dynamic && !in.bounded && in.lo == cell) return true;
        return false;
    }
    [[nodiscard]] static std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
        if (r.measure(s, f).width <= w) return s;
        std::string out = s;
        while (!out.empty() && r.measure(out + "\xE2\x80\xA6", f).width > w) {
            out.pop_back();
            while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
        }
        return out + "\xE2\x80\xA6";
    }
    [[nodiscard]] bool selectedOwner(std::uint32_t word, const std::vector<int>& owner, int o) const {
        if (word == pane_.word_) return true;
        const auto* m = zone();
        if (!m || pane_.word_ < m->from) return false;
        const auto k = pane_.word_ - m->from;
        return o >= 0 && k < owner.size() && owner[k] == o;
    }
    ConfigurationPane& pane_;
    mutable std::uint32_t cols_{50}, rows_{0};
    mutable float cell_{20.f}, viewH_{0.f};
    mutable gfx::Rect grid_{};
    mutable std::vector<gfx::Rect> cards_;
    float scroll_{0.f};
};

// ================================================================ le volet ====
ConfigurationPane::ConfigurationPane(std::string id) : ui::Widget(std::move(id)) {
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::make_unique<ui::TabControl>(this->id() + ".onglets")));
    props_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(this->id() + ".proprietes")));
    props_->setShowDescriptionPane(true);

    auto processor = std::make_unique<ui::PropertyGrid>(this->id() + ".processeur");
    processor_ = processor.get();
    tabs_->addTab(ui::TabControl::Tab{"Processeur", ui::Icon::Cpu, false, false}, std::move(processor));

    auto rack = std::make_unique<RackView>(this->id() + ".racks");
    rackView_ = rack.get();
    links_ += rackView_->moduleSelected->connect([this](std::uint16_t r, std::int16_t s) {
        rack_ = r;
        slot_ = s;
        refreshProperties();
    });
    tabs_->addTab(ui::TabControl::Tab{"Racks et modules", ui::Icon::Rack, false, false}, std::move(rack));

    auto channels = std::make_unique<ui::TableView>(this->id() + ".voies");
    channels_ = channels.get();
    channelsModel_ = std::make_shared<ChannelsModel>(*this);
    channels_->setModel(channelsModel_);
    {
        std::vector<ui::TableView::Column> cols(ChannelsModel::CCount);
        const float widths[] = {120.f, 150.f, 90.f, 60.f, 80.f, 64.f, 260.f, 360.f};
        for (std::size_t i = 0; i < cols.size(); ++i) {
            cols[i].title = channelsModel_->headerText(i);
            cols[i].width = widths[i];
            cols[i].sortable = false;
        }
        cols[ChannelsModel::CPlace].align = ui::Align::End;
        cols[ChannelsModel::CChannel].align = ui::Align::End;
        channels_->setColumns(std::move(cols));
    }
    channels_->setSelectionMode(ui::SelectionMode::Single);
    channels_->setAlternatingRowColors(true);
    links_ += channels_->selectionChanged->connect([this](const std::vector<RowIndex>&) { if (!syncing_) refreshProperties(); });
    tabs_->addTab(ui::TabControl::Tab{"Voies et adresses", ui::Icon::Module, false, false}, std::move(channels));

    auto network = std::make_unique<ui::TableView>(this->id() + ".reseau");
    network_ = network.get();
    networkModel_ = std::make_shared<NetworkModel>(*this);
    network_->setModel(networkModel_);
    {
        std::vector<ui::TableView::Column> cols(6);
        const float widths[] = {180.f, 90.f, 60.f, 260.f, 300.f, 80.f};
        for (std::size_t i = 0; i < cols.size(); ++i) {
            cols[i].title = networkModel_->headerText(i);
            cols[i].width = widths[i];
            cols[i].sortable = false;
        }
        network_->setColumns(std::move(cols));
    }
    network_->setSelectionMode(ui::SelectionMode::Single);
    links_ += network_->selectionChanged->connect([this](const std::vector<RowIndex>&) { if (!syncing_) refreshProperties(); });
    tabs_->addTab(ui::TabControl::Tab{"R\xC3\xA9seau", ui::Icon::Network, false, false}, std::move(network));

    auto memoryView = std::make_unique<MemoryMapView>(*this, this->id() + ".memoire");
    memoryView_ = memoryView.get();
    links_ += memoryView_->wordChosen->connect([this](std::uint32_t w) {
        word_ = w;
        hasWord_ = true;
        memoryView_->invalidate();
        refreshProperties();
    });
    links_ += memoryView_->zoneChosen->connect([this](std::size_t z) { showZone(z); });
    tabs_->addTab(ui::TabControl::Tab{"Plan m\xC3\xA9moire", ui::Icon::Chart, false, false}, std::move(memoryView));

    links_ += tabs_->currentChanged->connect([this](std::size_t) {
        refreshProperties();
        updateHint();
    });
    tabs_->setCurrentIndex(TRacks);
}

ConfigurationPane::~ConfigurationPane() = default;

void ConfigurationPane::setHosts(ApiPaneHosts h) {
    hosts_ = std::move(h);
    refresh();
}

void ConfigurationPane::attach(ApiFrame& frame) {
    frame_ = &frame;
    auto& t = frame.tools();
    t.add(AAddRack, HmiGlyph::Plus, "Ajouter un rack", "Rack");
    t.add(AAddModule, HmiGlyph::Plus, "Ajouter un module dans un emplacement libre", "Module");
    t.add(AReplace, HmiGlyph::Refresh, "Remplacer le module choisi par une autre r\xC3\xA9" "f\xC3\xA9rence", "Remplacer\xE2\x80\xA6");
    t.add(ARemove, HmiGlyph::Delete, "Retirer le module choisi (Ctrl+Z le remet)", "Supprimer");
    t.separator();
    t.add(AImportXhw, HmiGlyph::Import, "Importer le .XHW dans le projet ouvert : les racks, les modules, leurs voies", "Importer le .XHW\xE2\x80\xA6");
    t.add(ACheck, HmiGlyph::Check, "V\xC3\xA9rifier les adresses du code face aux modules", "V\xC3\xA9rifier les adresses");
    t.add(AFaultyOnly, HmiGlyph::EyeOff, "Seulement les adresses fautives", "Seulement les fautives");
    t.add(AShowModule, HmiGlyph::System, "Voir le module de l'adresse choisie", "Voir le module");
    // Lot API 5 : la zone montree, ses bornes de lecture.
    t.add(AZoneBits, HmiGlyph::Grid, "Zone %M : les bits internes", "%M");
    t.add(AZoneWords, HmiGlyph::Grid, "Zone %MW : les mots internes (et les %MD, %MF)", "%MW");
    t.add(AZoneConstants, HmiGlyph::Grid, "Zone %KW : les constantes", "%KW");
    t.add(ABounds, HmiGlyph::Magnet, "Borner la lecture de la zone : son d\xC3\xA9" "but et sa fin (Ctrl+Z les retire)", "Bornes\xE2\x80\xA6");
    t.add(AWholeZone, HmiGlyph::ZoomFit, "Lire toute la zone (retirer les bornes)", "Toute la zone");
    t.add(AFindGap, HmiGlyph::Search, "Trouver la plus grande place libre dans les bornes de la zone", "Trouver une place libre\xE2\x80\xA6");
    t.add(AOverlapsOnly, HmiGlyph::EyeOff, "Seulement les chevauchements", "Seulement les chevauchements");
    const auto on = [this](std::vector<int> tabs) {
        return [this, tabs = std::move(tabs)] { const int c = currentTab(); return std::find(tabs.begin(), tabs.end(), c) != tabs.end(); };
    };
    for (const int a : {AAddRack, AAddModule, AReplace, ARemove}) t.setVisibleWhen(a, on({TRacks}));
    t.setVisibleWhen(AImportXhw, on({TProcessor, TRacks, TNetwork}));
    t.setVisibleWhen(ACheck, on({TRacks}));
    t.setVisibleWhen(AFaultyOnly, on({TChannels}));
    t.setVisibleWhen(AShowModule, on({TChannels}));
    t.setVisibleWhen(AFindGap, on({TMemory}));
    t.setVisibleWhen(AOverlapsOnly, on({TMemory}));
    for (const int a : {AZoneBits, AZoneWords, AZoneConstants, ABounds, AWholeZone}) t.setVisibleWhen(a, on({TMemory}));
    t.setCheckedWhen(AZoneBits, [this] { return zone_ == 0; });
    t.setCheckedWhen(AZoneWords, [this] { return zone_ == 1; });
    t.setCheckedWhen(AZoneConstants, [this] { return zone_ == 2; });
    t.setCheckedWhen(AFaultyOnly, [this] { return faultyOnly_; });
    t.setCheckedWhen(AOverlapsOnly, [this] { return overlapsOnly_; });
    t.setCheckedWhen(AFindGap, [this] { return showGap_; });
    const auto editable = [this] { return hosts_.project && hosts_.project() != nullptr; };
    const auto moduleChosen = [this, editable] {
        const auto* m = moduleAt(static_cast<std::uint16_t>(std::max(0, rack_)), static_cast<std::int16_t>(slot_));
        return editable() && rack_ >= 0 && m && !(rack_ == 0 && slot_ == 0) && !m->isCpu && m->slot >= 0;
    };
    t.setEnabledWhen(AAddRack, editable);
    t.setEnabledWhen(AAddModule, editable);
    t.setEnabledWhen(AReplace, moduleChosen);
    t.setEnabledWhen(ARemove, moduleChosen);
    t.setEnabledWhen(AImportXhw, editable);
    t.setEnabledWhen(ABounds, editable);
    t.setEnabledWhen(AWholeZone, [this, editable] { return editable() && zone_ < zones_.size() && zones_[zone_].bounded; });
    t.setEnabledWhen(AShowModule, [this] {
        const auto rows = channels_->selectedModelRows();
        return !rows.empty() && rows.front() < channelRows_.size() && !report_.addresses[channelRows_[rows.front()]].module.empty();
    });
    links_ += t.triggered->connect([this](int a) { runAction(a); });
    updateHint();
}

int ConfigurationPane::currentTab() const { return static_cast<int>(tabs_->currentIndex()); }

void ConfigurationPane::showTab(int tab) {
    if (tab >= 0 && tab < TCount) tabs_->setCurrentIndex(static_cast<std::size_t>(tab));
    refreshProperties();
    updateHint();
}

const domain::Module* ConfigurationPane::moduleAt(std::uint16_t rack, std::int16_t slot) const {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (!p) return nullptr;
    for (const auto& r : p->hardware.racks)
        if (r.number == rack)
            for (const auto& m : r.modules)
                if (m.slot == slot) return &m;
    return nullptr;
}

void ConfigurationPane::refresh() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (!p) return;
    report_ = io::check(*p);
    memory_ = io::memoryOf(*p);
    zones_ = io::memoryZones(*p);
    ports_ = io::networkOf(*p);
    processor_->setCategories(buildConfigurationProperties(*p));
    rackView_->setHardware(p);
    std::vector<RackView::Usage> usage;
    for (const auto& m : report_.modules) {
        RackView::Usage u;
        u.rack = static_cast<std::uint16_t>(m.rack);
        u.slot = static_cast<std::int16_t>(m.slot);
        u.used = m.used;
        u.total = m.total;
        for (const auto& a : report_.addresses)
            if (a.rack == m.rack && a.slot == m.slot && a.faulty()) u.faulty = true;
        usage.push_back(u);
    }
    rackView_->setUsage(std::move(usage));
    if (rack_ >= 0) rackView_->select(static_cast<std::uint16_t>(rack_), static_cast<std::int16_t>(slot_));
    rebuildChannelRows();
    networkModel_->modelReset->emit();
    if (hasWord_ && (zone_ >= zones_.size() || word_ < zones_[zone_].from || word_ > zones_[zone_].to)) hasWord_ = false;
    memoryView_->invalidate();
    // Les nombres des sous-onglets : ceux de l'arbre.
    tabs_->setTabBadge(TRacks, std::to_string(p->hardware.racks.size()));
    tabs_->setTabBadge(TChannels, std::to_string(report_.addresses.size()), report_.faulty ? ui::Tone::Warning : ui::Tone::None);
    tabs_->setTabBadge(TNetwork, std::to_string(ports_.size()));
    {
        std::size_t located = 0;
        bool trouble = false;
        for (const auto& z : zones_) {
            located += z.variables.size();
            trouble = trouble || !z.overlaps.empty() || !z.outside.empty();
        }
        tabs_->setTabBadge(TMemory, std::to_string(located), trouble ? ui::Tone::Warning : ui::Tone::None);
    }
    refreshProperties();
    updateHint();
}

void ConfigurationPane::rebuildChannelRows() {
    std::string keep;
    if (const auto rows = channels_->selectedModelRows(); !rows.empty() && rows.front() < channelRows_.size() && channelRows_[rows.front()] < report_.addresses.size())
        keep = report_.addresses[channelRows_[rows.front()]].text;
    channelRows_.clear();
    for (std::size_t i = 0; i < report_.addresses.size(); ++i)
        if (!faultyOnly_ || report_.addresses[i].faulty()) channelRows_.push_back(i);
    channelsModel_->modelReset->emit();
    if (!keep.empty()) {
        syncing_ = true;
        (void)selectAddress(keep);
        syncing_ = false;
    }
}

bool ConfigurationPane::selectModule(std::uint16_t rack, std::int16_t slot) {
    if (!moduleAt(rack, slot)) return false;
    rack_ = rack;
    slot_ = slot;
    rackView_->select(rack, slot);
    showTab(TRacks);
    return true;
}

bool ConfigurationPane::selectAddress(std::string_view text) {
    const auto u = upper(text);
    for (std::size_t i = 0; i < channelRows_.size(); ++i)
        if (report_.addresses[channelRows_[i]].text == u) {
            channels_->selectModelRows({static_cast<RowIndex>(i)}, !syncing_);
            return true;
        }
    return false;
}

void ConfigurationPane::runAction(int action) {
    auto doc = hosts_.project ? hosts_.project() : nullptr;
    const auto status = [this](const std::string& m) { if (hosts_.status) hosts_.status(m); };
    switch (action) {
        case AAddRack: if (hosts_.request) hosts_.request("create.rack"); return;
        case AAddModule: if (hosts_.request) hosts_.request("create.module"); return;
        case AImportXhw: if (hosts_.request) hosts_.request("import-xhw"); return;
        case AReplace: {
            const auto* m = moduleAt(static_cast<std::uint16_t>(std::max(0, rack_)), static_cast<std::int16_t>(slot_));
            if (!doc || !m || !hosts_.ask) return;
            const auto rack = static_cast<std::uint16_t>(rack_);
            const auto slot = static_cast<std::int16_t>(slot_);
            hosts_.ask("Remplacer " + m->reference, "La r\xC3\xA9" "f\xC3\xA9rence du nouveau module (rack " + std::to_string(rack) + ", emplacement "
                           + std::to_string(slot) + "), comme dans le catalogue : BMXDDO1602, BMXDDI3202K...",
                       m->reference, [this, doc, rack, slot](const std::string& reference) {
                           std::string ref = upper(reference);
                           ref.erase(std::remove(ref.begin(), ref.end(), ' '), ref.end());
                           if (ref.empty()) return;
                           hosts_.apply(std::make_unique<project::ReplaceModuleCommand>(doc, rack, slot, ref));
                       });
            return;
        }
        case ARemove:
            if (doc && rack_ >= 0) {
                hosts_.apply(std::make_unique<project::RemoveModuleCommand>(doc, static_cast<std::uint16_t>(rack_), static_cast<std::int16_t>(slot_)));
                slot_ = -32768;
                refreshProperties();
            }
            return;
        case ACheck:
            showTab(TChannels);
            status(report_.hardware ? std::to_string(report_.addresses.size()) + " adresses dans le code \xC2\xB7 " + std::to_string(report_.faulty)
                                          + " sans module ou dans le mauvais sens."
                                    : std::string("Pas de .XHW dans le projet : rien \xC3\xA0 comparer (Importer le .XHW\xE2\x80\xA6)."));
            return;
        case AFaultyOnly:
            faultyOnly_ = !faultyOnly_;
            rebuildChannelRows();
            refreshProperties();
            return;
        case AShowModule: {
            const auto rows = channels_->selectedModelRows();
            if (rows.empty() || rows.front() >= channelRows_.size()) return;
            const auto& a = report_.addresses[channelRows_[rows.front()]];
            (void)selectModule(static_cast<std::uint16_t>(a.rack), static_cast<std::int16_t>(a.slot));
            return;
        }
        case AFindGap:
            showGap_ = !showGap_;
            if (showGap_ && zone_ < zones_.size() && zones_[zone_].gapSize() > 0) {
                word_ = zones_[zone_].gapFrom;
                hasWord_ = true;
            }
            memoryView_->invalidate();
            refreshProperties();
            return;
        case AZoneBits: showZone(0); return;
        case AZoneWords: showZone(1); return;
        case AZoneConstants: showZone(2); return;
        case ABounds: {
            if (zone_ >= zones_.size() || !hosts_.ask) return;
            const auto& m = zones_[zone_];
            const auto zone = zone_;
            hosts_.ask("Borner la zone " + m.prefix,
                       "Le d\xC3\xA9" "but et la fin de la lecture, s\xC3\xA9par\xC3\xA9s par un tiret (ex. 0-" + std::to_string(m.size ? m.size - 1 : 0)
                           + ") ; la zone a " + thousands(m.size) + " " + m.units + ". Vide : toute la zone.",
                       std::to_string(m.from) + "-" + std::to_string(m.to), [this, zone](const std::string& text) {
                           std::string t;
                           for (const char ch : text) if (ch != ' ' && ch != '%') t += ch;
                           if (t.empty()) {
                               setWindow(zone, {});
                               return;
                           }
                           const auto dash = t.find_first_of("-:");
                           domain::MemoryWindow w;
                           w.set = true;
                           // « MW100 » ou « 100 » : les lettres sautees.
                           const auto number = [](const std::string& part) {
                               std::string digits;
                               for (const char ch : part) if (std::isdigit(static_cast<unsigned char>(ch))) digits += ch;
                               return digits.empty() ? -1L : std::strtol(digits.c_str(), nullptr, 10);
                           };
                           const long a = number(t.substr(0, dash));
                           const long b = dash == std::string::npos ? a : number(t.substr(dash + 1));
                           if (a < 0 || b < 0) {
                               if (hosts_.status) hosts_.status("Bornes refus\xC3\xA9" "es : \xC2\xAB d\xC3\xA9" "but-fin \xC2\xBB, par exemple 0-1023.");
                               return;
                           }
                           w.from = static_cast<std::uint32_t>(std::min(a, b));
                           w.to = static_cast<std::uint32_t>(std::max(a, b));
                           setWindow(zone, w);
                       });
            return;
        }
        case AWholeZone:
            setWindow(zone_, {});
            return;
        case AOverlapsOnly:
            overlapsOnly_ = !overlapsOnly_;
            memoryView_->invalidate();
            return;
        default: return;
    }
}

void ConfigurationPane::showZone(std::size_t zone) {
    if (zone >= 3) return;
    if (zone != zone_) {
        zone_ = zone;
        hasWord_ = false;
        showGap_ = false;
        memoryView_->resetScroll();
    }
    if (currentTab() != TMemory) tabs_->setCurrentIndex(TMemory);
    memoryView_->invalidate();
    refreshProperties();
    updateHint();
}

// Lot API 5 : des bornes, en une commande (Ctrl+Z les retire).
void ConfigurationPane::setWindow(std::size_t zone, domain::MemoryWindow window) {
    auto doc = hosts_.project ? hosts_.project() : nullptr;
    if (!doc || !hosts_.apply || zone >= doc->memoryWindows.size()) return;
    if (doc->memoryWindows[zone] == window) return;
    hasWord_ = false;
    memoryView_->resetScroll();
    hosts_.apply(std::make_unique<project::SetMemoryWindowCommand>(doc, static_cast<domain::MemoryZone>(zone), window));
}

void ConfigurationPane::refreshProperties() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (!p) return;
    std::vector<PG::Category> cats;
    switch (currentTab()) {
        case TProcessor: {
            PG::Category c;
            c.name = "Le processeur";
            c.properties.push_back({"R\xC3\xA9" "f\xC3\xA9rence", p->hardware.cpuReference.empty() ? std::string("?") : p->hardware.cpuReference,
                                    PG::ValueType::ReadOnly, {}, {}, nullptr});
            c.properties.push_back({"Famille", p->hardware.family, PG::ValueType::ReadOnly, {}, {}, nullptr});
            c.properties.push_back({"Racks", std::to_string(p->hardware.racks.size()) + (p->hardware.inferred ? " (d\xC3\xA9" "duit du .XPG : pas de .XHW)" : ""),
                                    PG::ValueType::ReadOnly, {}, {}, nullptr});
            c.properties.push_back({"Modules", std::to_string(p->hardware.totalModules()), PG::ValueType::ReadOnly, {}, {}, nullptr});
            cats.push_back(std::move(c));
            break;
        }
        case TRacks: {
            const auto* m = rack_ >= 0 ? moduleAt(static_cast<std::uint16_t>(rack_), static_cast<std::int16_t>(slot_)) : nullptr;
            if (!m) {
                PG::Category c;
                c.name = "Racks et modules";
                c.properties.push_back({"Racks", std::to_string(p->hardware.racks.size()), PG::ValueType::ReadOnly, {}, {}, nullptr});
                c.properties.push_back({"Modules", std::to_string(p->hardware.totalModules()), PG::ValueType::ReadOnly, {}, {}, nullptr});
                c.properties.push_back({"Adresses fautives", std::to_string(report_.faulty), PG::ValueType::ReadOnly,
                                        "Des adresses du code sans module \xC3\xA0 leur emplacement, ou dans le mauvais sens : V\xC3\xA9rifier les adresses.", {}, nullptr});
                c.properties.push_back({"Choisir un module", "un clic sur sa carte", PG::ValueType::ReadOnly, {}, {}, nullptr});
                cats.push_back(std::move(c));
                break;
            }
            cats = buildModuleProperties(*p, *m);
            PG::Category used;
            const io::ModuleUse* use = nullptr;
            for (const auto& u : report_.modules)
                if (u.rack == rack_ && u.slot == slot_) use = &u;
            used.name = "Voies utilis\xC3\xA9" "es par le programme" + (use && use->total ? "  " + std::to_string(use->used) + "/" + std::to_string(use->total) : std::string{});
            for (const auto& a : report_.addresses) {
                if (a.rack != rack_ || a.slot != slot_) continue;
                used.properties.push_back({a.text, joined(a.users, 3) + (a.faulty() ? "  (" + io::verdict(a) + ")" : std::string{}), PG::ValueType::ReadOnly, {}, {}, nullptr});
                if (used.properties.size() >= 40) break;
            }
            if (used.properties.empty())
                used.properties.push_back({"Aucune", "le code ne nomme aucune voie de ce module", PG::ValueType::ReadOnly, {}, {}, nullptr});
            cats.push_back(std::move(used));
            break;
        }
        case TChannels: {
            const auto rows = channels_->selectedModelRows();
            if (rows.empty() || rows.front() >= channelRows_.size()) {
                PG::Category c;
                c.name = "Voies et adresses";
                c.properties.push_back({"Adresses dans le code", std::to_string(report_.addresses.size()), PG::ValueType::ReadOnly, {}, {}, nullptr});
                c.properties.push_back({"Fautives", std::to_string(report_.faulty), PG::ValueType::ReadOnly,
                                        "Sans module \xC3\xA0 leur emplacement, dans le mauvais sens, ou une voie au-del\xC3\xA0 de celles du module.", {}, nullptr});
                if (!report_.hardware)
                    c.properties.push_back({"Pas de .XHW", "rien \xC3\xA0 comparer : Importer le .XHW\xE2\x80\xA6", PG::ValueType::ReadOnly, {}, {}, nullptr});
                cats.push_back(std::move(c));
                break;
            }
            const auto& a = report_.addresses[channelRows_[rows.front()]];
            PG::Category c;
            c.name = "Adresse choisie";
            c.properties.push_back({"Adresse", a.text, PG::ValueType::ReadOnly, {}, {}, nullptr});
            c.properties.push_back({"Sens", std::string(a.direction == io::Direction::Input ? "entr\xC3\xA9" "e" : "sortie") + (a.word ? " (mot)" : " TOR"),
                                    PG::ValueType::ReadOnly, {}, {}, nullptr});
            c.properties.push_back({"Module", a.module.empty() ? "aucun : rack " + std::to_string(a.rack) + ", emplacement " + std::to_string(a.slot) : a.module,
                                    PG::ValueType::ReadOnly, {}, {}, nullptr});
            if (!a.declaredAs.empty()) c.properties.push_back({"Variable", a.declaredAs, PG::ValueType::ReadOnly, {}, {}, nullptr});
            c.properties.push_back({a.writes ? "\xC3\x89" "crite par" : "Lue par", joined(a.users, 6) + (a.writes ? " (" + std::to_string(a.writes) + " fois)" : std::string{}),
                                    PG::ValueType::ReadOnly, {}, {}, nullptr});
            cats.push_back(std::move(c));
            // Que faire : des phrases courtes, une par ligne (la colonne est
            // etroite) ; la phrase entiere dans la bande d'aide, au clic.
            PG::Category what;
            what.name = "Que faire";
            std::vector<std::pair<std::string, std::string>> steps;
            switch (a.status) {
                case io::Address::Status::NoModule:
                    steps = {{"Le constat", "rien \xC3\xA0 l'emplacement " + std::to_string(a.rack) + "." + std::to_string(a.slot)},
                             {"Soit", "le .XHW n'est pas le bon : Importer le .XHW\xE2\x80\xA6"},
                             {"Soit", "le module manque : Module (Racks et modules)"},
                             {"Soit", "l'adresse du code est fausse : Utilis\xC3\xA9" "e par"}};
                    break;
                case io::Address::Status::WrongDirection:
                    steps = {{"Le constat", a.module + " travaille dans l'autre sens"},
                             {"Soit", "la r\xC3\xA9" "f\xC3\xA9rence est fausse : Remplacer\xE2\x80\xA6"},
                             {"Soit", "l'adresse du code est fausse (%I \xE2\x86\x94 %Q)"}};
                    break;
                case io::Address::Status::NoSuchChannel:
                    steps = {{"Le constat", a.detail}, {"\xC3\x80 faire", "v\xC3\xA9rifier la voie dans le code"}};
                    break;
                case io::Address::Status::NoHardware:
                    steps = {{"\xC3\x80 faire", "Importer le .XHW\xE2\x80\xA6 pour comparer"}};
                    break;
                default:
                    steps = {{"Rien", "le module est l\xC3\xA0, dans le bon sens"}, {"", "et la voie existe"}};
                    break;
            }
            std::string whole;
            for (const auto& [k, v] : steps) whole += (whole.empty() ? "" : " ; ") + (k.empty() ? v : k + " : " + v);
            for (const auto& [k, v] : steps) what.properties.push_back({k, v, PG::ValueType::ReadOnly, whole, {}, nullptr});
            cats.push_back(std::move(what));
            break;
        }
        case TNetwork: {
            PG::Category c;
            c.name = "R\xC3\xA9seau";
            const auto rows = network_->selectedModelRows();
            if (!rows.empty() && rows.front() < ports_.size()) {
                const auto& port = ports_[rows.front()];
                c.properties.push_back({"Module", port.module, PG::ValueType::ReadOnly, {}, {}, nullptr});
                c.properties.push_back({"Emplacement", "rack " + std::to_string(port.rack) + ", emplacement " + std::to_string(port.slot) + ", voie " + std::to_string(port.channel),
                                        PG::ValueType::ReadOnly, {}, {}, nullptr});
                c.properties.push_back({"Protocole", port.protocol, PG::ValueType::ReadOnly, {}, {}, nullptr});
                c.properties.push_back({"T\xC3\xA2" "che", port.task, PG::ValueType::ReadOnly, {}, {}, nullptr});
            }
            c.properties.push_back({"Adresses IP", "pas dans le .XHW", PG::ValueType::ReadOnly,
                                    "Le .XHW d\xC3\xA9" "crit les modules et leurs voies ; les adresses IP se r\xC3\xA8glent dans Control Expert (et l'IHM les a dans Configuration > Communication).",
                                    {}, nullptr});
            cats.push_back(std::move(c));
            break;
        }
        case TMemory: {
            if (zone_ >= zones_.size()) break;
            const auto& m = zones_[zone_];
            const bool editable = hosts_.project && hosts_.project() != nullptr;
            const auto* v = hasWord_ ? io::variableAt(m, word_) : nullptr;
            PG::Category c;
            if (v) {
                c.name = "Variable choisie";
                c.properties.push_back({"Nom", v->name, PG::ValueType::ReadOnly, {}, {}, nullptr});
                c.properties.push_back({"Type", v->type, PG::ValueType::ReadOnly, {}, {}, nullptr});
                c.properties.push_back({"Adresse", v->words > 1 ? m.cell(v->first) + " \xC3\xA0 " + m.cell(v->first + v->words - 1) : m.cell(v->first),
                                        PG::ValueType::ReadOnly, {}, {}, nullptr});
                c.properties.push_back({m.zone == domain::MemoryZone::Bits ? "Bits" : "Mots", std::to_string(v->words), PG::ValueType::ReadOnly, {}, {}, nullptr});
                cats.push_back(std::move(c));
            } else if (hasWord_) {
                c.name = m.cell(word_);
                const bool direct = std::binary_search(m.direct.begin(), m.direct.end(), word_);
                const bool written = std::binary_search(m.written.begin(), m.written.end(), word_);
                std::string state = direct ? (written ? "\xC3\xA9" "crite en direct par le code (elle prend sa place)" : "lue en direct par le code, sans variable situ\xC3\xA9" "e")
                                           : std::string("aucune variable situ\xC3\xA9" "e ici");
                c.properties.push_back({direct ? (written ? "\xC3\x89" "crite" : "Lue") : "Libre", state, PG::ValueType::ReadOnly, {}, {}, nullptr});
                for (const auto& in : m.indexed)
                    if (word_ >= in.lo && word_ <= in.hi)
                        c.properties.push_back({in.dynamic ? "Index\xC3\xA9" "e (dynamique)" : "Index\xC3\xA9" "e", in.text + " \xC2\xB7 " + in.section + " \xC2\xB7 " + in.why,
                                                PG::ValueType::ReadOnly, {}, {}, nullptr});
                cats.push_back(std::move(c));
            }
            // La zone : sa taille, ses bornes (modifiables : une commande), son utilisation.
            PG::Category z;
            z.name = "Zone " + m.prefix;
            z.properties.push_back({"Taille", thousands(m.size) + " " + m.units + (m.configured ? "" : " (d\xC3\xA9" "duite)"), PG::ValueType::ReadOnly,
                                    m.configured ? "La taille configur\xC3\xA9" "e au .XHW (numberInternalBit, numberInternalWord, numberConstantWord)."
                                                 : "Pas de .XHW : jusqu'\xC3\xA0 la derni\xC3\xA8re cellule employ\xC3\xA9" "e, arrondie \xC3\xA0 la centaine. Importer le .XHW pour la vraie taille.",
                                    {}, nullptr});
            const auto zone = zone_;
            const auto bound = [this, zone, editable](bool start) -> std::function<bool(std::string_view)> {
                if (!editable) return nullptr;
                return [this, zone, start](std::string_view text) {
                    if (zone >= zones_.size()) return false;
                    const auto& cur = zones_[zone];
                    std::string digits;
                    for (const char ch : text) if (std::isdigit(static_cast<unsigned char>(ch))) digits += ch;
                    if (digits.empty()) return false;
                    const auto n = static_cast<std::uint32_t>(std::min(std::strtoul(digits.c_str(), nullptr, 10), 1048575ul));
                    domain::MemoryWindow w;
                    w.set = true;
                    w.from = start ? n : cur.from;
                    w.to = start ? cur.to : n;
                    if (w.to < w.from) std::swap(w.from, w.to);
                    setWindow(zone, w);
                    return true;
                };
            };
            z.properties.push_back({"D\xC3\xA9" "but de lecture", std::to_string(m.from), editable ? PG::ValueType::Integer : PG::ValueType::ReadOnly,
                                    "La premi\xC3\xA8re cellule lue (" + m.prefix + "). Le plan, l'utilisation et la place libre se comptent entre les bornes.", {}, bound(true)});
            z.properties.push_back({"Fin de lecture", std::to_string(m.to), editable ? PG::ValueType::Integer : PG::ValueType::ReadOnly,
                                    "La derni\xC3\xA8re cellule lue. Au-del\xC3\xA0 de la taille configur\xC3\xA9" "e : pour voir ce qui d\xC3\xA9" "borde.", {}, bound(false)});
            z.properties.push_back({"Utilisation", thousands(m.used) + " / " + thousands(m.span()) + " " + m.units + " \xC2\xB7 " + io::percentText(m.percent()),
                                    PG::ValueType::ReadOnly, "Les cellules employ\xC3\xA9" "es entre les bornes : par une variable situ\xC3\xA9" "e, ou par le code en direct.", {}, nullptr});
            z.properties.push_back({"Variables situ\xC3\xA9" "es", std::to_string(m.variables.size()), PG::ValueType::ReadOnly, {}, {}, nullptr});
            z.properties.push_back({"En direct dans le code", thousands(m.direct.size()) + " " + m.units + " (" + thousands(m.written.size()) + " \xC3\xA9" "crits), "
                                                                  + std::to_string(m.directReferences) + " fois",
                                    PG::ValueType::ReadOnly, "Le code emploie aussi des " + m.prefix + " sans variable (" + m.prefix + "100 := 50 prend de la m\xC3\xA9moire comme une variable) : gris dans le plan.",
                                    {}, nullptr});
            if (!m.dynamicCells.empty() || m.dynamicUnknown)
                z.properties.push_back({"Index\xC3\xA9" "es dynamiques", thousands(m.dynamicCells.size()) + " " + m.units + " en plages \xC2\xB7 " + std::to_string(m.dynamicUnknown) + " sans plage",
                                        PG::ValueType::ReadOnly, "%MW0[index] : la cellule d\xC3\xA9pend de l'indice. Une boucle FOR donne sa plage (hachur\xC3\xA9" "e) ; sinon seule la base est compt\xC3\xA9" "e (cadre orange).",
                                        {}, nullptr});
            z.properties.push_back({"Plus grande place libre", m.gapSize() ? m.cell(m.gapFrom) + " \xE2\x86\x92 " + m.cell(m.gapTo) + " (" + thousands(m.gapSize()) + " " + m.units + ")"
                                                                          : std::string("aucune"),
                                    PG::ValueType::ReadOnly, "Dans les bornes : la place pour ajouter des variables sans en d\xC3\xA9placer.", {}, nullptr});
            z.properties.push_back({"Chevauchements", std::to_string(m.overlaps.size()), PG::ValueType::ReadOnly, {}, {}, nullptr});
            cats.push_back(std::move(z));
            if (!m.indexed.empty()) {
                // Les adresses indexees : l'indice evalue, ou dynamique (sa plage, si
                // une boucle FOR la donne ; sinon inconnue - le plan ne peut qu'avertir).
                PG::Category x;
                std::size_t dynamic = 0;
                for (const auto& in : m.indexed) dynamic += in.dynamic ? 1u : 0u;
                x.name = "Adresses index\xC3\xA9" "es  " + std::to_string(m.indexed.size()) + (dynamic ? "  (" + std::to_string(dynamic) + " dynamiques)" : std::string{});
                for (std::size_t i = 0; i < m.indexed.size() && i < 30; ++i) {
                    const auto& in = m.indexed[i];
                    const std::string cells = in.lo == in.hi ? m.cell(in.lo) : m.cell(in.lo) + " \xC3\xA0 " + m.cell(in.hi);
                    x.properties.push_back({in.text, (in.dynamic && !in.bounded ? "? \xC2\xB7 " : "") + cells + " \xC2\xB7 " + in.why + (in.write ? " \xC2\xB7 \xC3\xA9" "crite" : ""),
                                            PG::ValueType::ReadOnly,
                                            in.section + " : " + (in.constant ? "l'indice s'\xC3\xA9value, la cellule est fixe."
                                                                              : in.bounded ? "l'indice change \xC3\xA0 chaque tour de la boucle : la plage enti\xC3\xA8re est compt\xC3\xA9" "e."
                                                                              : in.estimated ? "une variable de l'indice est inconnue \xC3\xA0 l'avance : la plage est estim\xC3\xA9" "e avec elle \xC3\xA0 0, compt\xC3\xA9" "e et marqu\xC3\xA9" "e \xC2\xAB ? \xC2\xBB - \xC3\xA0 v\xC3\xA9rifier."
                                                                                             : "l'indice ne s'\xC3\xA9value pas : la m\xC3\xA9moire touch\xC3\xA9" "e d\xC3\xA9pend de l'ex\xC3\xA9" "cution (rien n'est compt\xC3\xA9)."),
                                            {}, nullptr});
                }
                cats.push_back(std::move(x));
            }
            if (!m.outside.empty()) {
                PG::Category o;
                o.name = "Hors zone  " + std::to_string(m.outside.size());
                for (std::size_t i = 0; i < m.outside.size() && i < 20; ++i)
                    o.properties.push_back({"", m.outside[i], PG::ValueType::ReadOnly,
                                            "Situ\xC3\xA9" "e au-del\xC3\xA0 des " + thousands(m.size) + " " + m.units + " que le .XHW configure : agrandir la zone dans Control Expert, ou la d\xC3\xA9placer.",
                                            {}, nullptr});
                cats.push_back(std::move(o));
            }
            // Les trois zones d'un coup d'oeil.
            PG::Category all;
            all.name = "Les trois zones";
            for (const auto& other : zones_)
                all.properties.push_back({other.prefix, io::percentText(other.percent()) + " \xC2\xB7 " + thousands(other.used) + " / " + thousands(other.span()) + " " + other.units
                                                          + (other.outside.empty() ? std::string{} : " \xC2\xB7 " + std::to_string(other.outside.size()) + " hors zone"),
                                          PG::ValueType::ReadOnly, {}, {}, nullptr});
            cats.push_back(std::move(all));
            break;
        }
        default: break;
    }
    props_->setCategories(std::move(cats));
}

void ConfigurationPane::updateHint() {
    if (!frame_) return;
    std::string text;
    ui::Tone tone = ui::Tone::None;
    switch (currentTab()) {
        case TProcessor: text = "Le processeur et sa m\xC3\xA9moire, tels que l'export les d\xC3\xA9" "crit."; break;
        case TRacks:
        case TChannels:
            if (!report_.hardware) text = "Pas de .XHW : les racks sont d\xC3\xA9" "duits du processeur ; Importer le .XHW\xE2\x80\xA6 pour v\xC3\xA9rifier les adresses.";
            else if (report_.faulty) {
                text = std::to_string(report_.addresses.size()) + " adresses topologiques dans le code \xC2\xB7 " + std::to_string(report_.faulty)
                     + " sans module ou dans le mauvais sens (Voies et adresses).";
                tone = ui::Tone::Warning;
            } else {
                text = std::to_string(report_.addresses.size()) + " adresses topologiques dans le code : toutes ont leur module.";
                tone = ui::Tone::Ok;
            }
            break;
        case TNetwork: text = std::to_string(ports_.size()) + " ports de communication d\xC3\xA9" "clar\xC3\xA9s par le .XHW ; les adresses IP se r\xC3\xA8glent dans Control Expert."; break;
        default: {
            text = "Plan m\xC3\xA9moire";
            bool trouble = false;
            for (const auto& z : zones_) {
                text += (z.zone == domain::MemoryZone::Bits ? " : " : " \xC2\xB7 ") + z.prefix + " " + io::percentText(z.percent());
                trouble = trouble || !z.overlaps.empty() || !z.outside.empty();
            }
            text += " \xC2\xB7 un clic sur une carte : sa zone ; sur un carr\xC3\xA9 : sa variable ; Bornes\xE2\x80\xA6 : ce qui est lu.";
            tone = trouble ? ui::Tone::Warning : ui::Tone::Ok;
            break;
        }
    }
    if (text != frame_->hint()) frame_->setHint(text, tone);
}

void ConfigurationPane::onLayout() {
    const auto b = bounds();
    const float rightW = std::clamp(b.w * 0.24f, 260.f, 400.f);
    tabs_->setBounds({b.x, b.y, b.w - rightW - 1.f, b.h});
    props_->setBounds({b.right() - rightW, b.y, rightW, b.h});
}

void ConfigurationPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.windowBg);
    ctx.r.fillRect({props_->bounds().x - 1.f, bounds().y, 1.f, bounds().h}, ctx.theme.color.border);
}

} // namespace app
