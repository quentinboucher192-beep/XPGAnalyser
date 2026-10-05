// =============================================================================
//  app/SimDebugPane.cpp - lot API 8 : l'onglet Simulation > Debogage
// -----------------------------------------------------------------------------
//  Ce que l'onglet montre : voir SimDebugPane.hpp. Ici, comment il reste
//  juste et leger :
//
//   * RIEN NE SE LIT DANS UN SIGNAL. SimulationHost emet `changed` a chaque
//     image ou un cycle a tourne, du fond de sa boucle : les signaux ne font que
//     lever des drapeaux, et tick() (a chaque image, onglet a l'ecran) relit ce
//     qui doit l'etre - l'etat et les points d'arret tout de suite, les valeurs
//     quatre fois par seconde en marche, une fois en pause.
//   * LES VALEURS A DROITE DU CODE ne se lisent que pour les lignes a l'ecran
//     (une section peut avoir 3 000 lignes) ; l'etat des commentaires (* *) de
//     chaque ligne est calcule une fois, au chargement de la section.
//   * « EXECUTER JUSQU'A LA LIGNE » est un point d'arret provisoire, cache de la
//     liste et des marges, retire des que la simulation ne tourne plus.
// =============================================================================
#include "SimDebugPane.hpp"

#include "ApiPanes.hpp"
#include "SimulationHost.hpp"
#include "SimulationPane.hpp"      // frenchMessage, frenchPlace : la halte en francais
#include "hmi/HmiPanels.hpp"       // HmiToolStrip, HmiGlyph

#include "../domain/ExecutionOrder.hpp"
#include "../project/CrossReference.hpp"
#include "../sim/Runtime.hpp"
#include "../ui/Icons.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace app {

namespace mt = project::members;
namespace sd = simdebug;
using ui::RowIndex;

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kBody{14};
const gfx::FontId kLead{18};

constexpr std::size_t kNpos = static_cast<std::size_t>(-1);
constexpr double      kLiveEvery = 0.25;        // s : les valeurs, quatre fois par seconde en marche
constexpr std::size_t kMaxBars = 8;             // les barres du temps : les plus lentes
constexpr std::size_t kNotesPerLine = 4;        // les valeurs a droite d'une ligne
constexpr std::size_t kMaxWatchRows = 3000;     // les lignes depliees des espions
constexpr float       kTitleH = 26.f;

enum BreakColumn : std::size_t { BDot = 0, BWhere, BCondition, BHits, BRemove, BCount };
enum WatchColumn : std::size_t { WName = 0, WValue, WSince, WRemove, WCount };
enum TraceColumn : std::size_t { TRank = 0, TEntry, TSection, TStatements, TTime, TCount };
enum DotIcon : int { IDot = 1, IRing, ICondition, IHere };

const char* const kDot = " \xC2\xB7 ";               // " . "
const char* const kEllipsis = "\xE2\x80\xA6";        // ...
const char* const kDash = "\xE2\x80\x94";            // le tiret : pas de valeur
const char* const kCross = "\xC3\x97";               // la croix d'une ligne

std::vector<SimDebugPane*>& livePanes() {
    static std::vector<SimDebugPane*> panes;
    return panes;
}

unsigned char uc(char c) noexcept { return static_cast<unsigned char>(c); }

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(uc(c)));
    return out;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(uc(s[a]))) ++a;
    while (b > a && std::isspace(uc(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool startsWith(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string text(const domain::Project& p, domain::SymbolId id) { return std::string(p.strings.text(id)); }

// Un chemin sans la casse ni les indices : "armoires[i].x" et "Armoires[0].X"
// se rejoignent en "armoires[].x" (le code ecrit souvent l'indice en variable).
std::string shape(std::string_view s) {
    std::string out;
    int depth = 0;
    for (const char c : s) {
        if (c == '[') { if (depth++ == 0) out += '['; continue; }
        if (c == ']') { if (--depth == 0) out += ']'; continue; }
        if (depth > 0 || std::isspace(uc(c))) continue;
        out += static_cast<char>(std::tolower(uc(c)));
    }
    return out;
}

// ---- le dessin -----------------------------------------------------------------
void drawBold(gfx::IRenderer& r, gfx::Point at, std::string_view s, gfx::FontId f, gfx::Color c) {
    r.drawText(at, s, f, c);
    r.drawText({at.x + 0.6f, at.y}, s, f, c);
}

// Le debut d'un texte qui tient dans `width`, avec « ... » s'il est coupe (sans
// couper un caractere UTF-8).
std::string elide(const gfx::IRenderer& r, const std::string& s, gfx::FontId f, float width) {
    if (width <= 0.f) return {};
    if (r.measure(s, f).width <= width) return s;
    const float ell = r.measure(kEllipsis, f).width;
    std::size_t cut = s.size();
    while (cut > 0) {
        --cut;
        while (cut > 0 && (uc(s[cut]) & 0xC0) == 0x80) --cut;
        if (r.measure(std::string_view(s).substr(0, cut), f).width + ell <= width) break;
    }
    return s.substr(0, cut) + kEllipsis;
}

// Un paragraphe en lignes de `width` au plus (coupe aux espaces ; un mot trop
// long est coupe avec « ... »).
std::vector<std::string> wrap(const gfx::IRenderer& r, const std::string& s, gfx::FontId f, float width) {
    std::vector<std::string> out;
    std::string line;
    std::size_t i = 0;
    while (i <= s.size()) {
        const auto sp = s.find(' ', i);
        const std::string word = s.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && r.measure(candidate, f).width > width) {
            out.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        if (sp == std::string::npos) break;
        i = sp + 1;
    }
    if (!line.empty()) out.push_back(line);
    for (auto& l : out) l = elide(r, l, f, width);
    return out;
}

void panel(gfx::IRenderer& r, const ui::Theme& th, const gfx::Rect& box) {
    r.fillRoundedRect(box, th.color.border, 6.f);
    r.fillRoundedRect({box.x + 1.f, box.y + 1.f, box.w - 2.f, box.h - 2.f}, th.color.panelBg, 5.f);
}

// Un rond de point d'arret (les tables le dessinent par leur peintre d'icones).
void paintDot(gfx::IRenderer& r, int icon, const gfx::Rect& box, const ui::Theme& th) {
    const float cx = box.x + box.w * 0.5f, cy = box.y + box.h * 0.5f;
    const auto red = th.color.error;
    r.fillRoundedRect({cx - 6.f, cy - 6.f, 12.f, 12.f}, red, 6.f);
    if (icon == IRing) r.fillRoundedRect({cx - 4.f, cy - 4.f, 8.f, 8.f}, th.color.panelBg, 4.f);
    if (icon == ICondition) r.fillRoundedRect({cx - 2.f, cy - 2.f, 4.f, 4.f}, gfx::Color{255, 255, 255, 230}, 2.f);
    if (icon == IHere) {
        const auto amber = th.color.warning;
        r.fillRect({cx - 9.f, cy - 2.f, 7.f, 4.f}, amber);
        const gfx::Vertex head[3] = {{{cx - 3.f, cy - 6.f}, amber}, {{cx - 3.f, cy + 6.f}, amber}, {{cx + 4.f, cy}, amber}};
        r.fillTriangles(head, 3);
    }
}

// Le theme du moment, pour les peintres d'icones des tables (ils n'ont que la
// couleur) : celui du dernier dessin de l'onglet.
const ui::Theme* g_theme = nullptr;

} // namespace

// ============================================================ les points d'arret ====
class SimDebugPane::BreakModel final : public ui::ITableModel {
public:
    explicit BreakModel(SimDebugPane& pane) : pane_(pane) {}
    void reset() { modelReset->emit(); }
    [[nodiscard]] std::size_t rowCount() const override { return pane_.bps_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return BCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        switch (c) {
            case BWhere:     return "Section : ligne";
            case BCondition: return "Condition";
            case BHits:      return "Passages";
            default:         return {};
        }
    }
    [[nodiscard]] std::string cellText(RowIndex row, std::size_t c) const override {
        if (row >= pane_.bps_.size()) return {};
        const auto& b = pane_.bps_[row];
        switch (c) {
            case BWhere:     // la maquette : "SFC_PurgeA : 42" ; abregee au milieu si la colonne est etroite
                if (row < pane_.bpWhereShown_.size() && !pane_.bpWhereShown_[row].empty()) return pane_.bpWhereShown_[row];
                return b.section + " : " + std::to_string(b.line);
            case BCondition: return b.condition.empty() ? std::string("sans condition") : b.condition;
            case BHits:      return sd::grouped(b.hits);
            case BRemove:    return kCross;
            default:         return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex row, std::size_t c) const override {
        ui::CellStyle s;
        if (row >= pane_.bps_.size()) return s;
        const auto& b = pane_.bps_[row];
        const bool here = pane_.isHit(b.id);
        if (c == BDot) s.customIcon = here ? IHere : !b.enabled ? IRing : b.condition.empty() ? IDot : ICondition;
        if (!b.enabled) s.fgTone = ui::Tone::Muted;
        if (c == BWhere && here) s.bold = true;
        if (c == BCondition) {
            s.monospace = !b.condition.empty();
            if (b.condition.empty()) s.fgTone = ui::Tone::Muted;
            if (!b.note.empty()) s.fgTone = ui::Tone::Warning;
        }
        if (c == BRemove) s.fgTone = ui::Tone::Muted;
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool editable(RowIndex, std::size_t c) const override { return c == BCondition; }
    bool setCellText(RowIndex row, std::size_t c, std::string_view value) override {
        if (c != BCondition || row >= pane_.bps_.size()) return false;
        std::string v = trim(value);
        if (lower(v) == "toujours" || lower(v) == "sans condition") v.clear();
        return pane_.setBreakpointCondition(static_cast<std::size_t>(row) + 1, v);
    }
    [[nodiscard]] std::string rowTooltip(RowIndex row) const override {
        if (row >= pane_.bps_.size()) return {};
        const auto& b = pane_.bps_[row];
        std::string tip = "Point d'arr\xC3\xAAt " + std::to_string(row + 1) + " : " + b.section + ", ligne " + std::to_string(b.line)
                        + (b.enabled ? "" : " (d\xC3\xA9sactiv\xC3\xA9)");
        tip += b.condition.empty() ? "\nSans condition : il arr\xC3\xAAte \xC3\xA0 chaque passage." : "\nSeulement si " + b.condition + ".";
        if (b.hits > 0) tip += "\nIl a arr\xC3\xAAt\xC3\xA9 le programme " + sd::plural(b.hits, "fois", "fois") + ".";
        if (!b.note.empty()) tip += "\n" + b.note;
        tip += "\nLe rond : l'activer / le d\xC3\xA9sactiver \xC2\xB7 double-clic sur la condition : la modifier \xC2\xB7 la croix : l'enlever \xC2\xB7 "
               "double-clic ailleurs : son code.";
        return tip;
    }
private:
    SimDebugPane& pane_;
};

// Un clic sur le rond l'active ou le desactive ; sur la croix, l'enleve ;
// Suppr enleve les points choisis.
class SimDebugPane::BreakTable final : public ui::TableView {
public:
    BreakTable(SimDebugPane& pane, std::string id) : ui::TableView(std::move(id)), pane_(pane) {}
protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && !cellEditing()) {
            for (std::size_t v = 0; v < visibleRowCount(); ++v) {
                gfx::Rect cell;
                const auto number = static_cast<std::size_t>(viewRow(v)) + 1;
                if (cellRect(v, BDot, cell) && cell.contains(d->pos)) {
                    if (number <= pane_.bps_.size()) (void)pane_.setBreakpointEnabled(number, !pane_.bps_[number - 1].enabled);
                    return ui::EventResult::Consumed;
                }
                if (cellRect(v, BRemove, cell) && cell.contains(d->pos)) {
                    (void)pane_.removeBreakpoint(number);
                    return ui::EventResult::Consumed;
                }
            }
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && !cellEditing() && k->key == ui::Key::Delete && k->mods.none()) {
            auto rows = selectedModelRows();
            std::sort(rows.rbegin(), rows.rend());
            for (const auto r : rows) (void)pane_.removeBreakpoint(static_cast<std::size_t>(r) + 1);
            return ui::EventResult::Consumed;
        }
        return ui::TableView::onEvent(ev);
    }
private:
    SimDebugPane& pane_;
};

// ================================================================== les espions ====
class SimDebugPane::WatchModel final : public ui::ITableModel {
public:
    explicit WatchModel(SimDebugPane& pane) : pane_(pane) {}
    void reset() { modelReset->emit(); }
    [[nodiscard]] std::size_t rowCount() const override { return pane_.watchRows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return WCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        switch (c) {
            case WName:  return "Espion";
            case WValue: return "Valeur";
            case WSince: return "Depuis";
            default:     return {};
        }
    }
    [[nodiscard]] std::string cellText(RowIndex row, std::size_t c) const override {
        if (row >= pane_.watchRows_.size()) return {};
        const auto& r = pane_.watchRows_[row];
        switch (c) {
            case WName:   return r.depth == 0 ? pane_.watches_[static_cast<std::size_t>(r.watch)].path : r.node.label;
            case WValue:  return r.value;
            case WSince:  return r.since;
            case WRemove: return r.depth == 0 ? std::string(kCross) : std::string{};
            default:      return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex row, std::size_t c) const override {
        ui::CellStyle s;
        if (row >= pane_.watchRows_.size()) return s;
        const auto& r = pane_.watchRows_[row];
        const auto& w = pane_.watches_[static_cast<std::size_t>(r.watch)];
        if (c == WName) {
            s.indent = static_cast<float>(r.depth) * 16.f;
            s.expander = r.expandable ? (r.open ? 1 : 0) : -1;
            s.icon = r.node.path.rfind('%', 0) == 0 ? ui::Icon::LocatedVariable : ui::Icon::Variable;
            s.iconTone = r.depth == 0 ? ui::Tone::Accent : ui::Tone::Muted;
        }
        if (c == WValue) {
            s.monospace = true;
            if (!w.known) s.fgTone = ui::Tone::Muted;
            else if (r.depth == 0 && w.changed && pane_.scanSeen_ != ~std::uint64_t{0} && w.changedScan + 1 >= pane_.scanSeen_)
                s.fgTone = ui::Tone::Accent;             // vient de changer
        }
        if (c == WSince || c == WRemove) s.fgTone = ui::Tone::Muted;
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(RowIndex row) const override {
        if (row >= pane_.watchRows_.size()) return {};
        const auto& r = pane_.watchRows_[row];
        const auto& w = pane_.watches_[static_cast<std::size_t>(r.watch)];
        std::string tip = r.node.path + (r.node.type.empty() ? std::string{} : " : " + r.node.type);
        if (!r.value.empty()) tip += "\n= " + r.value;
        if (r.depth == 0) tip += "\n" + r.since;
        if (!w.known) tip += "\nLa simulation ne la conna\xC3\xAEt pas encore (pas pr\xC3\xA9par\xC3\xA9" "e, ou un chemin mal \xC3\xA9" "crit).";
        tip += "\nUn clic : \xC2\xAB Qui a \xC3\xA9" "crit ? \xC2\xBB la suit \xC2\xB7 la fl\xC3\xA8" "che : la d\xC3\xA9plier \xC2\xB7 la croix : retirer l'espion.";
        return tip;
    }
private:
    SimDebugPane& pane_;
};

class SimDebugPane::WatchTable final : public ui::TableView {
public:
    WatchTable(SimDebugPane& pane, std::string id) : ui::TableView(std::move(id)), pane_(pane) {}
protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
            for (std::size_t v = 0; v < visibleRowCount(); ++v) {
                gfx::Rect cell;
                if (!cellRect(v, WRemove, cell) || !cell.contains(d->pos)) continue;
                const auto r = static_cast<std::size_t>(viewRow(v));
                if (r < pane_.watchRows_.size() && pane_.watchRows_[r].depth == 0) {
                    const std::string path = pane_.watches_[static_cast<std::size_t>(pane_.watchRows_[r].watch)].path;
                    (void)pane_.removeWatch(path);
                    return ui::EventResult::Consumed;
                }
            }
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && k->key == ui::Key::Delete && k->mods.none()) {
            std::vector<std::string> paths;
            for (const auto r : selectedModelRows())
                if (r < pane_.watchRows_.size() && pane_.watchRows_[r].depth == 0)
                    paths.push_back(pane_.watches_[static_cast<std::size_t>(pane_.watchRows_[r].watch)].path);
            for (const auto& p : paths) (void)pane_.removeWatch(p);
            if (!paths.empty()) return ui::EventResult::Consumed;
        }
        return ui::TableView::onEvent(ev);
    }
private:
    SimDebugPane& pane_;
};

// ============================================================ la trace du cycle ====
class SimDebugPane::TraceModel final : public ui::ITableModel {
public:
    explicit TraceModel(SimDebugPane& pane) : pane_(pane) {}
    void setRows(std::vector<sd::TraceRow> rows) {
        rows_ = std::move(rows);
        modelReset->emit();
    }
    [[nodiscard]] const std::vector<sd::TraceRow>& rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return TCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        switch (c) {
            case TRank:       return "#";
            case TEntry:      return "Entr\xC3\xA9" "e de MAST";
            case TSection:    return "Section";
            case TStatements: return "Instructions";
            case TTime:       return "Temps";
            default:          return {};
        }
    }
    [[nodiscard]] std::string cellText(RowIndex row, std::size_t c) const override {
        if (row >= rows_.size()) return {};
        const auto& r = rows_[row];
        switch (c) {
            case TRank:       return std::to_string(r.rank);
            case TEntry:      return r.firstOfEntry ? r.entry : std::string{};
            case TSection:    return r.section;
            case TStatements: return pane_.plannedTrace_ || !r.active ? std::string(kDash) : sd::grouped(r.statements);
            // 1.10.2 : sa condition d'activation etait fausse - elle n'a pas tourne.
            case TTime:       return pane_.plannedTrace_ ? std::string(kDash)
                                   : !r.active ? "inactive (" + r.condition + " faux)" : sd::millis(r.micros);
            default:          return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex row, std::size_t c) const override {
        ui::CellStyle s;
        if (row >= rows_.size()) return s;
        const auto& r = rows_[row];
        const auto* h = pane_.host();
        const bool here = h && h->state() == SimulationHost::State::Paused && h->lastBreakHit()
                       && sd::sameSection(h->lastBreakHit()->section, r.section);
        const bool next = !here && !pane_.nextSection_.empty() && sd::sameSection(pane_.nextSection_, r.section);
        if (here) { s.bold = true; s.fgTone = ui::Tone::Warning; }
        else if (next) s.fgTone = ui::Tone::Accent;
        if (c == TSection) s.icon = ui::Icon::Section;
        if (c == TRank || ((c == TStatements || c == TTime) && pane_.plannedTrace_)) s.fgTone = ui::Tone::Muted;
        if (!r.active && !pane_.plannedTrace_ && !here) s.fgTone = ui::Tone::Muted;   // 1.10.2 : inactive
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(RowIndex row) const override {
        if (row >= rows_.size()) return {};
        const auto& r = rows_[row];
        std::string tip = std::to_string(r.rank) + ". " + r.entry + (sd::sameSection(r.entry, r.section) ? std::string{} : " \xE2\x80\xBA " + r.section);
        if (!pane_.plannedTrace_ && !r.active)
            tip += "\nInactive au dernier cycle : sa condition d'activation (" + r.condition
                 + ") \xC3\xA9tait fausse ; elle ne tourne pas tant qu'elle l'est (comme sur l'automate).";
        else if (!pane_.plannedTrace_)
            tip += "\n" + sd::plural(r.statements, "instruction", "instructions") + " au dernier cycle, " + sd::millis(r.micros);
        else
            tip += "\nL'ordre pr\xC3\xA9vu : le temps et les instructions viennent apr\xC3\xA8s un cycle.";
        return tip + "\nDouble-clic : son code ici.";
    }
private:
    SimDebugPane& pane_;
    std::vector<sd::TraceRow> rows_;
};

// ---- Lot API 8 (2e partie) : l'espion tape (sous la liste des espions) ----
//  Entree ajoute ce qui est tape - la suggestion en surbrillance d'abord, s'il y
//  en a une (un seul appui, comme la liste de la maquette).
class SimDebugPane::WatchField final : public ui::InputText {
public:
    WatchField(SimDebugPane& pane, std::string id) : ui::InputText(std::move(id)), pane_(pane) {}
    void take() { grabFocus(); }
protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && k->key == ui::Key::Return && !k->mods.ctrl && !k->mods.alt) {
            if (suggestionsOpen()) (void)acceptSuggestion(suggestionIndex());
            std::string why;
            if (!pane_.submitWatchField(&why) && !why.empty()) pane_.status(why);
            return ui::EventResult::Consumed;
        }
        return ui::InputText::onEvent(ev);
    }
private:
    SimDebugPane& pane_;
};

// ==================================================================== l'onglet ====
SimDebugPane::SimDebugPane(std::string id) : ui::Widget(std::move(id)) {
    livePanes().push_back(this);
    setFocusPolicy(false);

    const auto dotPainter = [](gfx::IRenderer& r, int icon, const gfx::Rect& box, gfx::Color) {
        static const ui::Theme fallback = ui::Theme::dark();
        paintDot(r, icon, box, g_theme ? *g_theme : fallback);
    };

    bpTable_ = &static_cast<ui::TableView&>(addChild(std::make_unique<BreakTable>(*this, this->id() + ".points")));
    bpModel_ = std::make_shared<BreakModel>(*this);
    bpTable_->setModel(bpModel_);
    bpTable_->setSelectionMode(ui::SelectionMode::Extended);
    bpTable_->setAlternatingRowColors(false);
    bpTable_->setIconPainter(dotPainter);
    links_ += bpTable_->selectionChanged->connect([this](const std::vector<RowIndex>& rows) {
        // Un point choisi : son code (sauf en pause sur un passage : le code du passage reste).
        if (rows.size() != 1 || rows.front() >= bps_.size()) return;
        auto* h = host();
        if (h && h->state() == SimulationHost::State::Paused && h->lastBreakHit()) return;
        const auto& b = bps_[rows.front()];
        (void)showSection(b.section, b.line);
    });
    links_ += bpTable_->activated->connect([this](RowIndex r) {
        if (r >= bps_.size()) return;
        (void)showSection(bps_[r].section, bps_[r].line);
    });

    watchTable_ = &static_cast<ui::TableView&>(addChild(std::make_unique<WatchTable>(*this, this->id() + ".espions")));
    watchModel_ = std::make_shared<WatchModel>(*this);
    watchTable_->setModel(watchModel_);
    watchTable_->setSelectionMode(ui::SelectionMode::Extended);
    watchTable_->setAlternatingRowColors(true);
    links_ += watchTable_->expanderClicked->connect([this](RowIndex r) { toggleWatchRow(static_cast<std::size_t>(r)); });
    links_ += watchTable_->selectionChanged->connect([this](const std::vector<RowIndex>& rows) {
        if (rows.size() != 1 || rows.front() >= watchRows_.size()) return;
        const auto& r = watchRows_[rows.front()];
        if (r.node.real) chooseVariable(r.node.path);
    });
    // Lot API 8 (2e partie) : « Ajouter un espion : tape un nom » (les noms du projet
    // qui commencent pareil ; Entree ajoute).
    watchField_ = &static_cast<ui::InputText&>(addChild(std::make_unique<WatchField>(*this, this->id() + ".espion-tape")));
    watchField_->setPlaceholder("Ajouter un espion : tape un nom");
    watchField_->setAssist([this](std::string_view before, std::size_t& from, std::vector<ui::InputText::Suggestion>& out) {
        from = 0;
        for (const auto& name : watchSuggestions(std::string(before))) {
            ui::InputText::Suggestion s;
            s.text = name;
            out.push_back(std::move(s));
        }
    });

    auto code = std::make_unique<ui::MultiLineText>(this->id() + ".code");
    code->setReadOnly(true);
    code->setShowLineNumbers(true);
    code->setLanguage(ui::Language::StructuredText);
    code->setBreakpointGutter(true);
    code_ = &static_cast<ui::MultiLineText&>(addChild(std::move(code)));
    links_ += code_->breakpointToggled->connect([this](std::size_t line) {
        if (!codeSection_.empty()) (void)toggleBreakpoint(codeSection_, static_cast<int>(line) + 1);
    });
    // Lot API 8 (2e partie) : le clic droit dans la marge : la condition de la ligne.
    links_ += code_->breakpointConditionRequested->connect([this](std::size_t line) {
        if (!codeSection_.empty()) (void)editCondition(codeSection_, static_cast<int>(line) + 1);
    });
    links_ += code_->breakpointEnableToggled->connect([this](std::size_t line) {
        for (std::size_t i = 0; i < bps_.size(); ++i)
            if (sd::sameSection(bps_[i].section, codeSection_) && bps_[i].line == static_cast<int>(line) + 1) {
                (void)setBreakpointEnabled(i + 1, !bps_[i].enabled);
                return;
            }
    });
    // « Qui a ecrit ? » suit le nom sous le curseur ; un double-clic en fait un espion.
    links_ += code_->caretSymbolChanged->connect([this](const std::string& symbol) {
        if (!symbol.empty()) chooseVariable(symbol);
    });
    links_ += code_->symbolActivated->connect([this](const std::string& symbol) {
        std::string resolved = symbol;
        if (auto* rt = runtime())
            for (const auto& prefix : prefixes()) {
                sim::Value v;
                if (!prefix.empty() && rt->get(prefix + symbol, v)) { resolved = prefix + symbol; break; }
            }
        std::string why;
        if (addWatch(resolved, &why)) status("Espion ajout\xC3\xA9 : " + resolved + " (double-clic sur un nom du code).");
        else if (!why.empty()) status(why);
    });
    // Le survol d'un nom : sa valeur en direct (l'infobulle se relit tant qu'elle est ouverte).
    code_->setValueProvider([this](std::string_view symbol, std::string& out) {
        const auto v = valueText(std::string(symbol));
        if (v.empty()) return false;
        out = std::string(symbol) + " = " + v;
        return true;
    });

    traceTable_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".trace")));
    traceModel_ = std::make_shared<TraceModel>(*this);
    traceTable_->setModel(traceModel_);
    traceTable_->setSelectionMode(ui::SelectionMode::Single);
    traceTable_->setAlternatingRowColors(true);
    links_ += traceTable_->activated->connect([this](RowIndex r) {
        const auto& rows = traceModel_->rows();
        if (r < rows.size()) (void)showSection(rows[r].section);
    });
}

SimDebugPane::~SimDebugPane() {
    auto& panes = livePanes();
    panes.erase(std::remove(panes.begin(), panes.end(), this), panes.end());
    // Le point d'arret provisoire ne survit pas a l'onglet.
    if (auto* h = host(); h && runToId_ != 0) (void)h->removeBreakpoint(runToId_);
}

bool SimDebugPane::claimsKey(ui::Key key) {
    if (key != ui::Key::F11 && key != ui::Key::F10 && key != ui::Key::F5) return false;
    for (const auto* p : livePanes())
        if (p->shown() && p->host() && p->project()) return true;
    return false;
}

bool SimDebugPane::isTransient(std::uint32_t id) {
    if (id == 0) return false;
    for (const auto* p : livePanes())
        if (p->runToId_ == id) return true;
    return false;
}

void SimDebugPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    dirty_ = true;
}

void SimDebugPane::attach(ApiFrame& frame) {
    frame_ = &frame;
    auto& t = frame.tools();
    // L'ordre et les mots de la maquette (Simulation > Debogage) : Continuer et
    // Pause cote a cote, puis Section suivante, Cycle suivant, Arreter ; les points
    // d'arret ; Modifier le projet ; l'aide.
    t.add(AContinue, HmiGlyph::Play, "Continuer : le programme repart jusqu'au prochain point d'arr\xC3\xAAt (F5)", "Continuer");
    t.add(APause, HmiGlyph::DistributeH, "Pause : le programme s'arr\xC3\xAAte entre deux sections, l\xC3\xA0 o\xC3\xB9 il en est", "Pause");
    t.add(AStepSection, HmiGlyph::Down, "Section suivante : la section en cours s'ex\xC3\xA9" "cute, puis la pause avant la suivante (F10)", "Section suivante");
    t.add(AStepCycle, HmiGlyph::Refresh, "Cycle suivant : le programme va jusqu'\xC3\xA0 la fin du cycle, puis la pause (F11)", "Cycle suivant");
    t.add(AStop, HmiGlyph::Stop, "Arr\xC3\xAAter : tout repart des valeurs initiales ; les points d'arr\xC3\xAAt restent (Maj+F5)", "Arr\xC3\xAAter");
    t.add(ARunToLine, HmiGlyph::Forward, "Ex\xC3\xA9" "cuter jusqu'\xC3\xA0 la ligne du curseur, dans le code montr\xC3\xA9 ici (Ctrl+F10)",
          "Ex\xC3\xA9" "cuter jusqu'\xC3\xA0 la ligne");
    t.separator();
    t.add(AAddBreakpoint, HmiGlyph::Plus, "Poser un point d'arr\xC3\xAAt : une section, une ligne, une condition (ou un clic dans la marge du code)",
          std::string("Point d'arr\xC3\xAAt") + kEllipsis);
    t.add(ADisableAll, HmiGlyph::EyeOff, "D\xC3\xA9sactiver tous les points d'arr\xC3\xAAt : ils restent dans la liste", "Tout d\xC3\xA9sactiver");
    t.add(AAddWatch, HmiGlyph::Eye, "Un espion : un chemin dont la valeur se suit ici (ou double-clic sur un nom du code)",
          std::string("Espion") + kEllipsis);
    t.separator();
    t.add(AModify, HmiGlyph::Code, "En pause : modifie le code ou les types comme d'habitude ; Continuer reprend au m\xC3\xAAme cycle, pas au cycle 0",
          std::string("Modifier le projet") + kEllipsis);
    t.separator();
    t.add(AHelp, HmiGlyph::Help, "L'aide (F1)", "Aide (F1)");

    using S = SimulationHost::State;
    const auto state = [this] {
        auto* h = host();
        return h && h->attached() ? static_cast<int>(h->state()) : -1;
    };
    t.setEnabledWhen(AContinue, [this, state] {
        const int s = state();
        return project() != nullptr && s != static_cast<int>(S::Running) && s != static_cast<int>(S::Halted);
    });
    t.setEnabledWhen(AStepSection, [this, state] {
        const int s = state();
        return project() != nullptr && s != static_cast<int>(S::Running) && s != static_cast<int>(S::Halted);
    });
    t.setEnabledWhen(AStepCycle, [this, state] {
        const int s = state();
        return project() != nullptr && s != static_cast<int>(S::Running) && s != static_cast<int>(S::Halted);
    });
    t.setEnabledWhen(APause, [state] { return state() == static_cast<int>(S::Running); });
    t.setEnabledWhen(AStop, [state] { return state() >= 0 && state() != static_cast<int>(S::Stopped); });
    t.setEnabledWhen(ARunToLine, [this, state] {
        const int s = state();
        return project() != nullptr && !codeSection_.empty() && s != static_cast<int>(S::Running) && s != static_cast<int>(S::Halted);
    });
    t.setEnabledWhen(AAddBreakpoint, [this] { return project() != nullptr; });
    t.setEnabledWhen(AAddWatch, [this] { return project() != nullptr; });
    t.setEnabledWhen(ADisableAll, [this] { return std::any_of(bps_.begin(), bps_.end(), [](const auto& b) { return b.enabled; }); });
    t.setEnabledWhen(AModify, [state] { return state() == static_cast<int>(S::Paused); });
    // La maquette : Pause prend la place de Continuer pendant la marche.
    t.setVisibleWhen(AContinue, [state] { return state() != static_cast<int>(S::Running); });
    t.setVisibleWhen(APause, [state] { return state() == static_cast<int>(S::Running); });
    links_ += t.triggered->connect([this](int a) { runAction(a); });

    // La ligne d'aide de la maquette (sans le clic droit, que la marge ne fait pas).
    frame.setHint("Un clic dans la marge du code pose ou retire un point d'arr\xC3\xAAt (F9 sur la ligne)" + std::string(kDot)
                  + "survoler un nom montre sa valeur" + kDot + "F10 ex\xC3\xA9" "cute la section en cours et s'arr\xC3\xAAte avant la suivante"
                  + kDot + "Ctrl+F10 jusqu'\xC3\xA0 la ligne du curseur" + kDot + "double-clic sur un nom : un espion");
}

// ------------------------------------------------------------------ les acces ----
SimulationHost* SimDebugPane::host() const { return hosts_.host ? hosts_.host() : nullptr; }

sim::Runtime* SimDebugPane::runtime() const {
    auto* h = host();
    return h ? h->runtime() : nullptr;
}

std::shared_ptr<const domain::Project> SimDebugPane::project() const { return hosts_.project ? hosts_.project() : nullptr; }

bool SimDebugPane::shown() const {
    for (const ui::Widget* w = this; w; w = w->parent())
        if (!w->visible()) return false;
    return true;
}

void SimDebugPane::status(const std::string& message) const {
    if (hosts_.status) hosts_.status(message);
}

void SimDebugPane::journal(const std::string& kind, const std::string& message, const std::string& section, int line) const {
    if (hosts_.journal) hosts_.journal(kind, message, section, line);
}

bool SimDebugPane::isHit(std::uint32_t id) const {
    auto* h = host();
    return id != 0 && h && h->state() == SimulationHost::State::Paused && h->lastBreakHit() && h->lastBreakHit()->id == id;
}

bool SimDebugPane::ensureAttached() {
    auto* h = host();
    if (!h) return false;
    if (h->attached() && !h->stale()) return true;
    std::string why;
    if (hosts_.attach && hosts_.attach(&why)) return true;
    status("La simulation ne d\xC3\xA9marre pas : " + SimulationPane::frenchMessage(why.empty() ? std::string("aucun projet ouvert") : why));
    return false;
}

sd::StateFacts SimDebugPane::facts() const {
    sd::StateFacts f;
    auto* h = host();
    if (!h) return f;
    f.attached = h->attached();
    f.state = h->state();
    f.cycle = h->scanCount();
    if (f.state == SimulationHost::State::Halted) {
        for (const auto& d : h->lastDiagnostics())
            if (d.severity == sim::Diagnostic::Severity::Error) {
                f.haltMessage = SimulationPane::frenchMessage(d.message);
                if (!d.section.empty()) f.haltMessage += " (" + SimulationPane::frenchPlace(d.section, d.line) + ")";
                break;
            }
        if (f.haltMessage.empty()) f.haltMessage = SimulationPane::frenchMessage(h->haltMessage());
    }
    if (f.state == SimulationHost::State::Paused && h->lastBreakHit()) {
        f.hit = &*h->lastBreakHit();
        for (std::size_t i = 0; i < bps_.size(); ++i)
            if (bps_[i].id == f.hit->id) {
                f.breakpoint = &bps_[i];
                f.number = i + 1;
                break;
            }
    }
    for (const auto& b : bps_) {
        ++f.total;
        if (b.enabled) ++f.active;
    }
    f.nextSection = nextSection_;
    f.gesture = gesture_;
    if (f.state == SimulationHost::State::Running && runToId_ != 0) f.runToLine = runToText_;
    return f;
}

// ------------------------------------------------------------------ le rythme ----
void SimDebugPane::refresh() {
    dirty_ = true;
    writerForPath_.clear();          // le code a pu changer : les lignes qui ecrivent se relisent
    // La section montree a pu changer de texte (une commande) : on la recharge.
    codeIndex_ = domain::kNoIndex;
}

void SimDebugPane::tick(double now) {
    now_ = now;
    auto* h = host();
    // La maquette : "Simuler" a l'arret (le programme part du cycle 0), "Continuer" sinon.
    if (frame_) {
        const int resume = h && h->attached() && h->state() != SimulationHost::State::Stopped ? 1 : 0;
        if (resume != continueLabel_) {
            continueLabel_ = resume;
            frame_->tools().setText(AContinue,
                                    resume ? "Continuer : le programme repart jusqu'au prochain point d'arr\xC3\xAAt (F5)"
                                           : "Simuler : le programme de MAST part du cycle 0 et tourne jusqu'au premier point d'arr\xC3\xAAt (F5)",
                                    resume ? "Continuer" : "Simuler");
        }
    }
    if (h != hostSeen_) {
        hostLinks_.clear();
        hostSeen_ = h;
        if (h) {
            // Des drapeaux seulement : ces signaux partent du fond de la boucle du simulateur.
            hostLinks_ += h->changed->connect([this] { bpDirty_ = true; });
            hostLinks_ += h->breakHit->connect([this] { hitDirty_ = true; });
            hostLinks_ += h->onlineChanged->connect([this] { dirty_ = true; codeIndex_ = domain::kNoIndex; });
        }
        dirty_ = true;
    }
    if (!shown()) return;
    g_theme = nullptr;

    using S = SimulationHost::State;
    const int state = h && h->attached() ? static_cast<int>(h->state()) : -1;
    const std::uint64_t generation = h ? h->generation() : 0;
    const std::uint64_t scan = h ? h->scanCount() : 0;
    // Le point d'arret provisoire (jusqu'a la ligne) : retire des que ca ne tourne plus.
    if (runToId_ != 0 && state != static_cast<int>(S::Running)) {
        if (h) (void)h->removeBreakpoint(runToId_);
        runToId_ = 0;
        runToText_.clear();
        bpDirty_ = true;
    }
    if (generation != generationSeen_) {
        // Un autre programme (prepare a nouveau) : les espions repartent de zero.
        generationSeen_ = generation;
        for (auto& w : watches_) {
            w.changed = false;
            w.text.clear();
            w.known = false;
        }
        hitKeySeen_.clear();
        dirty_ = true;
    }
    if (dirty_) {
        dirty_ = false;
        bpDirty_ = true;
        stateSeen_ = -2;                    // tout se relit plus bas
    }
    if (bpDirty_) {
        bpDirty_ = false;
        refreshBreakpoints();
    }
    // Un nouveau passage (le signal, ou le passage lui-meme : le moteur peut ne pas le dire).
    std::string hitKey;
    if (h && state == static_cast<int>(S::Paused) && h->lastBreakHit()) {
        const auto& hit = *h->lastBreakHit();
        hitKey = std::to_string(hit.id) + "|" + lower(hit.section) + "|" + std::to_string(hit.line) + "|" + std::to_string(hit.scan);
    }
    if (hitDirty_ || hitKey != hitKeySeen_) {
        hitDirty_ = false;
        if (hitKey != hitKeySeen_) {
            hitKeySeen_ = hitKey;
            if (!hitKey.empty()) onBreakHit();
        }
    }
    if (state != stateSeen_) {
        const int previous = stateSeen_;
        stateSeen_ = state;
        nextSection_ = h && state == static_cast<int>(S::Paused) ? h->nextSection() : std::string{};
        if (previous == static_cast<int>(S::Paused) && state == static_cast<int>(S::Running)) gesture_ = gesture_ == sd::LastGesture::RunToLine ? gesture_ : sd::LastGesture::Continue;
        if (state != static_cast<int>(S::Paused) || !h || !h->lastBreakHit()) {
            // Plus de passage : la pile est celle du code montre.
            if (hitKey.empty()) { codeExecLine_ = kNpos; codeCallLine_ = kNpos; }
        }
        if (codeSection_.empty() || codeIndex_ == domain::kNoIndex) {
            // Rien de montre encore : la section du passage, la prochaine, ou la premiere de MAST.
            std::string first = nextSection_;
            if (first.empty())
                if (const auto p = project()) {
                    const auto order = domain::executionOrder(*p, std::string_view("MAST"));
                    if (!order.empty()) first = sd::sectionKey(*p, order.front().section);
                }
            if (!codeSection_.empty() && codeIndex_ == domain::kNoIndex) first = codeSection_;
            if (!first.empty()) (void)showSection(first);
        }
        refreshStack();
        scanSeen_ = ~std::uint64_t{0};      // les valeurs se relisent tout de suite
        nextLive_ = 0.0;
        if (frame_) frame_->invalidate();
        invalidate();
    }
    if (scan != scanSeen_ && (now >= nextLive_ || state != static_cast<int>(S::Running))) {
        scanSeen_ = scan;
        nextLive_ = now + kLiveEvery;
        if (h && state == static_cast<int>(S::Paused)) nextSection_ = h->nextSection();
        readWatchValues();
        refreshCodeMarks();
        refreshTimes();
        refreshWriter();
        invalidate();
    } else if (code_->firstVisibleLine() != notesFrom_) {
        refreshCodeMarks();                  // le code a defile : les valeurs des lignes montrees
    }
}

void SimDebugPane::refreshBreakpoints() {
    auto* h = host();
    std::vector<SimBreakpoint> list;
    if (h)
        for (auto& b : h->breakpoints())
            if (b.id != runToId_) list.push_back(std::move(b));
    // Ce qui compte pour l'ecran : l'ordre, l'etat, la condition, les passages, la note.
    std::string signature;
    for (const auto& b : list)
        signature += std::to_string(b.id) + (b.enabled ? "+" : "-") + b.section + ":" + std::to_string(b.line) + "?" + b.condition + "#"
                   + std::to_string(b.hits) + "!" + b.note + "\n";
    if (signature == bpSignature_) return;
    bpSignature_ = std::move(signature);
    bps_ = std::move(list);
    bpModel_->reset();
    refreshCodeMarks();
    if (frame_) frame_->invalidate();
    invalidate();
}

void SimDebugPane::onBreakHit() {
    auto* h = host();
    if (!h || !h->lastBreakHit()) return;
    const auto hit = *h->lastBreakHit();
    refreshBreakpoints();
    nextSection_ = h->nextSection();
    // (le passage lui-meme va au journal par l'ecran : SimDebugWorkspace.cpp, onglet ouvert ou non)
    levels_ = sd::stackLevels(hit.stack, hit.section);
    // Le niveau du passage : le plus profond qui montre une section.
    level_ = 0;
    for (std::size_t i = 0; i < levels_.size(); ++i)
        if (!levels_[i].section.empty() && levels_[i].kind != sd::StackLevel::Kind::Instance) level_ = i;
    codeInstance_ = level_ < levels_.size() ? levels_[level_].instance : std::string{};
    codeCallLine_ = kNpos;
    const auto instance = codeInstance_;
    if (showSection(hit.section, hit.line)) {
        codeExecLine_ = hit.line > 0 ? static_cast<std::size_t>(hit.line - 1) : kNpos;
        codeInstance_ = instance;
    }
    // « Qui a ecrit ? » : la premiere variable de la ligne, si rien n'est choisi.
    if (writerPath_.empty() && !hit.values.empty()) writerPath_ = hit.values.front().first;
    scanSeen_ = ~std::uint64_t{0};
    refreshCodeMarks();
    refreshWriter();
    if (frame_) frame_->invalidate();
    invalidate();
}

void SimDebugPane::refreshStack() {
    auto* h = host();
    if (h && h->state() == SimulationHost::State::Paused && h->lastBreakHit()) {
        const auto& hit = *h->lastBreakHit();
        auto levels = sd::stackLevels(hit.stack, hit.section);
        const bool same = levels.size() == levels_.size()
                       && std::equal(levels.begin(), levels.end(), levels_.begin(),
                                     [](const sd::StackLevel& a, const sd::StackLevel& b) { return a.label == b.label; });
        if (!same) {
            levels_ = std::move(levels);
            level_ = levels_.empty() ? 0 : levels_.size() - 1;
        }
        return;
    }
    // Sans passage : ou vit le code montre (la tache, l'unite, la section).
    levels_.clear();
    level_ = 0;
    const auto p = project();
    if (!p || codeIndex_ >= p->sections.size()) return;
    const auto& s = p->sections[codeIndex_];
    sd::StackLevel task;
    task.kind = sd::StackLevel::Kind::Task;
    task.label = task.name = s.task ? text(*p, s.task) : std::string("MAST");
    if (task.label.empty()) task.label = task.name = "MAST";
    const bool block = s.owner < p->pous.size() && p->pous[s.owner].kind == domain::PouKind::FunctionBlockType;
    if (!block) levels_.push_back(task);
    if (s.owner < p->pous.size() && p->pous[s.owner].kind == domain::PouKind::ProgramUnit) {
        sd::StackLevel unit;
        unit.kind = sd::StackLevel::Kind::Unit;
        unit.label = unit.name = text(*p, p->pous[s.owner].name);
        levels_.push_back(unit);
    }
    sd::StackLevel section;
    section.kind = block ? sd::StackLevel::Kind::BlockSection : sd::StackLevel::Kind::Section;
    section.label = section.name = text(*p, s.name);
    section.section = codeSection_;
    if (block) section.type = text(*p, p->pous[s.owner].name);
    levels_.push_back(section);
    level_ = levels_.size() - 1;
}

void SimDebugPane::refreshTimes() {
    auto* h = host();
    const auto p = project();
    std::vector<SimSectionTime> times = h ? h->sectionTimes() : std::vector<SimSectionTime>{};
    const std::int64_t period = h ? h->scanIntervalMs() : 20;
    std::vector<sd::TraceRow> rows;
    plannedTrace_ = times.empty();
    if (plannedTrace_ && p) {
        // Pas encore de cycle mesure : l'ordre prevu de MAST (les entrees, leurs sections).
        std::size_t rank = 0;
        for (const auto& e : domain::executionEntries(*p, std::string_view("MAST"))) {
            const std::string entry = e.unit && e.pou < p->pous.size() ? text(*p, p->pous[e.pou].name)
                                    : e.section < p->sections.size() ? text(*p, p->sections[e.section].name) : std::string{};
            bool first = true;
            for (const auto si : e.sections) {
                sd::TraceRow r;
                r.rank = ++rank;
                r.entry = entry;
                r.section = sd::sectionKey(*p, si);
                r.firstOfEntry = first;
                first = false;
                rows.push_back(std::move(r));
            }
        }
    } else {
        rows = sd::traceRows(times);
    }
    times_ = std::move(times);
    bars_ = sd::timeBars(times_, period);
    if (bars_.size() > kMaxBars) bars_.resize(kMaxBars);
    timeSummary_ = sd::cycleSummary(times_, period);
    // La table ne se refait que si ses lignes changent (son defilement reste).
    const auto& old = traceModel_->rows();
    const bool same = old.size() == rows.size()
                   && std::equal(old.begin(), old.end(), rows.begin(), [](const sd::TraceRow& a, const sd::TraceRow& b) {
                          return a.section == b.section && a.statements == b.statements && a.micros == b.micros && a.entry == b.entry
                              && a.active == b.active;
                      });
    if (!same) traceModel_->setRows(std::move(rows));
    else traceTable_->invalidate();
}

// ------------------------------------------------------------- les espions ----
std::vector<std::string> SimDebugPane::watches() const {
    std::vector<std::string> out;
    for (const auto& w : watches_) out.push_back(w.path);
    return out;
}

std::vector<std::string> SimDebugPane::watchLines() const {
    std::vector<std::string> out;
    for (const auto& r : watchRows_) {
        const auto& w = watches_[static_cast<std::size_t>(r.watch)];
        std::string line(static_cast<std::size_t>(r.depth) * 2, ' ');
        line += (r.depth == 0 ? w.path : r.node.label) + " = " + (r.value.empty() ? std::string(kDash) : r.value);
        if (r.depth == 0) line += " (" + r.since + ")";
        out.push_back(std::move(line));
    }
    return out;
}

bool SimDebugPane::locateWatch(const domain::Project& p, const std::string& path, mt::Node& out) const {
    const auto want = shape(path) == lower(path) ? lower(path) : lower(path);
    std::string key;
    for (const char c : path)
        if (!std::isspace(uc(c))) key += static_cast<char>(std::tolower(uc(c)));
    if (key.empty()) return false;
    // Les racines : les globales (leur nom), les locales des unites (Unite.nom).
    std::string rootName, rootType;
    std::size_t best = 0;
    const auto consider = [&](const std::string& name, const domain::Variable& v) {
        const auto n = lower(name);
        if (n.size() <= best) return;
        if (key == n || (key.size() > n.size() && startsWith(key, n) && (key[n.size()] == '.' || key[n.size()] == '['))) {
            rootName = name;
            rootType = text(p, v.type.name);
            best = n.size();
        }
    };
    for (const auto& v : p.variables)
        if (v.scope == domain::VariableScope::Global || v.scope == domain::VariableScope::Constant) consider(text(p, v.name), v);
    for (const auto& pou : p.pous) {
        if (pou.kind != domain::PouKind::ProgramUnit) continue;
        const auto unit = text(p, pou.name);
        for (const auto vi : pou.parameters) if (vi < p.variables.size()) consider(unit + "." + text(p, p.variables[vi].name), p.variables[vi]);
        for (const auto vi : pou.locals) if (vi < p.variables.size()) consider(unit + "." + text(p, p.variables[vi].name), p.variables[vi]);
    }
    if (rootName.empty()) return false;
    auto node = mt::root(rootName, rootType);
    for (int guard = 0; guard < 64; ++guard) {
        if (node.real && lower(node.path) == key) {
            out = std::move(node);
            return true;
        }
        if (!mt::hasChildren(p, node)) return false;
        auto kids = mt::children(p, node);
        bool found = false;
        for (auto& k : kids) {
            const auto kp = lower(k.path);
            if (k.real) {
                if (kp == key || (key.size() > kp.size() && startsWith(key, kp) && (key[kp.size()] == '.' || key[kp.size()] == '['))) {
                    node = std::move(k);
                    found = true;
                    break;
                }
                continue;
            }
            // Un paquet, une ligne d'un tableau : les indices du chemin cherche.
            if (key.size() <= kp.size() || !startsWith(key, kp) || key[kp.size()] != '[') continue;
            const auto close = key.find(']', kp.size());
            if (close == std::string::npos) continue;
            std::vector<std::int64_t> idx;
            const auto inside = key.substr(kp.size() + 1, close - kp.size() - 1);
            std::size_t from = 0;
            while (from <= inside.size()) {
                const auto comma = inside.find(',', from);
                idx.push_back(std::strtoll(inside.substr(from, (comma == std::string::npos ? inside.size() : comma) - from).c_str(), nullptr, 10));
                if (comma == std::string::npos) break;
                from = comma + 1;
            }
            if (idx.size() <= k.dim) continue;
            bool same = true;
            for (std::size_t f = 0; f < k.fixed.size(); ++f)
                if (f >= idx.size() || idx[f] != k.fixed[f]) same = false;
            if (!same || idx[k.dim] < k.first || idx[k.dim] > k.last) continue;
            node = std::move(k);
            found = true;
            break;
        }
        if (!found) return false;
    }
    (void)want;
    return false;
}

bool SimDebugPane::addWatch(const std::string& typed, std::string* why) {
    const std::string path = trim(typed);
    if (path.empty()) {
        if (why) *why = "Un espion : un chemin (Armoires[0].ana.PT1.mes).";
        return false;
    }
    for (const auto& w : watches_)
        if (lower(w.path) == lower(path)) {
            if (why) *why = "D\xC3\xA9j\xC3\xA0 un espion : " + path;
            return false;
        }
    Watch w;
    w.path = path;
    const auto p = project();
    mt::Node node;
    sim::Value v;
    auto* rt = runtime();
    if (p && locateWatch(*p, path, node)) {
        w.path = node.path;                  // l'orthographe du projet
        w.type = node.type;
    } else if (rt && rt->get(path, v)) {
        w.type = std::string(sim::toString(v.type()));
    } else {
        if (why) *why = "Introuvable dans le projet : " + path + " (une globale, un membre, ou Unite.variable pour une locale).";
        return false;
    }
    watches_.push_back(std::move(w));
    readWatchValues();
    journal("espion", "Espion ajout\xC3\xA9 : " + watches_.back().path);
    invalidate();
    return true;
}

bool SimDebugPane::removeWatch(const std::string& path) {
    const auto want = lower(trim(path));
    for (std::size_t i = 0; i < watches_.size(); ++i)
        if (lower(watches_[i].path) == want) {
            journal("espion", "Espion retir\xC3\xA9 : " + watches_[i].path);
            watches_.erase(watches_.begin() + static_cast<std::ptrdiff_t>(i));
            rebuildWatchRows();
            invalidate();
            return true;
        }
    return false;
}

bool SimDebugPane::expandWatch(const std::string& path, bool open) {
    const auto want = lower(trim(path));
    for (const auto& r : watchRows_)
        if (lower(r.node.path) == want || lower(r.node.key) == want || lower(r.node.label) == want) {
            if (!r.expandable) return false;
            if (open) expanded_.insert(lower(r.node.key));
            else expanded_.erase(lower(r.node.key));
            rebuildWatchRows();
            return true;
        }
    return false;
}

void SimDebugPane::toggleWatchRow(std::size_t row) {
    if (row >= watchRows_.size() || !watchRows_[row].expandable) return;
    const auto key = lower(watchRows_[row].node.key);
    if (expanded_.count(key)) expanded_.erase(key);
    else expanded_.insert(key);
    rebuildWatchRows();
}

void SimDebugPane::rebuildWatchRows() {
    const auto p = project();
    watchRows_.clear();
    auto* rt = runtime();
    const auto readNode = [&](const mt::Node& n) -> std::string {
        if (!n.real) return {};
        if (!rt) return kDash;
        sim::Value v;
        if (rt->get(n.path, v)) return sd::formatValue(v);
        return p && mt::hasChildren(*p, n) ? std::string("{") + kEllipsis + "}" : std::string("?");
    };
    const auto scan = host() ? host()->scanCount() : 0;
    const auto period = host() ? host()->scanIntervalMs() : 20;
    // Les enfants d'une ligne depliee, a toute profondeur.
    const std::function<void(int, const mt::Node&, int)> addChildren = [&](int watch, const mt::Node& parent, int depth) {
        if (!p) return;
        for (auto& child : mt::children(*p, parent)) {
            if (watchRows_.size() >= kMaxWatchRows) return;
            WatchRow r;
            r.watch = watch;
            r.depth = depth;
            r.expandable = mt::hasChildren(*p, child);
            r.open = r.expandable && expanded_.count(lower(child.key)) > 0;
            r.value = readNode(child);
            r.node = std::move(child);
            const bool open = r.open;
            const mt::Node copy = r.node;
            watchRows_.push_back(std::move(r));
            if (open) addChildren(watch, copy, depth + 1);
        }
    };
    for (std::size_t i = 0; i < watches_.size(); ++i) {
        const auto& w = watches_[i];
        mt::Node node;
        if (!p || !locateWatch(*p, w.path, node)) node = mt::root(w.path, w.type);
        WatchRow r;
        r.watch = static_cast<int>(i);
        r.expandable = p && mt::hasChildren(*p, node);
        r.open = r.expandable && expanded_.count(lower(node.key)) > 0;
        r.value = w.known ? w.text : (rt ? std::string("?") : std::string(kDash));
        r.since = sd::sinceText(w.changed, w.changedScan, scan, period);
        r.node = std::move(node);
        const bool open = r.open;
        const mt::Node copy = r.node;
        watchRows_.push_back(std::move(r));
        if (open) addChildren(static_cast<int>(i), copy, 1);
    }
    watchModel_->reset();
}

void SimDebugPane::readWatchValues() {
    auto* rt = runtime();
    const auto p = project();
    const auto scan = host() ? host()->scanCount() : 0;
    for (auto& w : watches_) {
        std::string value;
        bool known = false;
        if (rt) {
            sim::Value v;
            if (rt->get(w.path, v)) {
                value = sd::formatValue(v);
                known = true;
            } else if (p) {
                mt::Node n;
                if (locateWatch(*p, w.path, n) && mt::hasChildren(*p, n)) {
                    value = std::string("{") + kEllipsis + "}";     // une structure : ses membres ont les valeurs
                    known = true;
                }
            }
        }
        if (known && w.known && value != w.text) {
            w.changed = true;
            w.changedScan = scan;
        }
        w.text = value;
        w.known = known;
    }
    // Les lignes gardent leur place (et la selection) : seules les valeurs changent.
    const bool sameShape = !watchRows_.empty() || watches_.empty();
    if (!sameShape) {
        rebuildWatchRows();
        return;
    }
    std::size_t count = 0;
    for (const auto& r : watchRows_) if (r.depth == 0) ++count;
    if (count != watches_.size()) {
        rebuildWatchRows();
        return;
    }
    const auto period = host() ? host()->scanIntervalMs() : 20;
    for (auto& r : watchRows_) {
        const auto& w = watches_[static_cast<std::size_t>(r.watch)];
        if (r.depth == 0) {
            r.value = w.known ? w.text : (rt ? std::string("?") : std::string(kDash));
            r.since = sd::sinceText(w.changed, w.changedScan, scan, period);
        } else if (r.node.real) {
            sim::Value v;
            r.value = rt && rt->get(r.node.path, v) ? sd::formatValue(v) : (r.expandable ? std::string("{") + kEllipsis + "}" : std::string(kDash));
        }
    }
    watchTable_->invalidate();
}

// ------------------------------------------------------------------- le code ----
std::vector<std::string> SimDebugPane::prefixes() const {
    std::vector<std::string> out;
    const auto p = project();
    if (p && codeIndex_ < p->sections.size()) {
        const auto& s = p->sections[codeIndex_];
        if (s.owner < p->pous.size()) {
            const auto& pou = p->pous[s.owner];
            if (pou.kind == domain::PouKind::ProgramUnit) out.push_back(text(*p, pou.name) + ".");
            else if (pou.kind == domain::PouKind::FunctionBlockType && !codeInstance_.empty()) out.push_back(codeInstance_ + ".");
        }
    }
    out.emplace_back();
    return out;
}

bool SimDebugPane::readValue(const std::string& symbol, sim::Value& out) const {
    auto* rt = runtime();
    if (!rt || symbol.empty()) return false;
    for (const auto& prefix : prefixes())
        if (rt->get(prefix + symbol, out)) return true;
    return false;
}

std::string SimDebugPane::valueText(const std::string& symbol) const {
    sim::Value v;
    if (!readValue(symbol, v)) return {};
    std::string s = sd::formatValue(v);
    if (auto* rt = runtime())
        for (const auto& prefix : prefixes())
            if (rt->known(prefix + symbol)) {
                if (rt->isForced(prefix + symbol)) s += " (forc\xC3\xA9" "e)";
                break;
            }
    return s;
}

void SimDebugPane::loadSection(domain::Index index, const std::string& key) {
    const auto p = project();
    if (!p || index >= p->sections.size()) return;
    if (index == codeIndex_ && key == codeSection_) return;
    const auto& s = p->sections[index];
    code_->setLanguage(s.language == domain::PouLanguage::ST ? ui::Language::StructuredText
                       : s.language == domain::PouLanguage::IL ? ui::Language::InstructionList
                                                               : ui::Language::PlainText);
    code_->setText(s.body);
    codeIndex_ = index;
    codeSection_ = key;
    codeExecLine_ = kNpos;
    codeCallLine_ = kNpos;
    notesFrom_ = kNpos;
    // L'etat des commentaires (* *) au debut de chaque ligne : une fois ici.
    commentAt_.clear();
    bool inComment = false;
    std::size_t from = 0;
    const std::string& body = s.body;
    while (from <= body.size()) {
        commentAt_.push_back(inComment);
        const auto nl = body.find('\n', from);
        (void)sd::lineSymbols(std::string_view(body).substr(from, (nl == std::string::npos ? body.size() : nl) - from), inComment);
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
}

bool SimDebugPane::showSection(const std::string& section, int line) {
    const auto p = project();
    if (!p) return false;
    const auto index = sd::findSection(*p, section);
    if (index == domain::kNoIndex) return false;
    const auto key = sd::sectionKey(*p, index);
    const bool other = key != codeSection_ || index != codeIndex_;
    loadSection(index, key);
    auto* h = host();
    const bool paused = h && h->state() == SimulationHost::State::Paused && h->lastBreakHit();
    if (other) {
        codeExecLine_ = kNpos;
        codeCallLine_ = kNpos;
        if (paused && sd::sameSection(h->lastBreakHit()->section, key) && h->lastBreakHit()->line > 0)
            codeExecLine_ = static_cast<std::size_t>(h->lastBreakHit()->line - 1);
        if (!paused) refreshStack();
    }
    if (line > 0) code_->goToLine(static_cast<std::size_t>(line - 1));
    notesFrom_ = kNpos;
    refreshCodeMarks();
    invalidate();
    return true;
}

void SimDebugPane::refreshCodeMarks() {
    std::vector<ui::MultiLineText::BreakMark> marks;
    for (const auto& b : bps_)
        if (b.line > 0 && sd::sameSection(b.section, codeSection_))
            marks.push_back({static_cast<std::size_t>(b.line - 1), b.enabled, !b.condition.empty()});
    code_->setBreakpoints(std::move(marks));
    code_->setExecutionLine(codeExecLine_ != kNpos ? codeExecLine_ : codeCallLine_);

    // Les valeurs a droite des lignes montrees (et de celle du passage).
    std::vector<ui::MultiLineText::LineNote> notes;
    notesFrom_ = code_->firstVisibleLine();
    auto* h = host();
    auto* rt = runtime();
    const auto p = project();
    if (rt && p && codeIndex_ < p->sections.size()) {
        const auto& body = p->sections[codeIndex_].body;
        const std::size_t rows = static_cast<std::size_t>(std::max(1.f, code_->bounds().h / 16.f)) + 2;
        const SimBreakHit* hit = h && h->state() == SimulationHost::State::Paused && h->lastBreakHit()
                                     && sd::sameSection(h->lastBreakHit()->section, codeSection_) ? &*h->lastBreakHit() : nullptr;
        // Les debuts de ligne, jusqu'a la derniere montree.
        std::size_t line = 0, from = 0;
        while (line < notesFrom_ && from <= body.size()) {
            const auto nl = body.find('\n', from);
            if (nl == std::string::npos) { from = body.size() + 1; break; }
            from = nl + 1;
            ++line;
        }
        for (; line < notesFrom_ + rows && from <= body.size(); ++line) {
            const auto nl = body.find('\n', from);
            const auto lineText = std::string_view(body).substr(from, (nl == std::string::npos ? body.size() : nl) - from);
            std::string note;
            std::size_t shown = 0;
            if (hit && hit->line > 0 && static_cast<std::size_t>(hit->line - 1) == line && !hit->values.empty()) {
                for (const auto& [name, value] : hit->values) {
                    if (shown++ == kNotesPerLine) { note += kDot + std::string(kEllipsis); break; }
                    note += (note.empty() ? "" : kDot) + name + " = " + value;
                }
            } else {
                bool comment = line < commentAt_.size() ? commentAt_[line] : false;
                const auto names = sd::lineSymbols(lineText, comment);
                for (const auto& name : names) {
                    sim::Value v;
                    if (!readValue(name, v)) continue;
                    if (shown++ == kNotesPerLine) { note += kDot + std::string(kEllipsis); break; }
                    std::string value = sd::formatValue(v);
                    if (value.size() > 18) value = value.substr(0, 16) + kEllipsis;
                    note += (note.empty() ? "" : kDot) + name + " = " + value;
                }
            }
            if (!note.empty()) {
                const bool here = codeExecLine_ != kNpos && codeExecLine_ == line;
                notes.push_back({line, std::move(note), here ? ui::Tone::Warning : ui::Tone::None});
            }
            if (nl == std::string::npos) break;
            from = nl + 1;
        }
    }
    code_->setLineNotes(std::move(notes));
}

// -------------------------------------------------------------------- la pile ----
std::vector<std::string> SimDebugPane::stackLabels() const {
    std::vector<std::string> out;
    for (const auto& l : levels_) out.push_back(l.label);
    return out;
}

bool SimDebugPane::chooseLevel(std::size_t index) {
    if (index >= levels_.size()) return false;
    const auto level = levels_[index];
    using K = sd::StackLevel::Kind;
    if (level.kind == K::Task) {
        if (hosts_.request) hosts_.request("ordre");
        return true;
    }
    if (level.kind == K::Unit) {
        if (hosts_.request) hosts_.request("ordre:" + level.name);
        return true;
    }
    std::string section = level.section;
    const auto p = project();
    if (section.empty() && level.kind == K::Instance && p) {
        // Une instance sans section apres elle dans la pile : la premiere section de son bloc.
        for (const auto& pou : p->pous)
            if (pou.kind == domain::PouKind::FunctionBlockType && lower(text(*p, pou.name)) == lower(level.type) && !pou.sections.empty()) {
                section = sd::sectionKey(*p, pou.sections.front());
                break;
            }
    }
    if (section.empty()) return false;
    level_ = index;
    auto* h = host();
    const SimBreakHit* hit = h && h->state() == SimulationHost::State::Paused && h->lastBreakHit() ? &*h->lastBreakHit() : nullptr;
    // Le niveau du passage : sa ligne ; un niveau appelant : la ligne de l'appel.
    const bool deepest = hit && sd::sameSection(hit->section, section) && index + 1 >= levels_.size();
    if (!showSection(section, deepest ? hit->line : 0)) return false;
    codeInstance_ = level.instance;
    codeExecLine_ = deepest && hit->line > 0 ? static_cast<std::size_t>(hit->line - 1) : kNpos;
    codeCallLine_ = kNpos;
    if (!deepest && index + 1 < levels_.size() && p && codeIndex_ < p->sections.size()) {
        // L'appel du niveau suivant : « nom( » dans le texte de cette section.
        const auto& next = levels_[index + 1];
        const std::string callee = lower(next.kind == K::Instance ? next.name : std::string{});
        if (!callee.empty()) {
            const auto body = lower(p->sections[codeIndex_].body);
            std::size_t at = 0, line = 0, lineStart = 0;
            while ((at = body.find(callee, at)) != std::string::npos) {
                const bool left = at == 0 || !(std::isalnum(uc(body[at - 1])) || body[at - 1] == '_' || body[at - 1] == '.');
                std::size_t k = at + callee.size();
                while (k < body.size() && std::isspace(uc(body[k]))) ++k;
                if (left && k < body.size() && body[k] == '(') {
                    for (; lineStart < at; ++lineStart) if (body[lineStart] == '\n') ++line;
                    codeCallLine_ = line;
                    code_->goToLine(line);
                    break;
                }
                at += callee.size();
            }
        }
    }
    refreshCodeMarks();
    invalidate();
    return true;
}

// ---------------------------------------------------------- qui a ecrit ? ----
void SimDebugPane::chooseVariable(const std::string& path) {
    const auto p = trim(path);
    if (p == writerPath_) return;
    writerPath_ = p;
    refreshWriter();
    invalidate();
}

void SimDebugPane::refreshWriter() {
    writerValue_ = valueText(writerPath_);
    writerEngine_ = false;
    auto* h = host();
    if (h && !writerPath_.empty()) {
        // Le moteur connait le chemin tel que la simulation le nomme (une locale : Unite.nom).
        for (const auto& prefix : prefixes())
            if (h->lastWrite(prefix + writerPath_, writerLast_)) {
                writerEngine_ = true;
                break;
            }
    }
    if (writerForPath_ == writerPath_) return;
    writerForPath_ = writerPath_;
    writerSites_.clear();
    const auto p = project();
    if (!p || writerPath_.empty()) return;
    // La lecture du code : les lignes qui l'ecrivent (elle, un de ses membres, ou
    // la structure entiere qui la contient), les indices mis de cote.
    std::string rootName = writerPath_;
    if (const auto cut = rootName.find_first_of(".["); cut != std::string::npos) rootName = rootName.substr(0, cut);
    std::string rest = writerPath_;
    // Une locale d'unite (Unite.nom) : le nom est apres l'unite.
    for (const auto& pou : p->pous)
        if (pou.kind == domain::PouKind::ProgramUnit && lower(text(*p, pou.name)) == lower(rootName) && writerPath_.size() > rootName.size() + 1) {
            rest = writerPath_.substr(rootName.size() + 1);
            rootName = rest;
            if (const auto cut = rootName.find_first_of(".["); cut != std::string::npos) rootName = rootName.substr(0, cut);
            break;
        }
    const auto want = shape(rest);
    const auto refs = project::crossReference(*p, rootName);
    for (const auto& r : refs.writes) {
        const auto got = shape(r.path);
        const bool related = got == want || startsWith(got, want + ".") || startsWith(got, want + "[") || startsWith(want, got + ".")
                          || startsWith(want, got + "[");
        if (!related) continue;
        WriterSite site;
        site.section = r.section < p->sections.size() ? sd::sectionKey(*p, r.section) : r.sectionName;
        site.line = static_cast<std::uint32_t>(r.line);
        site.text = r.text;
        // 1.11.2 (D25) : l'unite de la section, pour la retrouver dans la trace du cycle.
        if (r.section < p->sections.size()) {
            const auto owner = p->sections[r.section].owner;
            if (owner < p->pous.size() && p->pous[owner].kind == domain::PouKind::ProgramUnit) site.unit = text(*p, p->pous[owner].name);
        }
        writerSites_.push_back(std::move(site));
        if (writerSites_.size() >= 6) break;
    }
}

std::vector<std::string> SimDebugPane::writerLines() const {
    std::vector<std::string> out;
    if (writerPath_.empty()) return out;
    out.push_back(writerPath_ + (writerValue_.empty() ? std::string{} : " = " + writerValue_));
    if (auto* rt = runtime(); rt && rt->isForced(writerPath_)) out.push_back("Personne : elle est forc\xC3\xA9" "e \xC3\xA0 " + writerValue_ + ".");
    auto* h = host();
    if (writerEngine_) out.push_back("\xC3\x89" "crite par " + sd::writeText(writerLast_, h ? h->scanCount() : 0));
    for (const auto& s : writerSites_) {
        // 1.11.2 (D25) : une ligne dont la section n'a pas tourne au dernier cycle le dit.
        const auto off = sd::inactiveSectionText(times_, s.unit, s.section);
        out.push_back(s.section + ", ligne " + std::to_string(s.line) + " : " + s.text + (off.empty() ? std::string{} : " \xE2\x80\x94 " + off));
    }
    if (!writerEngine_ && writerSites_.empty()) out.push_back("aucune ligne du code ne l'\xC3\xA9" "crit");
    return out;
}

// ------------------------------------------------------------- les points ----
bool SimDebugPane::toggleBreakpoint(const std::string& section, int line) {
    auto* h = host();
    const auto p = project();
    if (!h || line < 1) return false;
    std::string key = section;
    if (p)
        if (const auto index = sd::findSection(*p, section); index != domain::kNoIndex) key = sd::sectionKey(*p, index);
    for (const auto& b : h->breakpoints())
        if (b.id != runToId_ && sd::sameSection(b.section, key) && b.line == line) {
            (void)h->removeBreakpoint(b.id);
            const std::string said = "Point d'arr\xC3\xAAt enlev\xC3\xA9 : " + key + ", ligne " + std::to_string(line);
            journal("point-arret", said, key, line);
            status(said + ".");
            bpDirty_ = true;
            return false;
        }
    const auto id = h->addBreakpoint(key, line, {});
    const std::string said = "Point d'arr\xC3\xAAt pos\xC3\xA9 : " + key + ", ligne " + std::to_string(line);
    journal("point-arret", said, key, line);
    status(said + " \xE2\x80\x94 Continuer (F5) : le programme s'y arr\xC3\xAAtera.");
    bpDirty_ = true;
    return id != 0;
}

bool SimDebugPane::addBreakpointText(const std::string& typed, std::string* why) {
    sd::TypedBreakpoint t;
    std::string reason;
    if (!sd::parseBreakpoint(typed, t, &reason)) {
        if (why) *why = "Point d'arr\xC3\xAAt : " + reason + ".";
        return false;
    }
    auto* h = host();
    const auto p = project();
    if (!h || !p) {
        if (why) *why = "Point d'arr\xC3\xAAt : ouvre d'abord un projet.";
        return false;
    }
    const auto index = sd::findSection(*p, t.section);
    if (index == domain::kNoIndex) {
        if (why) *why = "Point d'arr\xC3\xAAt : section introuvable : " + t.section + ".";
        return false;
    }
    const auto& s = p->sections[index];
    std::size_t lines = 1;
    for (const char c : s.body) if (c == '\n') ++lines;
    if (static_cast<std::size_t>(t.line) > lines) {
        if (why) *why = "Point d'arr\xC3\xAAt : " + t.section + " n'a que " + std::to_string(lines) + " lignes.";
        return false;
    }
    const auto key = sd::sectionKey(*p, index);
    for (const auto& b : h->breakpoints())
        if (b.id != runToId_ && sd::sameSection(b.section, key) && b.line == t.line) {
            if (!t.condition.empty()) (void)h->setBreakpointCondition(b.id, t.condition);
            bpDirty_ = true;
            return true;
        }
    (void)h->addBreakpoint(key, t.line, t.condition);
    std::string said = "Point d'arr\xC3\xAAt pos\xC3\xA9 : " + key + ", ligne " + std::to_string(t.line);
    if (!t.condition.empty()) said += ", si " + t.condition;
    journal("point-arret", said, key, t.line);
    status(said + ".");
    bpDirty_ = true;
    (void)showSection(key, t.line);
    return true;
}

bool SimDebugPane::setBreakpointCondition(std::size_t number, const std::string& condition) {
    auto* h = host();
    if (!h || number == 0 || number > bps_.size()) return false;
    const auto& b = bps_[number - 1];
    const auto c = trim(condition);
    if (!h->setBreakpointCondition(b.id, c)) return false;
    journal("condition", c.empty() ? "Point d'arr\xC3\xAAt " + std::to_string(number) + " : sans condition"
                                   : "Point d'arr\xC3\xAAt " + std::to_string(number) + " : seulement si " + c,
            b.section, b.line);
    bpDirty_ = true;
    return true;
}

bool SimDebugPane::setBreakpointEnabled(std::size_t number, bool enabled) {
    auto* h = host();
    if (!h || number == 0 || number > bps_.size()) return false;
    const auto b = bps_[number - 1];
    if (!h->setBreakpointEnabled(b.id, enabled)) return false;
    const std::string said = "Point d'arr\xC3\xAAt " + std::to_string(number) + (enabled ? " activ\xC3\xA9" : " d\xC3\xA9sactiv\xC3\xA9")
                           + " : " + b.section + ", ligne " + std::to_string(b.line);
    journal("point-arret", said, b.section, b.line);
    status(said + ".");
    bpDirty_ = true;
    refreshBreakpoints();
    return true;
}

bool SimDebugPane::removeBreakpoint(std::size_t number) {
    auto* h = host();
    if (!h || number == 0 || number > bps_.size()) return false;
    const auto b = bps_[number - 1];
    if (!h->removeBreakpoint(b.id)) return false;
    const std::string said = "Point d'arr\xC3\xAAt enlev\xC3\xA9 : " + b.section + ", ligne " + std::to_string(b.line);
    journal("point-arret", said, b.section, b.line);
    status(said + ".");
    bpDirty_ = true;
    refreshBreakpoints();
    return true;
}

void SimDebugPane::clearBreakpoints() {
    auto* h = host();
    if (!h) return;
    const auto n = bps_.size();
    for (const auto& b : bps_) (void)h->removeBreakpoint(b.id);
    if (n > 0) {
        journal("point-arret", "Tous les points d'arr\xC3\xAAt enlev\xC3\xA9s (" + std::to_string(n) + ")");
        status("Tous les points d'arr\xC3\xAAt sont enlev\xC3\xA9s.");
    }
    bpDirty_ = true;
    refreshBreakpoints();
}

void SimDebugPane::disableAllBreakpoints() {
    auto* h = host();
    if (!h) return;
    std::size_t n = 0;
    for (const auto& b : bps_)
        if (b.enabled && h->setBreakpointEnabled(b.id, false)) ++n;
    if (n > 0) {
        journal("point-arret", "Tous les points d'arr\xC3\xAAt d\xC3\xA9sactiv\xC3\xA9s (" + std::to_string(n) + ")");
        status("Tous les points d'arr\xC3\xAAt sont d\xC3\xA9sactiv\xC3\xA9s : ils restent dans la liste, coche-les pour les r\xC3\xA9" "activer.");
    }
    bpDirty_ = true;
    refreshBreakpoints();
}

bool SimDebugPane::modifyProject() {
    auto* h = host();
    if (!h || !h->attached() || h->state() != SimulationHost::State::Paused) {
        status("Mets d'abord la simulation en pause.");
        return false;
    }
    std::string section = codeSection_;
    auto line = static_cast<std::uint32_t>(code_->caretLine() + 1);
    if (const auto& hit = h->lastBreakHit(); hit && hit->line > 0) {
        section = hit->section;
        line = static_cast<std::uint32_t>(hit->line);
    }
    if (section.empty() || !hosts_.goToLine) {
        status("Pas de code \xC3\xA0 modifier ici : choisis une section (la pile, la trace du cycle).");
        return false;
    }
    hosts_.goToLine(section, line);
    status("Modifie le code ou les types comme d'habitude : \xC2\xAB Continuer \xC2\xBB reprendra au cycle " + sd::grouped(h->scanCount())
           + ", sans repartir du cycle 0.");
    return true;
}

// ------------------------------------------------------------------ les gestes ----
bool SimDebugPane::runToLine(int line) {
    auto* h = host();
    if (!h || codeSection_.empty()) return false;
    if (line <= 0) line = static_cast<int>(code_->caretLine()) + 1;
    if (h->state() == SimulationHost::State::Halted) {
        status("Halte : Arr\xC3\xAAter d'abord (la simulation repart des valeurs initiales).");
        return false;
    }
    if (!ensureAttached()) return false;
    if (runToId_ != 0) (void)h->removeBreakpoint(runToId_);
    runToId_ = h->addBreakpoint(codeSection_, line, {});
    runToText_ = codeSection_ + ", ligne " + std::to_string(line);
    gesture_ = sd::LastGesture::RunToLine;
    h->setState(SimulationHost::State::Running);
    journal("continuer", "Ex\xC3\xA9" "cuter jusqu'\xC3\xA0 " + runToText_, codeSection_, line);
    stateSeen_ = -2;
    bpDirty_ = true;
    return true;
}

void SimDebugPane::transport(int action) {
    auto* h = host();
    if (!h || !project()) {
        status("Simulation : ouvre d'abord un projet.");
        return;
    }
    using S = SimulationHost::State;
    const bool halted = h->state() == S::Halted;
    const auto refuseHalted = [&] {
        status("Halte : Arr\xC3\xAAter d'abord (Maj+F5) - la simulation repart des valeurs initiales.");
    };
    switch (action) {
        case AContinue:
            if (halted) { refuseHalted(); return; }
            if (h->state() == S::Running) return;
            if (!ensureAttached()) return;
            gesture_ = sd::LastGesture::Continue;
            h->setState(S::Running);
            journal("continuer", bps_.empty() ? std::string("Continuer : le programme tourne (aucun point d'arr\xC3\xAAt)")
                                              : "Continuer : jusqu'au prochain point d'arr\xC3\xAAt (" + sd::plural(bps_.size(), "pos\xC3\xA9", "pos\xC3\xA9s") + ")");
            break;
        case AStepSection: {
            if (halted) { refuseHalted(); return; }
            if (h->state() == S::Running) h->setState(S::Paused);
            if (!ensureAttached()) return;
            std::string next = h->nextSection();
            gesture_ = sd::LastGesture::StepSection;
            h->stepSection();
            if (next.empty() && !times_.empty()) next = times_.front().section;
            journal("pas", "Section suivante : " + (next.empty() ? std::string("la premi\xC3\xA8re du cycle") : next)
                               + " (cycle " + sd::grouped(h->scanCount()) + ")", next, 0);
            break;
        }
        case AStepCycle:
            if (halted) { refuseHalted(); return; }
            if (h->state() == S::Running) h->setState(S::Paused);
            if (!ensureAttached()) return;
            gesture_ = sd::LastGesture::StepCycle;
            h->step();
            journal("pas", "Cycle suivant : cycle " + sd::grouped(h->scanCount()) + " termin\xC3\xA9");
            break;
        case APause:
            if (h->state() != S::Running) return;
            gesture_ = sd::LastGesture::Pause;
            h->setState(S::Paused);
            journal("pause", "Pause au cycle " + sd::grouped(h->scanCount()));
            break;
        case AStop:
            if (!h->attached() || h->state() == S::Stopped) return;
            gesture_ = sd::LastGesture::None;
            h->setState(S::Stopped);
            journal("arreter", "Arr\xC3\xAAt\xC3\xA9" "e : tout repart des valeurs initiales");
            break;
        default:
            return;
    }
    stateSeen_ = -2;                        // la prochaine image relit tout
    bpDirty_ = true;
    if (frame_) frame_->invalidate();
    invalidate();
}

void SimDebugPane::runAction(int action) {
    switch (action) {
        case AContinue:
        case AStepSection:
        case AStepCycle:
        case APause:
        case AStop:
            transport(action);
            break;
        case ARunToLine:
            (void)runToLine(0);
            break;
        case AAddBreakpoint:
            askBreakpoint();
            break;
        case AAddWatch:
            askWatch();
            break;
        case AClearBreakpoints:
            clearBreakpoints();
            break;
        case ADisableAll:
            disableAllBreakpoints();
            break;
        case AModify:
            (void)modifyProject();
            break;
        case AHelp:
            if (hosts_.help) hosts_.help();
            break;
        default:
            break;
    }
}

void SimDebugPane::askBreakpoint() {
    if (!hosts_.ask) return;
    const std::string initial = codeSection_.empty() ? std::string{} : codeSection_ + " " + std::to_string(code_->caretLine() + 1);
    hosts_.ask("Poser un point d'arr\xC3\xAAt", "Section et ligne",
               "La section et la ligne : \xC2\xAB SFC_PurgeA 42 \xC2\xBB. Une condition apr\xC3\xA8s \xC2\xAB si \xC2\xBB : "
               "\xC2\xAB SFC_PurgeA 42 si Armoires[0].etat = 3 \xC2\xBB - le programme ne s'y arr\xC3\xAAtera que si elle est vraie.",
               initial, [this](const std::string& answer) {
                   std::string why;
                   if (!addBreakpointText(answer, &why) && !why.empty()) status(why);
               });
}

void SimDebugPane::askWatch() {
    if (!hosts_.ask) return;
    hosts_.ask("Ajouter un espion", "Chemin",
               "Un chemin de la simulation : Armoires[0].ana.PT1.mes ; une locale d'unit\xC3\xA9 : Unite.compteur. "
               "Sa valeur se suit ici, avec le cycle o\xC3\xB9 elle a chang\xC3\xA9.",
               writerPath_, [this](const std::string& answer) {
                   std::string why;
                   if (!addWatch(answer, &why) && !why.empty()) status(why);
               });
}

// ------------------------------------------------------------------ la mise en page ----
void SimDebugPane::onLayout() { layoutParts(); }

void SimDebugPane::layoutParts() {
    const auto b = bounds();
    const float pad = 10.f, gap = 10.f;
    band_ = {b.x, b.y, b.w, 62.f};
    const float bottomH = std::clamp(b.h * 0.27f, 150.f, 230.f);
    const gfx::Rect bottom{b.x + pad, b.bottom() - bottomH - pad, std::max(0.f, b.w - 2.f * pad), bottomH};
    const float midTop = band_.bottom() + gap;
    const gfx::Rect mid{b.x + pad, midTop, std::max(0.f, b.w - 2.f * pad), std::max(0.f, bottom.y - gap - midTop)};
    float leftW = std::clamp(mid.w * 0.24f, 250.f, 340.f);
    float rightW = std::clamp(mid.w * 0.23f, 240.f, 330.f);
    if (mid.w - leftW - rightW - 2.f * gap < 360.f) rightW = 0.f;             // etroit : pas de colonne de droite
    if (mid.w - leftW - rightW - 2.f * gap < 300.f) leftW = std::max(200.f, mid.w * 0.35f);
    const gfx::Rect left{mid.x, mid.y, leftW, mid.h};
    side_ = rightW > 0.f ? gfx::Rect{mid.right() - rightW, mid.y, rightW, mid.h} : gfx::Rect{};
    const float centreX = left.right() + gap;
    const float centreR = rightW > 0.f ? side_.x - gap : mid.right();
    const gfx::Rect centre{centreX, mid.y, std::max(0.f, centreR - centreX), mid.h};

    // A gauche : les points d'arret, puis les espions.
    const float listsH = std::max(0.f, left.h - 2.f * kTitleH - gap);
    const float bpH = std::floor(listsH * 0.42f);
    bpTitle_ = {left.x, left.y, left.w, kTitleH};
    bpTable_->setBounds({left.x, bpTitle_.bottom(), left.w, bpH});
    watchTitle_ = {left.x, bpTitle_.bottom() + bpH + gap, left.w, kTitleH};
    // Lot API 8 (2e partie) : dessous, le champ « Ajouter un espion : tape un nom ».
    const float fieldH = 28.f;
    watchTable_->setBounds({left.x, watchTitle_.bottom(), left.w, std::max(0.f, left.bottom() - watchTitle_.bottom() - fieldH - 6.f)});
    watchField_->setBounds({left.x, std::max(watchTitle_.bottom(), left.bottom() - fieldH), left.w, fieldH});

    // Au centre : la pile, le code.
    stackRect_ = {centre.x, centre.y, centre.w, 30.f};
    code_->setBounds({centre.x, stackRect_.bottom() + 4.f, centre.w, std::max(0.f, centre.bottom() - stackRect_.bottom() - 4.f)});

    // En bas : le temps (a gauche), la trace (a droite).
    const float half = std::floor((bottom.w - gap) * 0.5f);
    timesBox_ = {bottom.x, bottom.y, half, bottom.h};
    traceTitle_ = {bottom.x + half + gap, bottom.y, bottom.w - half - gap, kTitleH};
    traceTable_->setBounds({traceTitle_.x, traceTitle_.bottom(), traceTitle_.w, std::max(0.f, bottom.bottom() - traceTitle_.bottom())});

    // Les colonnes des tables suivent leur largeur.
    {
        const float w = bpTable_->bounds().w;
        std::vector<ui::TableView::Column> cols(BCount);
        const float dot = 28.f, hits = 70.f, cross = 26.f;
        // Lot API 8 : corrections des captures - "Section : ligne" d'abord (60 %) :
        // la condition est souvent "sans condition".
        const float where = std::max(90.f, (w - dot - hits - cross - 14.f) * 0.6f);
        const float cond = std::max(80.f, w - dot - hits - cross - where - 14.f);
        const float widths[BCount] = {dot, where, cond, hits, cross};
        for (std::size_t i = 0; i < BCount; ++i) {
            cols[i].title = bpModel_->headerText(i);
            cols[i].width = widths[i];
            cols[i].minWidth = i == BDot || i == BRemove ? 20.f : 40.f;
            cols[i].sortable = false;
            cols[i].filterable = false;
            cols[i].resizable = i == BWhere || i == BCondition;
        }
        cols[BHits].align = ui::Align::End;
        cols[BRemove].align = ui::Align::Center;
        bpTable_->setColumns(std::move(cols));
    }
    {
        const float w = watchTable_->bounds().w;
        std::vector<ui::TableView::Column> cols(WCount);
        const float cross = 26.f;
        const float value = std::clamp(w * 0.26f, 70.f, 130.f);
        const float since = w >= 380.f ? std::clamp(w * 0.3f, 90.f, 190.f) : 0.f;
        const float name = std::max(90.f, w - value - since - cross - 14.f);
        const float widths[WCount] = {name, value, since > 0.f ? since : 120.f, cross};
        for (std::size_t i = 0; i < WCount; ++i) {
            cols[i].title = watchModel_->headerText(i);
            cols[i].width = widths[i];
            cols[i].minWidth = i == WRemove ? 20.f : 40.f;
            cols[i].sortable = false;
            cols[i].filterable = false;
        }
        cols[WValue].align = ui::Align::End;
        cols[WSince].visible = since > 0.f;
        cols[WRemove].align = ui::Align::Center;
        watchTable_->setColumns(std::move(cols));
    }
    {
        const float w = traceTable_->bounds().w;
        std::vector<ui::TableView::Column> cols(TCount);
        const float rank = 40.f, statements = 96.f, time = 86.f;
        const float entry = std::max(80.f, (w - rank - statements - time - 14.f) * 0.45f);
        const float section = std::max(90.f, w - rank - statements - time - entry - 14.f);
        const float widths[TCount] = {rank, entry, section, statements, time};
        for (std::size_t i = 0; i < TCount; ++i) {
            cols[i].title = traceModel_->headerText(i);
            cols[i].width = widths[i];
            cols[i].sortable = false;
            cols[i].filterable = false;
        }
        cols[TRank].align = ui::Align::End;
        cols[TStatements].align = ui::Align::End;
        cols[TTime].align = ui::Align::End;
        traceTable_->setColumns(std::move(cols));
    }
}

// ------------------------------------------------------------------- la peinture ----
int SimDebugPane::hitAt(gfx::Point p) const {
    for (std::size_t i = 0; i < hits_.size(); ++i)
        if (hits_[i].rect.contains(p)) return static_cast<int>(i);
    return -1;
}

gfx::Rect SimDebugPane::partRect(std::string_view key) const {
    for (const auto& h : hits_)
        if (h.key == key) return h.rect;
    return {};
}

void SimDebugPane::paintTitle(const ui::PaintContext& ctx, const gfx::Rect& r, const std::string& title, const std::string& count,
                              const std::vector<std::pair<std::string, std::string>>& links) {
    auto& rr = ctx.r;
    const auto& c = ctx.theme.color;
    const float ty = r.y + (r.h - rr.lineHeight(kSmall)) * 0.5f;
    drawBold(rr, {r.x + 2.f, ty}, title, kSmall, c.textMuted);
    float x = r.x + 2.f + rr.measure(title, kSmall).width + 8.f;
    // Trop etroit pour le compteur entier et les liens : le nombre seul ("2").
    float linksW = 0.f;
    for (const auto& link : links) linksW += rr.measure(link.first, kSmall).width + 14.f;
    std::string shown = count;
    if (!shown.empty() && x + rr.measure(shown, kSmall).width + 12.f + linksW > r.right()) shown = shown.substr(0, shown.find(kDot));
    if (!shown.empty()) {
        const float w = rr.measure(shown, kSmall).width + 12.f;
        rr.fillRoundedRect({x, ty - 1.f, w, rr.lineHeight(kSmall) + 2.f}, c.headerBg, 7.f);
        rr.drawText({x + 6.f, ty}, shown, kSmall, c.textMuted);
    }
    float right = r.right() - 2.f;
    for (auto it = links.rbegin(); it != links.rend(); ++it) {
        const auto& [label, key] = *it;
        const float w = rr.measure(label, kSmall).width;
        const gfx::Rect box{right - w - 4.f, ty - 2.f, w + 8.f, rr.lineHeight(kSmall) + 4.f};
        const bool hot = hover_ >= 0 && static_cast<std::size_t>(hover_) < hits_.size() && hits_[static_cast<std::size_t>(hover_)].key == key;
        const auto lc = ctx.theme.onSurface(c.accent);
        rr.drawText({box.x + 4.f, ty}, label, kSmall, lc);
        if (hot) rr.fillRect({box.x + 4.f, ty + rr.lineHeight(kSmall) - 1.f, w, 1.f}, lc);
        hits_.push_back({box, key, {}});
        right = box.x - 10.f;
    }
}

void SimDebugPane::paintBand(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto& c = th.color;
    const auto f = facts();
    using S = SimulationHost::State;
    ui::Tone tone = ui::Tone::Muted;
    if (f.attached) {
        switch (f.state) {
            case S::Running: tone = ui::Tone::Ok; break;
            case S::Paused:  tone = f.hit ? ui::Tone::Warning : ui::Tone::Info; break;
            case S::Halted:  tone = ui::Tone::Error; break;
            default:         break;
        }
    }
    const auto col = th.onSurface(th.tone(tone, c.textMuted));
    const auto b = band_;
    r.fillRect(b, tone == ui::Tone::Muted ? c.panelBg : col.withAlpha(th.isDark() ? 34 : 24));
    r.fillRect({b.x, b.y, 4.f, b.h}, col);
    r.fillRect({b.x, b.bottom() - 1.f, b.w, 1.f}, c.border);
    const float cy = b.y + 22.f;
    float x = b.x + 18.f;
    if (tone == ui::Tone::Ok) r.fillRoundedRect({x - 4.f, cy - 11.f, 22.f, 22.f}, col.withAlpha(60), 11.f);
    if (f.hit) {
        const gfx::Rect dot{x - 1.f, cy - 8.f, 16.f, 16.f};
        paintDot(r, IHere, dot, th);
    } else {
        r.fillRoundedRect({x, cy - 7.f, 14.f, 14.f}, col, 7.f);
    }
    x += 28.f;
    const auto sentence = sd::stateSentence(f);
    r.pushClip({x, b.y, std::max(0.f, b.right() - x - 12.f), b.h});
    drawBold(r, {x, cy - r.lineHeight(kLead) * 0.5f}, elide(r, sentence, kLead, b.right() - x - 14.f), kLead, c.text);
    // La deuxieme ligne : ou aller, ce que font les touches.
    const float y2 = b.y + 40.f;
    float at = x;
    const auto link = [&](const std::string& label, const std::string& key) {
        const auto lc = th.onSurface(c.accent);
        const float w = r.measure(label, kSmall).width;
        const bool hot = hover_ >= 0 && static_cast<std::size_t>(hover_) < hits_.size() && hits_[static_cast<std::size_t>(hover_)].key == key;
        r.drawText({at, y2}, label, kSmall, lc);
        if (hot) r.fillRect({at, y2 + r.lineHeight(kSmall) - 1.f, w, 1.f}, lc);
        hits_.push_back({gfx::Rect{at - 2.f, y2 - 2.f, w + 4.f, r.lineHeight(kSmall) + 4.f}, key, {}});
        at += w + 16.f;
    };
    std::string hint;
    if (f.hit) {
        link("Aller \xC3\xA0 la ligne dans l'onglet de la section", "etat:aller");
        hint = "F5 continue \xC2\xB7 F11 fait un seul cycle \xC2\xB7 F10 avance d'une section \xC2\xB7 Maj+F5 arr\xC3\xAAte";
    } else if (!f.attached || f.state == S::Stopped) {
        hint = "Pose tes points d'arr\xC3\xAAt (un clic dans la marge du code), puis F5 : le programme de MAST tournera jusqu'\xC3\xA0 eux.";
    } else if (f.state == S::Running) {
        // Les mots de la maquette (le bandeau de l'onglet Debogage, en marche).
        hint = "Clique dans la marge d'une ligne pour poser un point d'arr\xC3\xAAt : la simulation s'arr\xC3\xAAtera dessus.";
    } else if (f.state == S::Halted) {
        hint = "Arr\xC3\xAAter (Maj+F5) repart des valeurs initiales ; l'onglet Automate dit o\xC3\xB9 et pourquoi.";
    } else {
        hint = "F5 continue \xC2\xB7 F11 fait un seul cycle \xC2\xB7 F10 avance d'une section \xC2\xB7 Ctrl+F10 jusqu'\xC3\xA0 la ligne du curseur";
    }
    r.drawText({at, y2}, elide(r, hint, kSmall, b.right() - at - 14.f), kSmall, c.textMuted);
    r.popClip();
}

void SimDebugPane::paintStack(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto& c = th.color;
    const auto box = stackRect_;
    r.fillRoundedRect(box, c.headerBg, 5.f);
    auto* h = host();
    const bool live = h && h->state() == SimulationHost::State::Paused && h->lastBreakHit();
    float x = box.x + 8.f;
    const float ty = box.y + (box.h - r.lineHeight(kSmall)) * 0.5f;
    r.pushClip(box);
    const std::string lead = live ? "La pile" : "Le code montr\xC3\xA9";
    drawBold(r, {x, ty}, lead, kSmall, c.textMuted);
    x += r.measure(lead, kSmall).width + 12.f;
    if (levels_.empty()) {
        r.drawText({x, ty}, "choisis une section : un point d'arr\xC3\xAAt, la trace du cycle, ou un clic sur une barre du temps", kSmall, c.textDisabled);
    }
    for (std::size_t i = 0; i < levels_.size(); ++i) {
        const auto& l = levels_[i];
        const std::string key = "pile:" + std::to_string(i);
        const float w = r.measure(l.label, kSmall).width + 16.f;
        const gfx::Rect pill{x, box.y + 4.f, w, box.h - 8.f};
        const bool chosen = i == level_;
        const bool hot = hover_ >= 0 && static_cast<std::size_t>(hover_) < hits_.size() && hits_[static_cast<std::size_t>(hover_)].key == key;
        if (chosen) r.fillRoundedRect(pill, live ? th.onSurface(c.warning).withAlpha(70) : c.selectionBg, 4.f);
        else if (hot) r.fillRoundedRect(pill, c.rowAltBg, 4.f);
        const auto ink = chosen ? c.text : live ? c.text : c.textMuted;
        if (chosen) drawBold(r, {pill.x + 8.f, ty}, l.label, kSmall, ink);
        else r.drawText({pill.x + 8.f, ty}, l.label, kSmall, ink);
        hits_.push_back({pill, key, sd::levelTip(l)});
        x = pill.right() + 2.f;
        if (i + 1 < levels_.size()) {
            r.drawText({x + 2.f, ty}, "\xE2\x80\xBA", kSmall, c.textMuted);
            x += r.measure("\xE2\x80\xBA", kSmall).width + 6.f;
        }
    }
    r.popClip();
}

void SimDebugPane::paintSide(const ui::PaintContext& ctx) {
    if (side_.w <= 0.f || side_.h <= 0.f) return;
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto& c = th.color;
    panel(r, th, side_);
    const float innerW = side_.w - 24.f;
    float y = side_.y + 4.f;
    const float lh = r.lineHeight(kBody) + 3.f;
    r.pushClip(side_);
    paintTitle(ctx, {side_.x + 10.f, y, side_.w - 20.f, kTitleH}, "POURQUOI ICI", {}, {});
    y += kTitleH + 2.f;
    const auto f = facts();
    const std::string proposal = proposedCondition();     // Lot API 8 (2e partie) : la place du bouton
    const float whyLimit = side_.y + side_.h * 0.58f - (proposal.empty() ? 0.f : 34.f);
    for (const auto& paragraph : sd::whyHere(f)) {
        for (const auto& line : wrap(r, paragraph, kBody, innerW)) {
            if (y + lh > whyLimit) break;
            r.drawText({side_.x + 12.f, y}, line, kBody, c.text);
            y += lh;
        }
        y += 5.f;
        if (y > whyLimit) break;
    }
    // ---- Lot API 8 (2e partie) : « Mettre la condition nom = valeur » (un point sans condition) ----
    if (!proposal.empty()) {
        const std::string label = "Mettre la condition " + proposal;
        const float bw = std::min(innerW, r.measure(label, kSmall).width + 20.f);
        const gfx::Rect button{side_.x + 12.f, std::min(y, whyLimit) + 2.f, bw, r.lineHeight(kSmall) + 10.f};
        const bool hot = hover_ >= 0 && static_cast<std::size_t>(hover_) < hits_.size() && hits_[static_cast<std::size_t>(hover_)].key == "pourquoi:condition";
        const auto ac = th.onSurface(c.accent);
        if (hot) r.fillRect(button, c.selectionBg);
        r.strokeRect(button, ac, 1.f);
        r.drawText({button.x + 10.f, button.y + 5.f}, elide(r, label, kSmall, bw - 16.f), kSmall, ac);
        hits_.push_back({button, "pourquoi:condition",
                         "Le programme ne s'arr\xC3\xAAtera plus ici que si " + proposal + " (la valeur lue sur cette ligne au passage)."});
        y = button.bottom() + 6.f;
    }
    // Qui a ecrit ?
    y = std::max(y + 6.f, std::min(whyLimit + 6.f, side_.bottom() - 150.f));
    r.fillRect({side_.x + 10.f, y - 4.f, side_.w - 20.f, 1.f}, c.border);
    paintTitle(ctx, {side_.x + 10.f, y, side_.w - 20.f, kTitleH}, "QUI A \xC3\x89" "CRIT ?", {}, {});
    y += kTitleH + 2.f;
    const auto small = r.lineHeight(kSmall) + 3.f;
    if (writerPath_.empty()) {
        for (const auto& line : wrap(r, "Choisis une variable : un espion, ou un nom du code (le curseur dessus). Double-clic sur un nom : il devient un espion.",
                                     kSmall, innerW)) {
            r.drawText({side_.x + 12.f, y}, line, kSmall, c.textMuted);
            y += small;
        }
    } else {
        drawBold(r, {side_.x + 12.f, y}, elide(r, writerPath_, kBody, innerW), kBody, c.text);
        y += lh;
        if (!writerValue_.empty()) {
            r.drawText({side_.x + 12.f, y}, elide(r, "= " + writerValue_, kBody, innerW), kBody, th.onSurface(c.accent));
            y += lh;
        }
        // La maquette : forcee, personne ne l'ecrit (le forcage gagne sur le code).
        if (auto* rt = runtime(); rt && rt->isForced(writerPath_)) {
            for (const auto& line : wrap(r, "Personne : elle est forc\xC3\xA9" "e \xC3\xA0 " + writerValue_ + ". Sans le for\xC3\xA7" "age, le code ci-dessous l'\xC3\xA9" "crit.",
                                         kSmall, innerW)) {
                r.drawText({side_.x + 12.f, y}, line, kSmall, th.onSurface(c.warning));
                y += small;
            }
        }
        auto* h = host();
        const auto linkAt = [&](float lx, float ly, const std::string& label, const std::string& key) {
            const auto lc = th.onSurface(c.accent);
            const float w = r.measure(label, kSmall).width;
            const bool hot = hover_ >= 0 && static_cast<std::size_t>(hover_) < hits_.size() && hits_[static_cast<std::size_t>(hover_)].key == key;
            r.drawText({lx, ly}, label, kSmall, lc);
            if (hot) r.fillRect({lx, ly + r.lineHeight(kSmall) - 1.f, w, 1.f}, lc);
            hits_.push_back({gfx::Rect{lx - 2.f, ly - 2.f, w + 4.f, r.lineHeight(kSmall) + 4.f}, key, {}});
            return w;
        };
        if (writerEngine_) {
            const std::string said = "\xC3\x89" "crite par " + sd::writeText(writerLast_, h ? h->scanCount() : 0);
            for (const auto& line : wrap(r, said, kSmall, innerW - 60.f)) {
                r.drawText({side_.x + 12.f, y}, line, kSmall, c.text);
                y += small;
            }
            (void)linkAt(side_.x + 12.f, y, "Aller \xC3\xA0 la ligne", "ecrit:aller");
            y += small + 4.f;
        }
        if (!writerSites_.empty()) {
            r.drawText({side_.x + 12.f, y}, writerEngine_ ? "Dans le code, aussi :" : "Les lignes du code qui l'\xC3\xA9" "crivent :", kSmall, c.textMuted);
            y += small;
            for (std::size_t i = 0; i < writerSites_.size() && y + small < side_.bottom() - 4.f; ++i) {
                const auto& s = writerSites_[i];
                const std::string where = s.section + ", ligne " + std::to_string(s.line);
                const float w = linkAt(side_.x + 12.f, y, elide(r, where, kSmall, innerW * 0.6f), "ecrit:" + std::to_string(i));
                r.drawText({side_.x + 20.f + w, y}, elide(r, s.text, kSmall, innerW - w - 10.f), kSmall, c.textMuted);
                y += small;
                // 1.11.2 (D25, decision 143) : sa section n'a pas tourne au dernier cycle
                // (condition d'activation fausse) : cette ligne n'a rien ecrit.
                const auto off = sd::inactiveSectionText(times_, s.unit, s.section);
                if (!off.empty() && y + small < side_.bottom() - 4.f) {
                    const auto shown = elide(r, off, kSmall, innerW - 12.f);
                    r.drawText({side_.x + 24.f, y}, shown, kSmall, th.onSurface(c.warning));
                    hits_.push_back({gfx::Rect{side_.x + 22.f, y - 2.f, r.measure(shown, kSmall).width + 4.f, r.lineHeight(kSmall) + 4.f},
                                     "inactif:" + std::to_string(i),
                                     "Au dernier cycle, la condition d'activation de " + s.section
                                         + " \xC3\xA9tait fausse : la section n'a pas tourn\xC3\xA9, cette ligne n'a rien \xC3\xA9" "crit"
                                           " (comme sur l'automate). La trace du cycle le montre."});
                    y += small;
                }
            }
        } else if (!writerEngine_) {
            for (const auto& line : wrap(r, "Aucune ligne du code ne l'\xC3\xA9" "crit : elle vient de l'ext\xC3\xA9rieur (une entr\xC3\xA9" "e, l'IHM, un for\xC3\xA7" "age).",
                                         kSmall, innerW)) {
                r.drawText({side_.x + 12.f, y}, line, kSmall, c.textMuted);
                y += small;
            }
        }
    }
    r.popClip();
}

void SimDebugPane::paintTimes(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto& c = th.color;
    const auto box = timesBox_;
    if (box.w <= 0.f || box.h <= 0.f) return;
    paintTitle(ctx, {box.x, box.y, box.w, kTitleH}, "O\xC3\x99 PASSE LE TEMPS DU CYCLE", !bars_.empty() ? std::to_string(times_.size()) : std::string{}, {});
    const gfx::Rect body{box.x, box.y + kTitleH, box.w, box.h - kTitleH};
    panel(r, th, body);
    r.pushClip(body);
    float y = body.y + 8.f;
    if (bars_.empty()) {
        for (const auto& line : wrap(r, "Pas encore de cycle mesur\xC3\xA9 : Cycle suivant (F11) en fait un ; le temps de chaque section, "
                                        "la plus lente en t\xC3\xAAte, s'affiche ici (en ms et en part de la p\xC3\xA9riode de MAST).",
                                     kSmall, body.w - 24.f)) {
            r.drawText({body.x + 12.f, y}, line, kSmall, c.textMuted);
            y += r.lineHeight(kSmall) + 3.f;
        }
        r.popClip();
        return;
    }
    r.drawText({body.x + 12.f, y}, elide(r, timeSummary_, kSmall, body.w - 24.f), kSmall, c.text);
    y += r.lineHeight(kSmall) + 8.f;
    const float labelW = std::floor((body.w - 24.f) * 0.36f);
    const float valueW = 118.f;
    const float trackX = body.x + 12.f + labelW + 8.f;
    const float trackW = std::max(20.f, body.right() - 12.f - valueW - 8.f - trackX);
    const float rowH = std::max(18.f, std::min(24.f, (body.bottom() - y - 6.f) / static_cast<float>(bars_.size())));
    for (std::size_t i = 0; i < bars_.size(); ++i) {
        const auto& bar = bars_[i];
        if (y + rowH > body.bottom() - 2.f) break;
        const std::string key = "temps:" + std::to_string(i);
        const gfx::Rect row{body.x + 6.f, y, body.w - 12.f, rowH};
        const bool hot = hover_ >= 0 && static_cast<std::size_t>(hover_) < hits_.size() && hits_[static_cast<std::size_t>(hover_)].key == key;
        if (hot) r.fillRoundedRect(row, c.rowAltBg, 4.f);
        const float ty = y + (rowH - r.lineHeight(kSmall)) * 0.5f;
        r.drawText({body.x + 12.f, ty}, elide(r, bar.label, kSmall, labelW), kSmall, c.text);
        // La piste : toute la periode de MAST ; la barre : la part de cette section.
        const float barH = 8.f;
        const float by = y + (rowH - barH) * 0.5f;
        r.fillRoundedRect({trackX, by, trackW, barH}, c.headerBg, 4.f);
        const double share = std::min(100.0, bar.ofPeriod);
        const float w = std::max(2.f, static_cast<float>(share / 100.0) * trackW);
        const auto ink = bar.ofPeriod > 100.0 ? c.error : bar.ofPeriod > 50.0 ? c.warning : c.accent;
        r.fillRoundedRect({trackX, by, w, barH}, th.onSurface(ink), 4.f);
        const std::string value = sd::millis(bar.micros) + kDot + sd::percent(bar.ofPeriod);
        const float vw = r.measure(value, kSmall).width;
        r.drawText({body.right() - 12.f - vw, ty}, value, kSmall, c.textMuted);
        hits_.push_back({row, key,
                         bar.label + " : " + sd::millis(bar.micros) + ", " + sd::percent(bar.ofPeriod) + " de la p\xC3\xA9riode de MAST, "
                             + sd::percent(bar.ofCycle) + " du cycle ; " + sd::plural(bar.statements, "instruction", "instructions")
                             + ". Un clic : son code."});
        y += rowH;
    }
    r.popClip();
}

void SimDebugPane::onPaint(const ui::PaintContext& ctx) {
    g_theme = &ctx.theme;
    hits_.clear();
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    r.fillRect(bounds(), c.windowBg);
    paintBand(ctx);
    // ---- Lot API 8 : corrections des captures ---- la colonne "Section : ligne"
    // coupait la fin ("Acquisiti...") : la ligne ne se voyait pas. Le milieu du
    // nom de la section cede sa place ("Acqui...ANA : 12") ; la table (dessinee
    // apres) lit bpWhereShown_ ; l'infobulle de la ligne dit tout.
    {
        bpWhereShown_.assign(bps_.size(), std::string{});
        const auto& cols = bpTable_->columns();
        const float avail = cols.size() > BWhere ? cols[BWhere].width - 14.f : 0.f;
        const auto font = ctx.theme.font.ui;
        for (std::size_t i = 0; i < bps_.size() && avail > 0.f; ++i) {
            const std::string& sec = bps_[i].section;
            const std::string tail = " : " + std::to_string(bps_[i].line);
            if (r.measure(sec + tail, font).width <= avail) continue;
            std::string shown = "..." + tail;
            for (std::size_t keep = sec.size(); keep-- > 2;) {
                std::size_t head = (keep + 1) / 2;
                std::size_t from = sec.size() - (keep - head);
                while (head > 0 && (static_cast<unsigned char>(sec[head]) & 0xC0) == 0x80) --head;
                while (from < sec.size() && (static_cast<unsigned char>(sec[from]) & 0xC0) == 0x80) ++from;
                std::string s = sec.substr(0, head) + "..." + sec.substr(from) + tail;
                if (r.measure(s, font).width <= avail) { shown = std::move(s); break; }
            }
            bpWhereShown_[i] = std::move(shown);
        }
    }
    // Les compteurs de la maquette : "2 · 1 actif", "3 · au cycle 1 243".
    const auto active = static_cast<std::size_t>(std::count_if(bps_.begin(), bps_.end(), [](const auto& b) { return b.enabled; }));
    const std::string bpCount = bps_.empty() ? std::string("0")
                                             : std::to_string(bps_.size()) + kDot + std::to_string(active) + (active > 1 ? " actifs" : " actif");
    std::string watchCount = std::to_string(watches_.size());
    if (auto* h = host(); h && h->attached()) watchCount += std::string(kDot) + "au cycle " + sd::grouped(h->scanCount());
    paintTitle(ctx, bpTitle_, "POINTS D'ARR\xC3\x8AT", bpCount,
               bps_.empty() ? std::vector<std::pair<std::string, std::string>>{{"+ Point d'arr\xC3\xAAt", "points:ajouter"}}
                            : std::vector<std::pair<std::string, std::string>>{{"+ Point d'arr\xC3\xAAt", "points:ajouter"}, {"Tout enlever", "points:tout"}});
    paintTitle(ctx, watchTitle_, "ESPIONS", watchCount,
               {{"+ Espion", "espions:ajouter"}, {"Retirer", "espions:retirer"}});
    paintStack(ctx);
    paintSide(ctx);
    paintTimes(ctx);
    paintTitle(ctx, traceTitle_, plannedTrace_ ? "LA TRACE DU CYCLE (l'ordre pr\xC3\xA9vu)" : "LA TRACE DU CYCLE",
               traceModel_->rows().empty() ? std::string{} : std::to_string(traceModel_->rows().size()), {});
    // L'infobulle suit la zone survolee.
    const std::string tip = hover_ >= 0 && static_cast<std::size_t>(hover_) < hits_.size() ? hits_[static_cast<std::size_t>(hover_)].tip : std::string{};
    if (tip != tooltip()) setTooltip(tip);
}

void SimDebugPane::onPaintOverlay(const ui::PaintContext& ctx) {
    // Les listes vides disent quoi faire, par-dessus la table (apres elle).
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto note = [&](const ui::TableView& t, const std::string& message) {
        const auto b = t.bounds();
        if (b.w <= 0.f || b.h < 48.f) return;
        float y = b.y + 36.f;
        for (const auto& line : wrap(r, message, kSmall, b.w - 20.f)) {
            if (y + r.lineHeight(kSmall) > b.bottom() - 2.f) break;
            r.drawText({b.x + 10.f, y}, line, kSmall, c.textMuted);
            y += r.lineHeight(kSmall) + 3.f;
        }
    };
    if (bps_.empty())
        note(*bpTable_, "Aucun point d'arr\xC3\xAAt. Un clic dans la marge du code (ici, ou dans l'onglet d'une section) en pose un ; F9 sur la ligne du curseur aussi.");
    if (watches_.empty())
        note(*watchTable_, "Aucun espion. Double-clic sur un nom du code, ou \xC2\xAB + Espion \xC2\xBB : sa valeur se suit ici, avec le cycle o\xC3\xB9 elle a chang\xC3\xA9.");
}

ui::EventResult SimDebugPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = hitAt(m->pos);
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        const int h = hitAt(d->pos);
        if (h >= 0) {
            const std::string key = hits_[static_cast<std::size_t>(h)].key;    // activate peut redessiner
            activate(key);
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && !k->repeat && shown() && !k->mods.alt) {
        // F5, F10, F11 : l'onglet a l'ecran les prend, sauf si l'on tape ailleurs.
        const auto* focus = focusedWidget();
        if (focus && !isInside(focus)) {
            const auto* field = dynamic_cast<const ui::InputText*>(focus);
            const auto* editor = dynamic_cast<const ui::MultiLineText*>(focus);
            if (field || (editor && !editor->readOnly())) return ui::EventResult::Ignored;
        }
        if (k->key == ui::Key::F5 && !k->mods.ctrl) {
            runAction(k->mods.shift ? AStop : AContinue);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::F10 && !k->mods.shift) {
            runAction(k->mods.ctrl ? ARunToLine : AStepSection);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::F11 && k->mods.none()) {
            runAction(AStepCycle);
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

const ui::Widget* SimDebugPane::focusedWidget() {
    const ui::Widget* root = rootWidget();
    const ui::Widget* found = nullptr;
    const std::function<void(const ui::Widget&)> walk = [&](const ui::Widget& w) {
        if (found) return;
        if (w.focused()) { found = &w; return; }
        for (const auto& child : w.children()) walk(*child);
    };
    if (root) walk(*root);
    return found;
}

bool SimDebugPane::isInside(const ui::Widget* w) const {
    for (; w; w = w->parent())
        if (w == this) return true;
    return false;
}

void SimDebugPane::activate(const std::string& key) {
    auto* h = host();
    if (startsWith(key, "pile:")) {
        (void)chooseLevel(static_cast<std::size_t>(std::strtoul(key.c_str() + 5, nullptr, 10)));
    } else if (key == "etat:aller") {
        if (h && h->lastBreakHit() && hosts_.goToLine)
            hosts_.goToLine(h->lastBreakHit()->section, static_cast<std::uint32_t>(std::max(0, h->lastBreakHit()->line)));
    } else if (key == "ecrit:aller") {
        if (writerEngine_ && hosts_.goToLine) hosts_.goToLine(writerLast_.section, static_cast<std::uint32_t>(std::max(0, writerLast_.line)));
    } else if (startsWith(key, "ecrit:")) {
        const auto i = static_cast<std::size_t>(std::strtoul(key.c_str() + 6, nullptr, 10));
        if (i < writerSites_.size()) (void)showSection(writerSites_[i].section, static_cast<int>(writerSites_[i].line));
    } else if (startsWith(key, "temps:")) {
        const auto i = static_cast<std::size_t>(std::strtoul(key.c_str() + 6, nullptr, 10));
        if (i < bars_.size()) (void)showSection(bars_[i].section);
    } else if (key == "pourquoi:condition") {          // Lot API 8 (2e partie)
        (void)applyProposedCondition();
    } else if (key == "points:ajouter") {
        askBreakpoint();
    } else if (key == "points:tout") {
        clearBreakpoints();
    } else if (key == "espions:ajouter") {
        askWatch();
    } else if (key == "espions:retirer") {
        std::vector<std::string> paths;
        for (const auto r : watchTable_->selectedModelRows())
            if (r < watchRows_.size() && watchRows_[r].depth == 0)
                paths.push_back(watches_[static_cast<std::size_t>(watchRows_[r].watch)].path);
        if (paths.empty() && !watches_.empty()) {
            status("Espions : choisis d'abord celui \xC3\xA0 retirer (ou sa croix).");
            return;
        }
        for (const auto& p : paths) (void)removeWatch(p);
    }
    invalidate();
}

std::string SimDebugPane::stateLine() const { return sd::stateSentence(facts()); }

std::vector<std::string> SimDebugPane::whyLines() const { return sd::whyHere(facts()); }

// ==== Lot API 8 (2e partie) : la condition par clic droit, l'espion tape, « Mettre la condition » ====
namespace {

// Ou chercher un nom court du code d'une section : l'unite ("Unite."), l'instance
// d'une section de bloc, puis tel quel.
std::vector<std::string> namePrefixes(const domain::Project& p, domain::Index index, const std::string& instance) {
    std::vector<std::string> out;
    if (index < p.sections.size()) {
        const auto& s = p.sections[index];
        if (s.owner < p.pous.size()) {
            const auto& pou = p.pous[s.owner];
            if (pou.kind == domain::PouKind::ProgramUnit) out.push_back(text(p, pou.name) + ".");
            else if (pou.kind == domain::PouKind::FunctionBlockType && !instance.empty()) out.push_back(instance + ".");
        }
    }
    out.emplace_back();
    return out;
}

} // namespace

bool SimDebugPane::prepareConditionFor(const domain::Project& p, SimulationHost& host, const std::string& section, int line,
                                       const std::string& instance, ConditionAsk& out, std::string* why) {
    const auto index = sd::findSection(p, section);
    if (index == domain::kNoIndex) {
        if (why) *why = "Condition : section introuvable : " + section + ".";
        return false;
    }
    const auto key = sd::sectionKey(p, index);
    // Le code de la ligne (1 = la premiere).
    const std::string_view body = p.sections[index].body;
    std::string code;
    std::size_t count = 0, start = 0;
    bool found = false;
    for (;;) {
        const auto nl = body.find('\n', start);
        ++count;
        if (static_cast<int>(count) == line) {
            code = std::string(body.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start));
            found = true;
        }
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    if (line < 1 || !found) {
        if (why) *why = "Condition : " + key + " n'a que " + std::to_string(count) + " lignes.";
        return false;
    }
    out = ConditionAsk{};
    // Le point de cette ligne ; aucun : il est pose d'abord (la maquette).
    for (const auto& b : host.breakpoints())
        if (!isTransient(b.id) && sd::sameSection(b.section, key) && b.line == line) {
            out.id = b.id;
            out.condition = b.condition;
            break;
        }
    if (out.id == 0) {
        out.id = host.addBreakpoint(key, line, {});
        out.placed = out.id != 0;
        if (out.id == 0) {
            if (why) *why = "Condition : le point d'arr\xC3\xAAt ne se pose pas sur " + key + ", ligne " + std::to_string(line) + ".";
            return false;
        }
    }
    std::size_t rank = 0;
    for (const auto& b : host.breakpoints()) {
        if (isTransient(b.id)) continue;
        ++rank;
        if (b.id == out.id) {
            out.number = rank;
            break;
        }
    }
    out.section = key;
    out.line = line;
    out.title = sd::conditionTitle(out.number, key, line);
    out.code = trim(code);
    out.prefixes = namePrefixes(p, index, instance);
    // Les idees : les valeurs du passage si la simulation est arretee sur cette
    // ligne (SimBreakHit::values), sinon celles de maintenant.
    std::vector<std::pair<std::string, std::string>> values;
    const auto& hit = host.lastBreakHit();
    if (host.state() == SimulationHost::State::Paused && hit && sd::sameSection(hit->section, key) && hit->line == line) {
        values = hit->values;
    } else if (auto* rt = host.runtime()) {
        bool inComment = false;
        for (const auto& name : sd::lineSymbols(code, inComment))
            for (const auto& prefix : out.prefixes) {
                sim::Value v;
                if (rt->get(prefix + name, v)) {
                    values.emplace_back(name, sd::formatValue(v));
                    break;
                }
            }
    }
    out.ideas = sd::conditionIdeas(values);
    return true;
}

std::string SimDebugPane::previewCondition(sim::Runtime* rt, const std::vector<std::string>& prefixes, const std::string& condition) {
    return sd::conditionPreview(rt, prefixes, condition);
}

bool SimDebugPane::prepareCondition(const std::string& section, int line, ConditionAsk& out, std::string* why) {
    auto* h = host();
    const auto p = project();
    if (!h || !p) {
        if (why) *why = "Condition : ouvre d'abord un projet.";
        return false;
    }
    std::string instance;
    if (const auto index = sd::findSection(*p, section); index != domain::kNoIndex && index == codeIndex_) instance = codeInstance_;
    if (!prepareConditionFor(*p, *h, section, line, instance, out, why)) return false;
    if (out.placed)
        journal("point-arret", "Point d'arr\xC3\xAAt pos\xC3\xA9 : " + out.section + ", ligne " + std::to_string(out.line), out.section, out.line);
    bpDirty_ = true;
    invalidate();
    return true;
}

bool SimDebugPane::editCondition(const std::string& section, int line) {
    const auto p = project();
    if (!host() || !p || line < 1) return false;
    if (hosts_.askCondition) {
        // L'ecran pose le point au besoin et ouvre le dialogue (le meme que depuis
        // l'onglet d'une section).
        std::string key = section;
        if (const auto index = sd::findSection(*p, section); index != domain::kNoIndex) key = sd::sectionKey(*p, index);
        hosts_.askCondition(key, line);
        bpDirty_ = true;
        return true;
    }
    ConditionAsk ask;
    std::string why;
    if (prepareCondition(section, line, ask, &why)) return true;
    if (!why.empty()) status(why);
    return false;
}

std::string SimDebugPane::conditionPreview(const std::string& condition, const std::string& section) const {
    const auto p = project();
    std::vector<std::string> prefixes;
    if (p) {
        const auto index = section.empty() ? codeIndex_ : sd::findSection(*p, section);
        prefixes = namePrefixes(*p, index, index == codeIndex_ ? codeInstance_ : std::string{});
    } else {
        prefixes.emplace_back();
    }
    return sd::conditionPreview(runtime(), prefixes, condition);
}

bool SimDebugPane::setConditionById(std::uint32_t id, const std::string& condition) {
    auto* h = host();
    if (!h || id == 0) return false;
    const auto c = trim(condition);
    std::size_t rank = 0;
    for (const auto& b : h->breakpoints()) {
        if (isTransient(b.id)) continue;
        ++rank;
        if (b.id != id) continue;
        if (!h->setBreakpointCondition(id, c)) return false;
        journal("condition", c.empty() ? "Point d'arr\xC3\xAAt " + std::to_string(rank) + " : sans condition"
                                       : "Point d'arr\xC3\xAAt " + std::to_string(rank) + " : seulement si " + c,
                b.section, b.line);
        bpDirty_ = true;
        invalidate();
        return true;
    }
    return false;
}

bool SimDebugPane::removeBreakpointById(std::uint32_t id) {
    auto* h = host();
    if (!h || id == 0) return false;
    for (const auto& b : h->breakpoints()) {
        if (b.id != id || isTransient(b.id)) continue;
        const std::string section = b.section;
        const int line = b.line;
        if (!h->removeBreakpoint(id)) return false;
        const std::string said = "Point d'arr\xC3\xAAt enlev\xC3\xA9 : " + section + ", ligne " + std::to_string(line);
        journal("point-arret", said, section, line);
        status(said + ".");
        bpDirty_ = true;
        invalidate();
        return true;
    }
    return false;
}

std::vector<std::string> SimDebugPane::watchSuggestions(const std::string& typed) const {
    const auto p = project();
    const auto t = trim(typed);
    if (!p || t.empty()) return {};
    std::vector<std::string> names;
    // Un membre (Armoires[0].et) : les enfants du chemin avant le dernier point.
    if (const auto cut = t.find_last_of(".["); cut != std::string::npos && cut > 0) {
        mt::Node node;
        if (locateWatch(*p, t.substr(0, cut), node) && mt::hasChildren(*p, node))
            for (const auto& k : mt::children(*p, node)) names.push_back(k.path);
    }
    // Les globales et les locales des unites (Unite.nom), comme « + Espion ».
    for (const auto& v : p->variables)
        if (v.scope == domain::VariableScope::Global || v.scope == domain::VariableScope::Constant) names.push_back(text(*p, v.name));
    for (const auto& pou : p->pous) {
        if (pou.kind != domain::PouKind::ProgramUnit) continue;
        const auto unit = text(*p, pou.name);
        for (const auto vi : pou.parameters) if (vi < p->variables.size()) names.push_back(unit + "." + text(*p, p->variables[vi].name));
        for (const auto vi : pou.locals) if (vi < p->variables.size()) names.push_back(unit + "." + text(*p, p->variables[vi].name));
    }
    return sd::namesStartingWith(names, t);
}

void SimDebugPane::typeWatch(const std::string& typed) {
    if (!watchField_) return;
    static_cast<WatchField*>(watchField_)->take();
    watchField_->setText(typed);
    if (!trim(typed).empty()) watchField_->openSuggestions();
    invalidate();
}

bool SimDebugPane::submitWatchField(std::string* why) {
    if (!watchField_) return false;
    const auto typed = trim(watchField_->text());
    if (typed.empty()) {
        if (why) *why = "Un espion : tape un nom (Armoires[0].etat), Entr\xC3\xA9" "e l'ajoute.";
        return false;
    }
    for (const auto& w : watches_)
        if (lower(w.path) == lower(typed)) {
            if (why) *why = "D\xC3\xA9j\xC3\xA0 un espion : " + w.path + ".";
            watchField_->setText({});
            return false;
        }
    std::string reason;
    if (!addWatch(typed, &reason)) {
        if (why) *why = sd::unknownName(typed);
        return false;
    }
    watchField_->closeSuggestions();
    watchField_->setText({});
    chooseVariable(watches_.back().path);            // la maquette : le nouvel espion est choisi (Qui a ecrit ?)
    status("Espion ajout\xC3\xA9 : " + watches_.back().path + ".");
    return true;
}

std::string SimDebugPane::proposedCondition() const {
    auto* h = host();
    if (!h || !h->attached() || h->state() != SimulationHost::State::Paused || !h->lastBreakHit()) return {};
    const auto& hit = *h->lastBreakHit();
    for (const auto& b : h->breakpoints())
        if (b.id == hit.id) {
            if (isTransient(b.id) || !trim(b.condition).empty()) return {};
            return sd::suggestedCondition(hit.values);
        }
    return {};      // un pas, une pause : pas de point d'arret
}

bool SimDebugPane::applyProposedCondition() {
    const auto proposal = proposedCondition();
    auto* h = host();
    if (proposal.empty() || !h || !h->lastBreakHit()) {
        status("Mettre la condition : seulement en pause sur un point d'arr\xC3\xAAt sans condition.");
        return false;
    }
    if (!setConditionById(h->lastBreakHit()->id, proposal)) return false;
    status("Point d'arr\xC3\xAAt : il ne s'arr\xC3\xAAtera plus que si " + proposal + ".");
    return true;
}
// ==== fin Lot API 8 (2e partie) ====

} // namespace app
