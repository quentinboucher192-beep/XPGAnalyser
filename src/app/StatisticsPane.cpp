// =============================================================================
//  app/StatisticsPane.cpp - lot API 7 : l'onglet API > Statistiques
// =============================================================================
#include "StatisticsPane.hpp"
#include "../core/Edition.hpp"   // 1.12.0 : XPGAnalyser API - pas d'IHM

#include "ApiPanes.hpp"
#include "ApiListKit.hpp"                  // lot API 8 : la recherche (ApiFilterBar)
#include "hmi/HmiPanels.hpp"

#include "../ui/Icons.hpp"
#include "../ui/widgets/DataViews.hpp"
#include "../ui/widgets/FilterMemory.hpp"  // lot API 8 : les termes surlignes dans les listes

#include <algorithm>
#include <cmath>
#include <map>

namespace app {

namespace st = project::stats;
using ui::RowIndex;

namespace {

const gfx::FontId kCaption{11};
const gfx::FontId kSmall{12};
const gfx::FontId kBody{13};
const gfx::FontId kValue{17};
const gfx::FontId kUnit{15};
const gfx::FontId kBig{28};

// L'etat du calcul, dans la barre : pas une action, un renseignement.
constexpr int kStatusItem = 50;
// Les barres du programme : neuf, puis « N autres » - dix lignes, toujours.
constexpr std::size_t kTopRows = 10;
constexpr std::size_t kTypeRows = 6;
constexpr std::size_t kTodoRows = 6;
// Plus etroit, le bloc du programme pose son choix a trois SOUS son titre (une
// ligne de plus) : a cote, il le recouvrait. Une largeur fixe, pas une mesure :
// le plan (onLayout) et le dessin doivent dire la meme chose.
constexpr float kSegmentsBelow = 500.f;     // titre (~150) + choix (~280) + marges : ~470
constexpr float kSegmentsRow = 30.f;
// Les colonnes de la table des types : a leur largeur pleine, et au plus serre
// (leur titre en entier ; la colonne Part garde sa barre). Type : le reste.
constexpr float kTypeColumns[] = {0.f, 170.f, 110.f, 110.f, 110.f, 110.f, 200.f};
constexpr float kTypeColumnsMin[] = {180.f, 110.f, 96.f, 96.f, 80.f, 86.f, 130.f};

const char* const kDots = "\xE2\x80\xA6";
const char* const kMid = " \xC2\xB7 ";

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// ---------------------------------------------------------------- dessin ----
void drawBold(const ui::PaintContext& ctx, gfx::Point at, const std::string& s, gfx::FontId f, gfx::Color c) {
    ctx.r.drawText(at, s, f, c);
    ctx.r.drawText({at.x + 0.6f, at.y}, s, f, c);
}

float textWidth(const ui::PaintContext& ctx, const std::string& s, gfx::FontId f) { return ctx.r.measure(s, f).width; }

// Le texte coupe a la largeur, termine par ... ; jamais au milieu d'un caractere.
std::string fit(const ui::PaintContext& ctx, const std::string& s, gfx::FontId f, float w) {
    if (w <= 0.f) return {};
    if (textWidth(ctx, s, f) <= w) return s;
    const float dots = textWidth(ctx, kDots, f);
    auto n = ctx.r.fitCharacters(s, f, std::max(0.f, w - dots));
    n = std::min(n, s.size());
    while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) --n;
    return s.substr(0, n) + kDots;
}

void drawRight(const ui::PaintContext& ctx, float right, float y, const std::string& s, gfx::FontId f, gfx::Color c) {
    ctx.r.drawText({right - textWidth(ctx, s, f), y}, s, f, c);
}

// Mot a mot, au plus `maxLines` lignes ; la derniere se termine par ... si le
// reste ne tient pas.
std::vector<std::string> wrap(const ui::PaintContext& ctx, const std::string& text, gfx::FontId f, float width, std::size_t maxLines) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    bool cut = false;
    while (i < text.size()) {
        auto j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        const std::string word = text.substr(i, j - i);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && textWidth(ctx, candidate, f) > width) {
            lines.push_back(line);
            line = word;
            if (lines.size() == maxLines) {
                cut = true;
                break;
            }
        } else {
            line = candidate;
        }
        i = j + 1;
    }
    if (!cut && !line.empty()) {
        if (lines.size() < maxLines) lines.push_back(line);
        else cut = true;
    }
    if (cut && !lines.empty()) lines.back() = fit(ctx, lines.back() + kDots, f, width);
    for (auto& l : lines) l = fit(ctx, l, f, width);
    return lines;
}

void card(const ui::PaintContext& ctx, const gfx::Rect& r, bool hot) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRoundedRect(r, hot ? c.borderStrong : c.border, 8.f);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, c.panelBg, 7.f);
}

// Le titre d'un bloc : une icone et des capitales, en gris. Rend sa largeur.
float caption(const ui::PaintContext& ctx, float x, float y, ui::Icon icon, const std::string& text) {
    const auto col = ctx.theme.color.textMuted;
    ui::drawIcon(ctx.r, icon, {x, y, 13.f, 13.f}, col);
    ctx.r.drawText({x + 19.f, y}, text, kCaption, col);
    return 19.f + textWidth(ctx, text, kCaption);
}

gfx::Rect button(const ui::PaintContext& ctx, float right, float cy, const std::string& label, bool hot) {
    const auto& c = ctx.theme.color;
    const float w = textWidth(ctx, label, kSmall) + 22.f;
    const gfx::Rect r{right - w, cy - 13.f, w, 26.f};
    ctx.r.fillRoundedRect(r, hot ? c.borderStrong : c.border, 5.f);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, hot ? c.rowAltBg : c.panelBg, 4.f);
    ctx.r.drawText({r.x + 11.f, r.y + (r.h - ctx.r.lineHeight(kSmall)) * 0.5f}, label, kSmall, c.text);
    return r;
}

// Une barre empilee : ses parts, dans l'ordre, sur un fond. `scale` : la
// valeur qui remplit la barre entiere.
void stackedBar(const ui::PaintContext& ctx, const gfx::Rect& r, const std::vector<std::pair<double, gfx::Color>>& parts, double scale,
                gfx::Color track) {
    ctx.r.fillRoundedRect(r, track, r.h * 0.5f);
    if (scale <= 0.0) return;
    float x = r.x;
    for (const auto& [value, colour] : parts) {
        if (value <= 0.0) continue;
        const float w = std::min(r.right() - x, static_cast<float>(static_cast<double>(r.w) * value / scale));
        if (w <= 0.f) break;
        ctx.r.fillRect({x, r.y, std::max(1.f, w), r.h}, colour);
        x += w;
    }
}

gfx::Color languageColour(const ui::PaintContext& ctx, st::Language l) {
    const auto& c = ctx.theme.color;
    switch (l) {
        case st::Language::ST:  return c.accent;
        case st::Language::SFC: return c.info;
        case st::Language::FBD: return c.ok;
        case st::Language::LD:  return c.warning;
        case st::Language::IL:  return ctx.theme.brand.family[1];
        case st::Language::Other: break;
    }
    return c.textMuted;
}

// Les genres de usage::Genre, dans son ordre.
gfx::Color genreColour(const ui::PaintContext& ctx, std::size_t genre) {
    const auto& c = ctx.theme.color;
    switch (genre) {
        case 0: return c.accent;         // instances de DFB
        case 1: return c.info;           // blocs standard
        case 2: return c.ok;             // types derives
        case 3: return c.warning;        // situees
        default: return c.borderStrong;  // les autres
    }
}

const char* genreName(std::size_t genre) {
    switch (genre) {
        case 0: return "instances de DFB";
        case 1: return "blocs standard";
        case 2: return "types d\xC3\xA9riv\xC3\xA9s";
        case 3: return "situ\xC3\xA9" "es";
        default: return "\xC3\xA9l\xC3\xA9mentaires";
    }
}

ui::Icon typeIcon(st::TypeGenre g) {
    switch (g) {
        case st::TypeGenre::Derived:    return ui::Icon::DerivedType;
        case st::TypeGenre::Dfb:        return ui::Icon::FunctionBlock;
        case st::TypeGenre::Standard:   return ui::Icon::FunctionBlock;
        case st::TypeGenre::Elementary: return ui::Icon::Variable;
        case st::TypeGenre::Unknown:    break;
    }
    return ui::Icon::Warning;
}

// Ce qu'un clic sur un type ouvre : son onglet (DDT, DFB), sinon les variables
// de ce type (l'onglet Variables, cherche sur son nom).
std::string typeAction(const st::TypeRow& r) {
    switch (r.genre) {
        case st::TypeGenre::Derived: return "types";
        case st::TypeGenre::Dfb:     return "dfb";
        default:                     return "variables:" + r.name;
    }
}

std::string languageSplit(const st::LanguageLines& lines) {
    std::string out;
    for (std::size_t l = 0; l < st::kLanguageCount; ++l) {
        if (!lines[l]) continue;
        if (!out.empty()) out += kMid;
        out += std::string(st::languageName(static_cast<st::Language>(l))) + " " + st::thousands(lines[l]);
    }
    return out;
}

// "412 octets", "1,2 Ko" : dans une phrase.
std::string bytesInText(std::uint64_t b) {
    return b < 1024 ? st::thousands(b) + (b == 1 ? " octet" : " octets") : st::bytesText(b);
}

// Le nombre et son unite, pour une grande valeur de carte : ("46,7", "Ko").
std::pair<std::string, std::string> bytesParts(std::uint64_t b) {
    const auto t = st::bytesText(b);
    const auto sp = t.rfind(' ');
    return sp == std::string::npos ? std::make_pair(t, std::string{}) : std::make_pair(t.substr(0, sp), t.substr(sp + 1));
}

// Une valeur dans l'unite d'une autre (les parts d'un total en Ko) : "21,4" ;
// "< 0,1" plutot que "0,0" pour trois octets.
std::string inUnitOf(std::uint64_t b, std::uint64_t total) {
    if (total < 1024) return st::thousands(b);
    const double div = total < 1024u * 1024u ? 1024.0 : 1024.0 * 1024.0;
    const double v = static_cast<double>(b) / div;
    if (b > 0 && v < 0.05) return "< 0,1";
    return st::decimal(v, 1);
}

// La liste des noms d'un compteur, pour son infobulle.
std::string namesOf(const std::vector<st::NamedVariable>& v, std::size_t max) {
    std::string out;
    for (std::size_t i = 0; i < v.size() && i < max; ++i) {
        out += (i ? ", " : "") + v[i].name;
        if (!v[i].detail.empty()) out += " (" + v[i].detail + ")";
    }
    if (v.size() > max) out += std::string(", ") + kDots;
    return out;
}

} // namespace

// ================================================================ la table ====
class StatisticsPane::TypeModel final : public ui::ITableModel {
public:
    explicit TypeModel(const StatisticsPane& pane) : pane_(pane) {}
    enum Col : std::size_t { CType, CGenre, CDeclared, CInstances, CSize, CTotal, CShare, CCount };

    [[nodiscard]] std::size_t rowCount() const override { return rows().size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Type", "Genre", "D\xC3\xA9" "clar\xC3\xA9" "es", "Instances", "Taille", "Total", "Part"};
        return c < CCount ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= rows().size()) return {};
        const auto& t = rows()[r];
        switch (c) {
            case CType:      return t.name;
            case CGenre:     return std::string(st::typeGenreLabel(t.genre));
            case CDeclared:  return st::thousands(t.declared);
            case CInstances: return st::thousands(t.instances);
            // Une taille incomplete (un type inconnu dedans) : au moins cela.
            case CSize:      return (t.resolved ? std::string{} : std::string("\xE2\x89\xA5 ")) + st::bytesText(t.unitBytes);
            case CTotal:     return st::bytesText(t.totalBytes);
            case CShare:     return st::percentText(t.share);
            default:         return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= rows().size()) return s;
        const auto& t = rows()[r];
        if (c == CType) {
            s.icon = typeIcon(t.genre);
            s.iconTone = t.genre == st::TypeGenre::Derived ? ui::Tone::Ok
                       : t.genre == st::TypeGenre::Dfb     ? ui::Tone::Accent
                       : t.genre == st::TypeGenre::Unknown ? ui::Tone::Warning
                                                           : ui::Tone::Muted;
        } else if (c == CGenre || c == CShare) {
            s.fgTone = ui::Tone::Muted;
        } else if (c == CSize && !t.resolved) {
            s.fgTone = ui::Tone::Warning;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t c) const override {
        const auto& v = rows();
        if (a >= v.size() || b >= v.size()) return a < b;
        const auto& x = v[a];
        const auto& y = v[b];
        switch (c) {
            case CType:      return lowerAscii(x.name) < lowerAscii(y.name);
            case CGenre:     return x.genre != y.genre ? x.genre < y.genre : lowerAscii(x.name) < lowerAscii(y.name);
            case CDeclared:  return x.declared < y.declared;
            case CInstances: return x.instances < y.instances;
            case CSize:      return x.unitBytes < y.unitBytes;
            default:         return x.totalBytes < y.totalBytes;    // Total, Part
        }
    }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= rows().size()) return {};
        const auto& t = rows()[r];
        std::string out = t.name + " : " + st::thousands(t.instances) + (t.instances == 1 ? " instance de " : " instances de ")
                        + bytesInText(t.unitBytes);
        if (!t.holders.empty()) {
            out += ", dans ";
            for (std::size_t i = 0; i < t.holders.size(); ++i)
                out += (i ? ", " : "") + t.holders[i].first + (t.holders[i].second > 1 ? " (" + st::thousands(t.holders[i].second) + ")" : "");
            if (t.declared > t.holders.size()) out += std::string(", ") + kDots;
        }
        if (!t.resolved) out += ". Un type inconnu dedans compte 0 octet : c'est un minimum";
        out += t.genre == st::TypeGenre::Derived ? ". Double-clic : l'onglet des types d\xC3\xA9riv\xC3\xA9s."
             : t.genre == st::TypeGenre::Dfb     ? ". Double-clic : l'onglet des blocs DFB."
                                                 : ". Double-clic : les variables de ce type.";
        return out;
    }

private:
    [[nodiscard]] const std::vector<st::TypeRow>& rows() const noexcept { return pane_.stats_.memory.types; }
    const StatisticsPane& pane_;
};

// La molette entre deux defilements. Une TableView la prend des qu'elle est
// dessous, meme sans rien a faire defiler : en descendant l'onglet, la table
// arrivee sous la souris l'arretait net. Vers le bas, l'onglet defile d'abord
// (jusqu'a montrer la table entiere), puis la table ; vers le haut, la table
// d'abord (jusqu'a sa premiere ligne), puis l'onglet. Maj+molette : la table
// defile en largeur, comme partout.
class StatisticsPane::TypeTable final : public ui::TableView {
public:
    TypeTable(std::string id, StatisticsPane& pane) : ui::TableView(std::move(id)), pane_(pane) {}

protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && !w->mods.shift && bounds().contains(w->pos)) {
            gfx::Rect first;
            const bool atTop = visibleRowCount() == 0 || rowRect(0, first);
            const bool pageFirst = w->dy < 0.f ? pane_.scrollY_ < pane_.maxScroll() - 0.5f : atTop;
            // Ignoree ici, elle remonte a l'onglet (Widget::dispatch : les
            // enfants d'abord, puis le parent).
            if (pageFirst) return ui::EventResult::Ignored;
        }
        return ui::TableView::onEvent(ev);
    }

private:
    const StatisticsPane& pane_;
};

// ================================================================ le volet ====
StatisticsPane::StatisticsPane(std::string id) : ui::Widget(std::move(id)) {
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<TypeTable>(this->id() + ".types", *this)));
    model_ = std::make_shared<TypeModel>(*this);
    table_->setModel(model_);
    setTypeColumns(0.f);
    table_->setSelectionMode(ui::SelectionMode::Single);
    table_->sortBy(TypeModel::CTotal, ui::SortOrder::Descending);
    table_->setTooltip("Tous les types d\xC3\xA9" "clar\xC3\xA9s, le plus lourd d'abord. Un clic sur un titre trie ; double-clic sur une ligne : "
                       "le type (ou ses variables). Ctrl+C copie les lignes choisies.");
    links_ += table_->activated->connect([this](RowIndex row) { openType(static_cast<std::size_t>(row)); });
    // ---- Lot API 8 : chercher (la table des types, les barres du programme, la memoire) ----
    //  Ajoutee apres la table : dessinee par-dessus quand la table defile dessous.
    filters_ = &static_cast<ApiFilterBar&>(addChild(std::make_unique<ApiFilterBar>(
        this->id() + ".filtres", "Rechercher : type, section, unit\xC3\xA9, variable\xE2\x80\xA6")));
    links_ += filters_->changed->connect([this] { applySearch(); });
    filters_->recall();
    // ---- fin Lot API 8 ----
}

StatisticsPane::~StatisticsPane() = default;

// ---- Lot API 8 : chercher ----
gfx::Rect StatisticsPane::area() const {
    const auto b = bounds();
    const float h = filters_ && filters_->visible() ? 44.f : 0.f;
    return {b.x, b.y + h, b.w, std::max(0.f, b.h - h)};
}

bool StatisticsPane::typeKept(const st::TypeRow& t) const {
    if (query_.empty()) return true;
    // Le nom, le genre, les variables qui le portent (un nom de variable trouve son type).
    std::vector<std::string> texts{t.name, std::string(st::typeGenreLabel(t.genre))};
    for (const auto& h : t.holders) texts.push_back(h.first);
    return query_.matches(texts);
}

void StatisticsPane::applySearch() {
    query_ = ui::SearchQuery(filters_ ? filters_->search() : std::string{});
    ui::FilterChain chain;
    if (!query_.empty())
        chain.addPredicate("recherche", [this](RowIndex r) { return r < stats_.memory.types.size() && typeKept(stats_.memory.types[r]); });
    table_->setFilter(std::move(chain));
    table_->setHighlight(query_.text());
    if (filters_) filters_->setCount(table_->visibleRowCount(), stats_.memory.types.size());
    hover_ = pressed_ = -1;         // les zones cliquables se refont au dessin
    invalidate();
}

std::size_t StatisticsPane::shownTypes() const { return table_->visibleRowCount(); }
// ---- fin Lot API 8 ----

void StatisticsPane::setHosts(ApiPaneHosts h) {
    hosts_ = std::move(h);
    refresh();
}

void StatisticsPane::attach(ApiFrame& frame) {
    frame_ = &frame;
    auto& t = frame.tools();
    t.add(ARecompute, HmiGlyph::Refresh,
          "Tout recalculer sur le projet ouvert (c'est d\xC3\xA9j\xC3\xA0 fait apr\xC3\xA8s chaque modification)", "Recalculer");
    t.add(AExport, HmiGlyph::Export,
          "Exporter en CSV les chiffres, les entr\xC3\xA9" "es, les sections et les types (s\xC3\xA9parateur ;), dans le dossier du projet",
          "Exporter (CSV)\xE2\x80\xA6");
    t.separator();
    t.add(AVariables, HmiGlyph::SystemVars, "L'onglet Variables : chercher, filtrer, commenter", "Ouvrir les variables");
    t.add(AOrder, HmiGlyph::List, "L'ordre d'ex\xC3\xA9" "cution de MAST : ranger les entr\xC3\xA9" "es, v\xC3\xA9rifier les lectures",
          "Ouvrir l'ordre d'ex\xC3\xA9" "cution");
    t.separator();
    t.add(AHelp, HmiGlyph::Help, "L'aide (F1)", "Aide (F1)");
    // A droite des actions : un renseignement, pas un bouton (grise). Son
    // libelle tombe le premier quand la barre est etroite.
    t.add(kStatusItem, HmiGlyph::Clock, {}, {});
    t.setEnabledWhen(kStatusItem, [] { return false; });
    t.setEnabledWhen(AExport, [this] { return stats_.valid; });
    links_ += t.triggered->connect([this](int a) { runAction(a); });
    frame.setHint("Ctrl+5 ouvre cet onglet \xC2\xB7 tout se recalcule apr\xC3\xA8s chaque modification \xC2\xB7 "
                  "un clic sur une barre ouvre la section ou le type");
    updateStatus();
}

void StatisticsPane::updateStatus() {
    if (!frame_) return;
    if (stats_.valid)
        frame_->tools().setText(kStatusItem,
                                "Calcul\xC3\xA9 sur le projet ouvert en " + st::decimal(stats_.milliseconds, 1)
                                    + " ms, refait apr\xC3\xA8s chaque modification (Recalculer : tout de suite)",
                                "calcul\xC3\xA9 sur le projet ouvert \xC2\xB7 \xC3\xA0 jour");
    else
        frame_->tools().setText(kStatusItem, "Aucun projet ouvert : rien \xC3\xA0 compter", "pas de projet ouvert");
}

void StatisticsPane::refresh() {
    // Le type choisi dans la table le reste apres le calcul - par son nom : les
    // lignes changent d'ordre et de nombre, une ligne gardee par son rang
    // designerait un autre type.
    std::string keep;
    if (const auto sel = table_->selectedModelRows(); !sel.empty() && sel.front() < stats_.memory.types.size())
        keep = stats_.memory.types[sel.front()].name;
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (!p) {
        stats_ = {};
    } else {
        // L'IHM et la bibliotheque, tels que l'ecran les connait (nuls : absents).
        std::map<std::string, st::ReadInfo> reads;
        if (hosts_.hmiReads)
            for (const auto& [name, r] : hosts_.hmiReads()) reads.emplace(name, st::ReadInfo{r.via, r.uses});
        const auto library = hosts_.library ? hosts_.library() : nullptr;
        stats_ = st::compute(*p, library.get(), &reads);
    }
    model_->modelReset->emit();
    std::vector<RowIndex> again;
    for (std::size_t i = 0; i < stats_.memory.types.size() && !keep.empty(); ++i)
        if (stats_.memory.types[i].name == keep) {
            again.push_back(static_cast<RowIndex>(i));
            break;
        }
    table_->selectModelRows(std::move(again), /*notify=*/false);
    // Les zones cliquables se refont au dessin suivant : un survol ou un appui
    // retenu par son rang designerait peut-etre une autre zone.
    hover_ = pressed_ = -1;
    updateStatus();
    table_->setVisibility(stats_.valid ? ui::Visibility::Visible : ui::Visibility::Hidden);
    // Lot API 8 : la recherche (la table l'a reappliquee a ses lignes relues), son compte.
    if (filters_) {
        filters_->setVisibility(stats_.valid ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        filters_->setCount(table_->visibleRowCount(), stats_.memory.types.size());
    }
    invalidateLayout();
    invalidate();
}

void StatisticsPane::runAction(int action) {
    switch (action) {
        case ARecompute:
            refresh();
            if (hosts_.status && stats_.valid)
                hosts_.status("Statistiques recalcul\xC3\xA9" "es en " + st::decimal(stats_.milliseconds, 1) + " ms.");
            return;
        case AExport:
            // La cle "csv:<nom>" : l'ecran ecrit exports/<nom>.csv (avec le BOM).
            if (stats_.valid && hosts_.request) hosts_.request("csv:statistiques\n" + csv());
            return;
        case AVariables: if (hosts_.request) hosts_.request("variables"); return;
        case AOrder:     if (hosts_.request) hosts_.request("ordre"); return;
        case AHelp:      if (hosts_.request) hosts_.request("aide"); return;
        default: return;
    }
}

bool StatisticsPane::chooseBreakdown(std::string_view label) {
    std::string l = lowerAscii(label);
    while (!l.empty() && l.front() == ' ') l.erase(l.begin());
    if (l.rfind("par ", 0) == 0) l.erase(0, 4);
    Breakdown b{};
    if (l.rfind("entr", 0) == 0) b = Breakdown::Entries;
    else if (l.rfind("sec", 0) == 0) b = Breakdown::Sections;
    else if (l.rfind("lang", 0) == 0) b = Breakdown::Languages;
    else return false;
    if (b != breakdown_) {
        breakdown_ = b;
        invalidate();
    }
    return true;
}

const st::ProjectStats& StatisticsPane::stats() const noexcept { return stats_; }

std::string StatisticsPane::csv() const { return st::toCsv(stats_); }

gfx::Rect StatisticsPane::partRect(std::string_view key) const {
    for (const auto& h : hits_)
        if (h.key == key) return h.rect;
    return {};
}

std::string StatisticsPane::requestOf(std::string_view key) const {
    for (const auto& h : hits_)
        if (h.key == key) return h.action.rfind("seg:", 0) == 0 || h.action.rfind("scroll:", 0) == 0 ? std::string{} : h.action;
    return {};
}

float StatisticsPane::maxScroll() const { return std::max(0.f, contentH_ - area().h); }     // lot API 8 : sous la barre

void StatisticsPane::scrollTo(float y) {
    const float clamped = std::clamp(y, 0.f, maxScroll());
    if (clamped == scrollY_) return;
    scrollY_ = clamped;
    invalidateLayout();
    invalidate();
}

std::size_t StatisticsPane::todoRows() const noexcept { return std::min(stats_.findings.size(), kTodoRows); }

// ---------------------------------------------------------------- le plan ----
//  Des hauteurs fixes (les barres, les compteurs) : le plan se calcule sans
//  rien mesurer, dans onLayout comme dans onPaint, et reste le meme d'un dessin
//  a l'autre. Large : quatre cartes, deux colonnes ; etroit : deux cartes par
//  ligne, une colonne. La table prend le reste de la hauteur (six lignes au
//  moins, pas plus qu'il n'en faut) ; au-dela, le contenu defile.
StatisticsPane::Plan StatisticsPane::plan(float width, float viewHeight) const {
    Plan pl;
    const float inner = std::max(240.f, width - 2.f * pl.pad);
    pl.kpiColumns = inner >= 960.f ? 4 : 2;
    const auto cols = static_cast<float>(pl.kpiColumns);
    const float cardW = (inner - pl.gap * (cols - 1.f)) / cols;
    const float kpiH = 96.f;
    float y = pl.pad;
    for (int i = 0; i < 4; ++i) {
        const auto col = static_cast<float>(i % pl.kpiColumns);
        const auto row = static_cast<float>(i / pl.kpiColumns);
        pl.kpi[i] = {pl.pad + col * (cardW + pl.gap), y + row * (kpiH + pl.gap), cardW, kpiH};
    }
    y += static_cast<float>(4 / pl.kpiColumns) * (kpiH + pl.gap);

    pl.twoColumns = inner >= 900.f;
    const float leftW = pl.twoColumns ? std::floor(inner * 0.53f) : inner;
    const float rightW = pl.twoColumns ? inner - leftW - pl.gap : inner;
    const float rightX = pl.twoColumns ? pl.pad + leftW + pl.gap : pl.pad;
    // Le bloc du programme, etroit : son choix a trois passe sous le titre.
    const float row1H = 252.f + (leftW < kSegmentsBelow ? kSegmentsRow : 0.f);
    const float memH = 222.f;
    const float todoH = 46.f + static_cast<float>(std::max<std::size_t>(todoRows(), 1)) * 52.f + 4.f;
    if (pl.twoColumns) {
        pl.program = {pl.pad, y, leftW, row1H};
        pl.variables = {rightX, y, rightW, row1H};
        y += row1H + pl.gap;
        const float row2H = std::max(memH, todoH);
        pl.memory = {pl.pad, y, leftW, row2H};
        pl.todo = {rightX, y, rightW, row2H};
        y += row2H + pl.gap;
    } else {
        pl.program = {pl.pad, y, inner, row1H};
        y += row1H + pl.gap;
        pl.variables = {pl.pad, y, inner, row1H};
        y += row1H + pl.gap;
        pl.memory = {pl.pad, y, inner, memH};
        y += memH + pl.gap;
        pl.todo = {pl.pad, y, inner, todoH};
        y += todoH + pl.gap;
    }
    const float rowH = 24.f, headerH = 28.f;
    const float minTable = headerH + 6.f * rowH + 2.f;
    const float fullTable = headerH + static_cast<float>(std::max<std::size_t>(stats_.memory.types.size(), 1)) * rowH + 2.f;
    const float tableH = std::clamp(viewHeight - y - pl.pad, minTable, std::max(minTable, fullTable));
    pl.table = {pl.pad, y, inner, tableH};
    pl.height = y + tableH + pl.pad;
    return pl;
}

void StatisticsPane::onLayout() {
    // Lot API 8 : la barre de recherche en haut, fixe ; le reste defile dessous.
    if (filters_) filters_->setBounds({bounds().x, bounds().y, bounds().w, filters_->visible() ? 44.f : 0.f});
    const auto b = area();
    const auto pl = plan(b.w, b.h);
    contentH_ = pl.height;
    scrollY_ = std::clamp(scrollY_, 0.f, maxScroll());
    table_->setBounds({b.x + pl.table.x, b.y + pl.table.y - scrollY_, pl.table.w, pl.table.h});
    // Seulement quand la largeur change : defiler ne rend pas a une colonne la
    // largeur qu'on vient de lui donner a la main.
    if (std::fabs(pl.table.w - columnsFor_) < 0.5f) return;
    columnsFor_ = pl.table.w;
    setTypeColumns(pl.table.w);
}

// La colonne Type prend la largeur qui reste : la table remplit sa carte.
// Etroite, les autres se resserrent (jusqu'a kTypeColumnsMin) avant que la
// table ne defile en largeur - la colonne Part et ses barres restaient hors de
// vue. 0 : les largeurs pleines (avant le premier placement).
void StatisticsPane::setTypeColumns(float tableWidth) {
    std::vector<ui::TableView::Column> cols(TypeModel::CCount);
    float full = 0.f, least = 0.f;
    for (std::size_t i = 1; i < cols.size(); ++i) {
        full += kTypeColumns[i];
        least += kTypeColumnsMin[i];
    }
    const float room = tableWidth - 4.f;
    const float t = tableWidth > 0.f ? std::clamp((room - kTypeColumnsMin[0] - least) / (full - least), 0.f, 1.f) : 1.f;
    float used = 0.f;
    for (std::size_t i = 1; i < cols.size(); ++i) {
        cols[i].width = std::floor(kTypeColumnsMin[i] + (kTypeColumns[i] - kTypeColumnsMin[i]) * t);
        used += cols[i].width;
    }
    cols[0].width = tableWidth > 0.f ? std::max(kTypeColumnsMin[0], room - used) : 260.f;
    for (std::size_t i = 0; i < cols.size(); ++i) {
        cols[i].title = model_->headerText(i);
        if (i >= TypeModel::CDeclared) cols[i].align = ui::Align::End;
    }
    table_->setColumns(std::move(cols));
}

// --------------------------------------------------------------- le dessin ----
void StatisticsPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = area();      // lot API 8 : sous la barre de recherche
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.windowBg);
    hits_.clear();
    if (!hovered()) {
        hover_ = -1;
        dragThumb_ = false;
    }
    if (!stats_.valid) {
        const float w = std::min(620.f, b.w - 40.f);
        const float x = b.x + (b.w - w) * 0.5f;
        const float y = b.y + std::max(24.f, b.h * 0.22f);
        ui::drawIcon(ctx.r, ui::Icon::Chart, {x, y, 28.f, 28.f}, c.textMuted);
        drawBold(ctx, {x + 44.f, y}, "Pas de projet ouvert", gfx::FontId{18}, c.text);
        const auto lines = wrap(ctx, "Ouvre un projet, depuis son dossier ou un export .XPG : ses statistiques s'affichent ici, "
                                     "recalcul\xC3\xA9" "es apr\xC3\xA8s chaque modification.", kBody, w - 44.f, 3);
        for (std::size_t k = 0; k < lines.size(); ++k)
            ctx.r.drawText({x + 44.f, y + 30.f + static_cast<float>(k) * 18.f}, lines[k], kBody, c.textMuted);
        return;
    }
    const auto pl = plan(b.w, b.h);
    const auto at = [&](const gfx::Rect& r) { return gfx::Rect{b.x + r.x, b.y + r.y - scrollY_, r.w, r.h}; };
    ctx.r.pushClip(b);
    paintCards(ctx, pl, b.y - scrollY_);
    paintProgram(ctx, at(pl.program));
    paintVariables(ctx, at(pl.variables));
    paintMemory(ctx, at(pl.memory));
    paintTodo(ctx, at(pl.todo));
    // Sous la table : sa carte (la table se dessine par-dessus, un pixel en retrait).
    const auto t = at(pl.table);
    ctx.r.fillRoundedRect({t.x - 1.f, t.y - 1.f, t.w + 2.f, t.h + 2.f}, c.border, 6.f);
    ctx.r.popClip();
}

// Les parts de la table (une petite barre devant le %), et l'ascenseur : par-dessus.
void StatisticsPane::onPaintOverlay(const ui::PaintContext& ctx) {
    if (!stats_.valid) return;
    const auto b = area();      // lot API 8
    const auto& c = ctx.theme.color;
    if (table_->visible()) {
        double top = 0.0;
        for (const auto& r : stats_.memory.types) top = std::max(top, r.share);
        const auto tb = table_->bounds().intersect(b);
        if (top > 0.0 && !tb.empty()) {
            ctx.r.pushClip(tb);
            const auto& rows = stats_.memory.types;
            const float textW = textWidth(ctx, "100 %", ctx.theme.font.ui);
            for (std::size_t i = 0; i < table_->visibleRowCount(); ++i) {
                gfx::Rect cell;
                if (!table_->cellRect(i, TypeModel::CShare, cell)) continue;
                const auto row = table_->viewRow(i);
                if (row >= rows.size()) continue;
                const float room = cell.w - textW - 26.f;
                if (room < 12.f) continue;
                const float w = std::max(2.f, room * static_cast<float>(rows[row].share / top));
                ctx.r.fillRoundedRect({cell.x + 8.f, cell.y + cell.h * 0.5f - 3.f, w, 6.f}, c.accent, 3.f);
            }
            ctx.r.popClip();
        }
    }
    const auto thumb = thumbRect();
    if (thumb.w > 0.f) ctx.r.fillRoundedRect(thumb, dragThumb_ ? c.scrollbarHover : c.scrollbar, 3.f);
}

gfx::Rect StatisticsPane::thumbRect() const {
    const auto b = area();      // lot API 8
    const float most = maxScroll();
    if (most <= 0.f || contentH_ <= 0.f) return {};
    const float h = std::max(28.f, b.h * b.h / contentH_);
    const float y = b.y + (b.h - h) * (scrollY_ / most);
    return {b.right() - 9.f, y, 6.f, h};
}

// --------------------------------------------------------------- les cartes ----
void StatisticsPane::paintCards(const ui::PaintContext& ctx, const Plan& pl, float top) {
    const auto& c = ctx.theme.color;
    const auto& s = stats_;
    const auto& code = s.code;
    const auto& vs = s.variables;
    const auto& mem = s.memory;
    const auto b = area();      // lot API 8 (seul son x compte ici)

    // Un morceau d'une ligne de carte : « globales » (lead), « 21,4 » (gras),
    // « Ko » (rest). Dix pixels entre deux morceaux.
    struct Seg {
        std::string lead, bold, rest;
        bool warn{false};
    };
    struct Card {
        std::string key, title, big, unit, action, tip;
        ui::Icon icon{ui::Icon::None};
        std::vector<Seg> line1, line2;
        double meter{-1.0};
    };
    std::vector<Card> cards(4);

    auto& lines = cards[0];
    lines.key = "carte:lignes";
    lines.title = "LIGNES DE CODE";
    lines.icon = ui::Icon::Code;
    lines.big = st::thousands(code.lines);
    lines.line1 = {{"", st::thousands(code.entries.size()), code.entries.size() == 1 ? " entr\xC3\xA9" "e de " + code.task : " entr\xC3\xA9" "es de " + code.task},
                   {"", st::thousands(code.taskSections), code.taskSections == 1 ? " section" : " sections"}};
    lines.line2 = {{"", st::thousands(code.units), (code.units == 1 ? " unit\xC3\xA9 (" : " unit\xC3\xA9s (") + st::thousands(code.unitSections)
                                                       + (code.unitSections == 1 ? " section)" : " sections)")}};
    if (code.dfbLines) lines.line2.push_back({"+ ", st::thousands(code.dfbLines), " dans les DFB"});
    lines.action = "ordre";
    lines.tip = "Les lignes des sections que " + code.task + " ex\xC3\xA9" "cute, les unit\xC3\xA9s en bloc (celles du texte, tel que l'export l'\xC3\xA9" "crit).";
    if (code.dfbLines || code.subroutineLines || code.otherLines) {
        lines.tip += " Hors de " + code.task + " :";
        if (code.dfbLines) lines.tip += " " + st::thousands(code.dfbLines) + " lignes dans " + st::thousands(code.dfbs) + (code.dfbs == 1 ? " bloc DFB" : " blocs DFB");
        if (code.subroutineLines) lines.tip += " " + st::thousands(code.subroutineLines) + " dans les sous-routines";
        if (code.otherLines) lines.tip += " " + st::thousands(code.otherLines) + " ailleurs";
        lines.tip += ".";
    }
    lines.tip += " Un clic : l'ordre d'ex\xC3\xA9" "cution.";

    auto& vars = cards[1];
    vars.key = "carte:variables";
    vars.title = "VARIABLES";
    vars.icon = ui::Icon::Variable;
    vars.big = st::thousands(vs.total());
    vars.line1 = {{"", st::thousands(vs.count[0]), vs.count[0] == 1 ? " globale" : " globales"},
                  {"", st::thousands(vs.count[1]), vs.count[1] == 1 ? " locale" : " locales"},
                  {"", st::thousands(vs.count[2]), vs.count[2] == 1 ? " param\xC3\xA8tre" : " param\xC3\xA8tres"}};
    vars.line2 = {{"dont ", st::thousands(vs.located), vs.located == 1 ? " situ\xC3\xA9" "e" : " situ\xC3\xA9" "es"}};
    vars.action = "variables";
    vars.tip = "Les globales, les locales et les param\xC3\xA8tres des unit\xC3\xA9s et des DFB. Les champs des types d\xC3\xA9riv\xC3\xA9s comptent dans "
               "leur type. Un clic : l'onglet Variables.";

    auto& memory = cards[2];
    memory.key = "carte:memoire";
    memory.title = "M\xC3\x89MOIRE D\xC3\x89" "CLAR\xC3\x89" "E";
    memory.icon = ui::Icon::Layers;
    const auto parts = bytesParts(mem.total);
    memory.big = parts.first;
    memory.unit = parts.second;
    memory.line1 = {{"globales ", inUnitOf(mem.byScope[0], mem.total), ""},
                    {"locales ", inUnitOf(mem.byScope[1], mem.total), ""},
                    {"param\xC3\xA8tres ", inUnitOf(mem.byScope[2], mem.total), parts.second.empty() ? std::string{} : " " + parts.second}};
    if (!mem.unresolvedTypes.empty())
        memory.line2 = {{"", st::thousands(mem.unresolvedTypes.size()),
                         mem.unresolvedTypes.size() == 1 ? " type inconnu, compt\xC3\xA9 0 octet" : " types inconnus, compt\xC3\xA9s 0 octet", true}};
    else
        memory.line2 = {{"un mod\xC3\xA8le des d\xC3\xA9" "clarations", "", ""}};
    memory.action = "scroll:table";
    memory.tip = "La taille des d\xC3\xA9" "clarations : BOOL 1 octet, INT 2, DINT et REAL 4, STRING[n] n + 1 ; un DDT, la somme de ses champs ; "
                 "une instance de DFB, ses variables. Sans bourrage : un mod\xC3\xA8le, pas le plan m\xC3\xA9moire de l'automate. Un clic : la table des types.";

    auto& comments = cards[3];
    comments.key = "carte:commentees";
    comments.title = "COMMENT\xC3\x89" "ES";
    comments.icon = ui::Icon::Document;
    const double share = vs.commentedShare();
    comments.big = std::to_string(static_cast<long long>(std::lround(share * 100.0)));
    comments.unit = "%";
    // Une seule ligne : la jauge passe dessous.
    comments.line1 = {{"", st::thousands(vs.commented), " sur " + st::thousands(vs.total())}};
    if (vs.globalsWithoutComment)
        comments.line1.push_back({"", "", st::thousands(vs.globalsWithoutComment)
                                              + (vs.globalsWithoutComment == 1 ? " globale sans commentaire" : " globales sans commentaire"), true});
    comments.meter = share;
    comments.action = "variables";
    comments.tip = "Les variables qui portent un commentaire. Un export .XPG ne porte que ceux des champs de DDT ; Coller depuis Excel (onglet "
                   "Variables) les commente d'un coup.";

    for (std::size_t i = 0; i < cards.size(); ++i) {
        const auto& cd = cards[i];
        const gfx::Rect r{b.x + pl.kpi[i].x, top + pl.kpi[i].y, pl.kpi[i].w, pl.kpi[i].h};
        const bool hot = hover_ == static_cast<int>(hits_.size());
        hits_.push_back({r, cd.key, cd.action, cd.tip, false});
        card(ctx, r, hot);
        caption(ctx, r.x + 14.f, r.y + 12.f, cd.icon, cd.title);
        const auto big = fit(ctx, cd.big, kBig, r.w - 60.f);
        drawBold(ctx, {r.x + 14.f, r.y + 28.f}, big, kBig, c.text);
        if (!cd.unit.empty())
            ctx.r.drawText({r.x + 20.f + textWidth(ctx, big, kBig), r.y + 28.f + ctx.r.lineHeight(kBig) - ctx.r.lineHeight(kUnit) - 3.f}, cd.unit,
                           kUnit, c.textMuted);
        const auto segments = [&](const std::vector<Seg>& segs, float y) {
            float x = r.x + 14.f;
            const float right = r.right() - 12.f;
            for (std::size_t k = 0; k < segs.size(); ++k) {
                const auto& sg = segs[k];
                const auto muted = sg.warn ? ctx.theme.onSurface(c.warning) : c.textMuted;
                if (k) x += 10.f;
                if (!sg.lead.empty()) {
                    const auto t = fit(ctx, sg.lead, kSmall, right - x);
                    ctx.r.drawText({x, y}, t, kSmall, muted);
                    x += textWidth(ctx, t, kSmall);
                }
                if (!sg.bold.empty()) {
                    const float w = textWidth(ctx, sg.bold, kSmall);
                    if (x + w > right) return;
                    drawBold(ctx, {x, y}, sg.bold, kSmall, sg.warn ? muted : c.text);
                    x += w + 0.6f;
                }
                if (!sg.rest.empty()) {
                    const auto t = fit(ctx, sg.rest, kSmall, right - x);
                    ctx.r.drawText({x, y}, t, kSmall, muted);
                    x += textWidth(ctx, t, kSmall);
                }
                if (x >= right) return;
            }
        };
        segments(cd.line1, r.y + 64.f);
        segments(cd.line2, r.y + 79.f);
        if (cd.meter >= 0.0) {
            const gfx::Rect m{r.x + 14.f, r.bottom() - 9.f, r.w - 28.f, 4.f};
            ctx.r.fillRoundedRect(m, c.border, 2.f);
            const float w = static_cast<float>(static_cast<double>(m.w) * std::clamp(cd.meter, 0.0, 1.0));
            if (w > 0.f) ctx.r.fillRoundedRect({m.x, m.y, std::max(4.f, w), m.h}, ctx.theme.coverage(static_cast<float>(cd.meter)), 2.f);
        }
    }
}

// ------------------------------------------------------- le programme ----
void StatisticsPane::paintProgram(const ui::PaintContext& ctx, const gfx::Rect& r) {
    const auto& c = ctx.theme.color;
    const auto& code = stats_.code;
    card(ctx, r, false);
    const float capW = caption(ctx, r.x + 14.f, r.y + 14.f, ui::Icon::Chart, "LE PROGRAMME, EN LIGNES");

    // Le choix a trois, a droite du titre ; etroit, dessous (le plan a prevu la
    // ligne : kSegmentsBelow).
    const std::string labels[3] = {"par entr\xC3\xA9" "e de " + code.task, "par section", "par langage"};
    const char* const keys[3] = {"entrees", "sections", "langages"};
    float segW[3];
    float total = 0.f;
    for (int i = 0; i < 3; ++i) {
        segW[i] = textWidth(ctx, labels[i], kSmall) + 18.f;
        total += segW[i];
    }
    const bool below = r.w < kSegmentsBelow;
    const gfx::Rect seg = below ? gfx::Rect{r.x + 12.f, r.y + 36.f, total + 4.f, 24.f}
                                : gfx::Rect{r.right() - 12.f - total - 4.f, r.y + 9.f, total + 4.f, 24.f};
    ctx.r.fillRoundedRect(seg, c.border, 6.f);
    ctx.r.fillRoundedRect({seg.x + 1.f, seg.y + 1.f, seg.w - 2.f, seg.h - 2.f}, c.windowBg, 5.f);
    float sx = seg.x + 2.f;
    for (int i = 0; i < 3; ++i) {
        const gfx::Rect cell{sx, seg.y + 2.f, segW[i], seg.h - 4.f};
        const bool on = static_cast<int>(breakdown_) == i;
        const bool hot = hover_ == static_cast<int>(hits_.size());
        hits_.push_back({cell, keys[i], "seg:" + std::to_string(i), "Les lignes " + labels[i], false});
        if (on) ctx.r.fillRoundedRect(cell, c.selectionBg, 4.f);
        else if (hot) ctx.r.fillRoundedRect(cell, ctx.theme.brand.hover, 4.f);
        ctx.r.drawText({cell.x + 9.f, cell.y + (cell.h - ctx.r.lineHeight(kSmall)) * 0.5f}, labels[i], kSmall, on ? c.selectionText : c.textMuted);
        sx += segW[i];
    }

    // La legende des langages presents, apres le titre (si la place le permet).
    if (breakdown_ != Breakdown::Languages) {
        float lx = r.x + 14.f + capW + 14.f;
        const float legendEnd = below ? r.right() - 12.f : seg.x - 8.f;
        for (std::size_t l = 0; l < st::kLanguageCount; ++l) {
            if (!code.byLanguage[l]) continue;
            const std::string name(st::languageName(static_cast<st::Language>(l)));
            const float w = 13.f + textWidth(ctx, name, kSmall);
            if (lx + w > legendEnd) break;
            ctx.r.fillRoundedRect({lx, r.y + 16.f, 9.f, 9.f}, languageColour(ctx, static_cast<st::Language>(l)), 2.f);
            ctx.r.drawText({lx + 13.f, r.y + 14.f}, name, kSmall, c.textMuted);
            lx += w + 12.f;
        }
    }

    // Les lignes du graphique.
    struct Row {
        std::string label, action, tip, key;
        ui::Icon icon{ui::Icon::Section};
        st::LanguageLines parts{};
        std::size_t total{0};
        bool others{false};     // « N autres » : un cumul, pas une entree
    };
    std::vector<Row> rows;
    std::string note;
    if (breakdown_ == Breakdown::Entries) {
        std::vector<const st::EntryLines*> order;
        for (const auto& e : code.entries)      // lot API 8 : celles que la recherche garde
            if (query_.empty() || query_.matches({e.name, e.unit ? "unit\xC3\xA9" : "section"})) order.push_back(&e);
        std::stable_sort(order.begin(), order.end(), [](const st::EntryLines* a, const st::EntryLines* z) { return a->lines > z->lines; });
        const std::size_t shown = order.size() > kTopRows ? kTopRows - 1 : order.size();
        for (std::size_t i = 0; i < shown; ++i) {
            const auto& e = *order[i];
            Row row;
            row.label = e.name;
            row.key = "barre:" + e.name;
            row.icon = e.unit ? ui::Icon::Program : ui::Icon::Section;
            row.parts = e.byLanguage;
            row.total = e.lines;
            row.action = e.unit ? std::string("unites") : "section:" + std::to_string(e.section);
            row.tip = e.name + kMid + "rang " + std::to_string(e.rank) + kMid
                    + (e.unit ? "unit\xC3\xA9 de " + st::thousands(e.sections) + " sections" + kMid : std::string{}) + languageSplit(e.byLanguage)
                    + (e.unit ? " \xE2\x80\x94 un clic : l'onglet des unit\xC3\xA9s" : " \xE2\x80\x94 un clic ouvre la section");
            rows.push_back(std::move(row));
        }
        if (order.size() > shown) {
            Row more;
            std::size_t n = 0;
            for (std::size_t i = shown; i < order.size(); ++i) {
                ++n;
                more.total += order[i]->lines;
                for (std::size_t l = 0; l < st::kLanguageCount; ++l) more.parts[l] += order[i]->byLanguage[l];
            }
            more.label = st::thousands(n) + " autres entr\xC3\xA9" "es";
            more.key = "barre:autres";
            more.others = true;
            more.icon = ui::Icon::Layers;
            more.action = "ordre";
            more.tip = more.label + " : " + st::thousands(more.total) + " lignes \xE2\x80\x94 un clic : l'ordre d'ex\xC3\xA9" "cution, toutes les entr\xC3\xA9" "es";
            rows.push_back(std::move(more));
        }
    } else if (breakdown_ == Breakdown::Sections) {
        std::vector<const st::SectionLines*> order;
        for (const auto& sec : code.sections)   // lot API 8 : celles que la recherche garde (nom, unite)
            if (query_.empty() || query_.matches({sec.name, sec.unit})) order.push_back(&sec);
        std::stable_sort(order.begin(), order.end(), [](const st::SectionLines* a, const st::SectionLines* z) { return a->lines > z->lines; });
        const std::size_t shown = order.size() > kTopRows ? kTopRows - 1 : order.size();
        for (std::size_t i = 0; i < shown; ++i) {
            const auto& sec = *order[i];
            Row row;
            row.label = sec.unit.empty() ? sec.name : sec.unit + " \xE2\x80\xBA " + sec.name;
            row.key = "barre:" + row.label;
            row.icon = ui::Icon::Section;
            row.parts[static_cast<std::size_t>(sec.language)] = sec.lines;
            row.total = sec.lines;
            row.action = "section:" + std::to_string(sec.section);
            row.tip = row.label + kMid + std::string(st::languageName(sec.language)) + kMid + st::thousands(sec.lines) + " lignes \xE2\x80\x94 un clic ouvre la section";
            rows.push_back(std::move(row));
        }
        if (order.size() > shown) {
            Row more;
            std::size_t n = 0;
            for (std::size_t i = shown; i < order.size(); ++i) {
                ++n;
                more.total += order[i]->lines;
                more.parts[static_cast<std::size_t>(order[i]->language)] += order[i]->lines;
            }
            more.label = st::thousands(n) + " autres sections";
            more.key = "barre:autres";
            more.others = true;
            more.icon = ui::Icon::Layers;
            more.action = "ordre";
            more.tip = more.label + " : " + st::thousands(more.total) + " lignes \xE2\x80\x94 un clic : l'ordre d'ex\xC3\xA9" "cution";
            rows.push_back(std::move(more));
        }
    } else {
        // Les quatre langages d'une tache (ST, SFC, LD, FBD) meme a zero - « pas
        // de LD » est une reponse ; IL et le reste seulement s'ils sont la.
        std::vector<std::size_t> langs;
        for (std::size_t l = 0; l < st::kLanguageCount; ++l) {
            const auto lang = static_cast<st::Language>(l);
            if (code.byLanguage[l] || (lang != st::Language::IL && lang != st::Language::Other)) langs.push_back(l);
        }
        std::stable_sort(langs.begin(), langs.end(), [&](std::size_t a, std::size_t z) { return code.byLanguage[a] > code.byLanguage[z]; });
        for (const auto l : langs) {
            Row row;
            row.label = std::string(st::languageLabel(static_cast<st::Language>(l)));
            row.key = "barre:" + std::string(st::languageName(static_cast<st::Language>(l)));
            row.icon = ui::Icon::Code;
            row.parts[l] = code.byLanguage[l];
            row.total = code.byLanguage[l];
            const auto pct = code.lines ? static_cast<double>(row.total) / static_cast<double>(code.lines) : 0.0;
            row.tip = row.total ? row.label + " : " + st::thousands(row.total) + " lignes, " + st::percentText(pct) + " du programme"
                                : row.label + " : aucune ligne dans " + code.task;
            rows.push_back(std::move(row));
        }
        note = "Chaque section compte les lignes de son texte, tel que l'export l'\xC3\xA9" "crit (un grafcet, un sch\xC3\xA9ma LD ou FBD : "
               "celles de sa description).";
        if (code.dfbLines)
            note += " Hors de " + code.task + " : " + st::thousands(code.dfbLines) + " lignes dans "
                  + (code.dfbs == 1 ? std::string("un bloc DFB") : st::thousands(code.dfbs) + " blocs DFB") + ".";
    }

    // L'echelle : la plus longue ENTREE. Le cumul des autres ne la donne pas (il
    // ecraserait toutes les barres) : il est dessine plus pale, et coupe s'il
    // depasse.
    std::size_t most = 1;
    for (const auto& row : rows)
        if (!row.others) most = std::max(most, row.total);
    const float rowH = 20.f;
    const float nameW = std::clamp(r.w * 0.34f, 120.f, 230.f);
    const float numberW = 62.f;
    const float barX = r.x + 14.f + nameW + 8.f;
    const float barW = std::max(30.f, r.right() - 14.f - numberW - 8.f - barX);
    float y = r.y + 46.f + (below ? kSegmentsRow : 0.f);
    if (rows.empty()) {
        // Lot API 8 : une recherche qui ne garde rien le dit.
        ctx.r.drawText({r.x + 14.f, y}, query_.empty() ? code.task + " n'ex\xC3\xA9" "cute rien encore." : std::string("Rien ne correspond \xC3\xA0 la recherche."),
                       kBody, c.textMuted);
        return;
    }
    for (const auto& row : rows) {
        const gfx::Rect line{r.x + 6.f, y - 1.f, r.w - 12.f, rowH};
        const bool clickable = !row.action.empty();
        const bool hot = clickable && hover_ == static_cast<int>(hits_.size());
        if (clickable) hits_.push_back({line, row.key, row.action, row.tip, false});
        else hits_.push_back({line, row.key, std::string{}, row.tip, false});
        if (hot) ctx.r.fillRoundedRect(line, ctx.theme.brand.hover, 4.f);
        ui::drawIcon(ctx.r, row.icon, {r.x + 14.f, y + 3.f, 12.f, 12.f}, c.textMuted);
        const auto label = fit(ctx, row.label, kBody, nameW - 20.f);
        // Lot API 8 : les termes cherches, surlignes (pas sur un cumul ni un langage).
        if (!row.others && breakdown_ != Breakdown::Languages)
            ui::drawSearchMarks(ctx.r, query_, label, r.x + 32.f, y + 1.f, ctx.r.lineHeight(kBody), nameW - 20.f, kBody);
        ctx.r.drawText({r.x + 32.f, y + 1.f}, label, kBody, c.text);
        std::vector<std::pair<double, gfx::Color>> parts;
        for (std::size_t l = 0; l < st::kLanguageCount; ++l) {
            const auto colour = languageColour(ctx, static_cast<st::Language>(l));
            parts.emplace_back(static_cast<double>(row.parts[l]), row.others ? colour.withAlpha(110) : colour);
        }
        stackedBar(ctx, {barX, y + 4.f, barW, 10.f}, parts, static_cast<double>(most), c.border);
        drawRight(ctx, r.right() - 14.f, y + 2.f, st::thousands(row.total), kSmall, c.textMuted);
        y += rowH;
    }
    if (!note.empty()) {
        const auto lines = wrap(ctx, note, kSmall, r.w - 28.f, 3);
        y += 10.f;
        for (const auto& l : lines) {
            ctx.r.drawText({r.x + 14.f, y}, l, kSmall, c.textMuted);
            y += 16.f;
        }
    }
}

// --------------------------------------------------------- les variables ----
void StatisticsPane::paintVariables(const ui::PaintContext& ctx, const gfx::Rect& r) {
    const auto& c = ctx.theme.color;
    const auto& vs = stats_.variables;
    card(ctx, r, false);
    caption(ctx, r.x + 14.f, r.y + 14.f, ui::Icon::Variable, "LES VARIABLES");

    // Trois barres : chacune pleine, partagee par genre.
    const char* const names[3] = {"Globales", "Locales", "Param\xC3\xA8tres"};
    const char* const keys[3] = {"portee:globales", "portee:locales", "portee:parametres"};
    const char* const actions[3] = {"variables", "unites", "unites"};
    const float labelW = 100.f, numberW = 48.f;
    const float barX = r.x + 14.f + labelW;
    const float barW = std::max(30.f, r.right() - 14.f - numberW - 8.f - barX);
    // La legende mesuree d'abord : etroite (deux colonnes vers 1 000 pixels),
    // elle prend une ligne de plus et les barres se resserrent d'autant - les
    // compteurs debordaient du bas du bloc. 171 : le titre (46), l'ecart sous
    // la legende (23), les compteurs (94) et la marge du bas (8).
    int legendLines = 1;
    {
        float lx = r.x + 14.f;
        for (std::size_t g = 0; g < st::kGenreCount; ++g) {
            const float w = 13.f + textWidth(ctx, genreName(g), kSmall);
            if (lx + w > r.right() - 12.f && lx > r.x + 14.f) {
                lx = r.x + 14.f;
                ++legendLines;
            }
            lx += w + 12.f;
        }
    }
    const float step = std::clamp((r.h - 171.f - 16.f * static_cast<float>(legendLines - 1)) / 3.f, 18.f, 27.f);
    float y = r.y + 46.f;
    for (std::size_t s = 0; s < st::kScopeCount; ++s) {
        std::string tip = std::string(names[s]) + " : " + st::thousands(vs.count[s]);
        std::vector<std::pair<double, gfx::Color>> parts;
        for (std::size_t g = 0; g < st::kGenreCount; ++g) {
            parts.emplace_back(static_cast<double>(vs.byGenre[s][g]), genreColour(ctx, g));
            if (vs.byGenre[s][g]) tip += std::string(g ? kMid : " \xE2\x80\x94 ") + genreName(g) + " " + st::thousands(vs.byGenre[s][g]);
        }
        tip += s == 0 ? " \xE2\x80\x94 un clic : l'onglet Variables" : " \xE2\x80\x94 un clic : l'onglet des unit\xC3\xA9s";
        const gfx::Rect line{r.x + 6.f, y - 3.f, r.w - 12.f, std::min(24.f, step)};
        const bool hot = hover_ == static_cast<int>(hits_.size());
        hits_.push_back({line, keys[s], actions[s], tip, false});
        if (hot) ctx.r.fillRoundedRect(line, ctx.theme.brand.hover, 4.f);
        ctx.r.drawText({r.x + 14.f, y + 1.f}, names[s], kBody, c.text);
        stackedBar(ctx, {barX, y + 2.f, barW, 13.f}, parts, static_cast<double>(vs.count[s]), c.border);
        drawRight(ctx, r.right() - 14.f, y + 1.f, st::thousands(vs.count[s]), kBody, c.text);
        y += step;
    }
    // La legende.
    float lx = r.x + 14.f;
    float ly = y + 1.f;
    for (std::size_t g = 0; g < st::kGenreCount; ++g) {
        const std::string name = genreName(g);
        const float w = 13.f + textWidth(ctx, name, kSmall);
        if (lx + w > r.right() - 12.f && lx > r.x + 14.f) {
            lx = r.x + 14.f;
            ly += 16.f;
        }
        ctx.r.fillRoundedRect({lx, ly + 3.f, 9.f, 9.f}, genreColour(ctx, g), 2.f);
        ctx.r.drawText({lx + 13.f, ly}, name, kSmall, c.textMuted);
        lx += w + 12.f;
    }

    // Les six compteurs, des globales.
    struct Counter {
        const char* key;
        std::string label;
        std::size_t value;
        int level;          // 0 : neutre, 1 : a regarder, 2 : un defaut
        std::string action, tip;
    };
    const auto one = [](const std::vector<st::NamedVariable>& v, const std::string& many) {
        return v.size() == 1 && v.front().index != domain::kNoIndex ? "variable:" + std::to_string(v.front().index) : many;
    };
    std::vector<Counter> counters;
    counters.push_back({"compteur:pas-utilisees", "pas utilis\xC3\xA9" "es", vs.unused.size(), vs.unused.empty() ? 0 : 1, "variables-inutilisees",
                        "Globales que le code ne lit ni n'\xC3\xA9" "crit, qu'aucune unit\xC3\xA9 ne re\xC3\xA7oit. Pas utilis\xC3\xA9" "e ne veut pas dire "
                        "\xC3\xA0 supprimer : l'IHM peut la lire."});
    counters.push_back({"compteur:lues-ihm", "lues par l'IHM", vs.readByHmi.size(), 0, one(vs.readByHmi, "variables"),
                        "Globales que l'IHM lit, par une variable IHM li\xC3\xA9" "e ou cit\xC3\xA9" "es telles quelles."});
    counters.push_back({"compteur:ecrites-jamais-lues", "\xC3\xA9" "crites, jamais lues", vs.writtenNeverRead.size(), vs.writtenNeverRead.empty() ? 0 : 1,
                        one(vs.writtenNeverRead, "variables"),
                        "Le code les \xC3\xA9" "crit, rien ne les lit (ni le code ni l'IHM) ; les situ\xC3\xA9" "es sont \xC3\xA0 part, on les lit de dehors."});
    counters.push_back({"compteur:jamais-ecrites", "jamais \xC3\xA9" "crites", vs.readNeverWritten.size(), vs.readNeverWritten.empty() ? 0 : 1,
                        one(vs.readNeverWritten, "variables"),
                        "Le code les lit, rien ne les \xC3\xA9" "crit : sans valeur initiale, elles valent 0. \xC3\x80 part : les situ\xC3\xA9" "es (le "
                        "mat\xC3\xA9riel, l'IHM ou un superviseur peuvent les \xC3\xA9" "crire) et celles que l'IHM lit."});
    counters.push_back({"compteur:adresses-double", "adresses en double", vs.duplicates.size(), vs.duplicates.empty() ? 0 : 2,
                        vs.duplicates.size() == 1 ? "variables:" + vs.duplicates.front().address : std::string("variables"),
                        "Deux variables \xC3\xA0 la m\xC3\xAAme adresse : l'une \xC3\xA9" "crase l'autre."});
    counters.push_back({"compteur:es-sans-commentaire", "E/S sans commentaire", vs.ioWithoutComment.size(), vs.ioWithoutComment.empty() ? 0 : 1,
                        one(vs.ioWithoutComment, "variables"), "Les entr\xC3\xA9" "es et sorties (%I, %Q, %CH) sans commentaire."});
    // Les noms, dans l'infobulle.
    std::vector<const std::vector<st::NamedVariable>*> lists = {&vs.unused, &vs.readByHmi, &vs.writtenNeverRead, &vs.readNeverWritten, nullptr,
                                                                &vs.ioWithoutComment};
    // 1.12.0 : XPGAnalyser API n'a pas d'IHM - ni « lues par l'IHM », ni l'IHM dans les phrases.
    if (!core::hasIhm()) {
        counters.erase(counters.begin() + 1);
        lists.erase(lists.begin() + 1);
        for (auto& c : counters)
            for (const char* from : {" l'IHM peut la lire.", " (ni le code ni l'IHM)", ", l'IHM ou un superviseur", " et celles que l'IHM lit"}) {
                const auto at = c.tip.find(from);
                if (at == std::string::npos) continue;
                const std::string_view f(from);
                c.tip.replace(at, f.size(), f == " l'IHM peut la lire." ? " un superviseur peut la lire."
                                            : f == " (ni le code ni l'IHM)" ? " (ni le code)"
                                            : f == ", l'IHM ou un superviseur" ? " ou un superviseur" : "");
            }
    }
    for (std::size_t k = 0; k < counters.size(); ++k) {
        if (lists[k] && !lists[k]->empty()) counters[k].tip += " " + namesOf(*lists[k], 8) + ".";
        if (!lists[k] && !vs.duplicates.empty()) {
            std::string d;
            for (std::size_t i = 0; i < vs.duplicates.size() && i < 4; ++i) {
                d += (i ? kMid : " ") + vs.duplicates[i].address + " :";
                for (std::size_t j = 0; j < vs.duplicates[i].variables.size(); ++j)
                    d += (j ? ", " : " ") + vs.duplicates[i].variables[j].name;
            }
            counters[k].tip += d;
        }
        if (counters[k].value) counters[k].tip += counters[k].action.rfind("variable:", 0) == 0 ? " Un clic : la variable." : " Un clic : l'onglet Variables.";
    }
    const float gy = std::max(ly + 22.f, r.bottom() - 2.f * 44.f - 6.f - 12.f);
    const float gw = (r.w - 28.f - 2.f * 8.f) / 3.f;
    for (std::size_t k = 0; k < counters.size(); ++k) {
        const auto& ct = counters[k];
        const auto col = static_cast<float>(k % 3);
        const auto rowIdx = static_cast<float>(k / 3);
        const gfx::Rect box{r.x + 14.f + col * (gw + 8.f), gy + rowIdx * (44.f + 6.f), gw, 44.f};
        const bool hot = hover_ == static_cast<int>(hits_.size());
        hits_.push_back({box, ct.key, ct.action, ct.tip, false});
        ctx.r.fillRoundedRect(box, hot ? c.borderStrong : c.border, 6.f);
        ctx.r.fillRoundedRect({box.x + 1.f, box.y + 1.f, box.w - 2.f, box.h - 2.f}, hot ? c.rowAltBg : c.panelBg, 5.f);
        ctx.r.drawText({box.x + 9.f, box.y + 5.f}, fit(ctx, ct.label, kSmall, box.w - 16.f), kSmall, c.textMuted);
        const auto colour = ct.level == 0 || ct.value == 0 ? c.text
                          : ct.level == 2 ? ctx.theme.onSurface(c.error) : ctx.theme.onSurface(c.warning);
        drawBold(ctx, {box.x + 9.f, box.y + 20.f}, st::thousands(ct.value), kValue, colour);
    }
}

// ------------------------------------------------------------ la memoire ----
void StatisticsPane::paintMemory(const ui::PaintContext& ctx, const gfx::Rect& r) {
    const auto& c = ctx.theme.color;
    const auto& mem = stats_.memory;
    card(ctx, r, false);
    const float capW = caption(ctx, r.x + 14.f, r.y + 14.f, ui::Icon::Layers, "LA M\xC3\x89MOIRE, PAR TYPE");
    const std::string side = st::bytesText(mem.total) + " d\xC3\xA9" "clar\xC3\xA9" "s" + kMid + "un mod\xC3\xA8le des d\xC3\xA9" "clarations, pas le plan de l'automate";
    const float sideW = r.right() - 14.f - (r.x + 14.f + capW + 16.f);
    const auto sideText = fit(ctx, side, kSmall, sideW);
    drawRight(ctx, r.right() - 14.f, r.y + 14.f, sideText, kSmall, c.textMuted);

    std::vector<const st::TypeRow*> rows;
    for (const auto& t : mem.types)
        if (t.totalBytes > 0 && rows.size() < kTypeRows && typeKept(t)) rows.push_back(&t);     // lot API 8 : la recherche
    float y = r.y + 46.f;
    if (rows.empty()) {
        ctx.r.drawText({r.x + 14.f, y}, query_.empty() ? std::string("Aucune d\xC3\xA9" "claration ne p\xC3\xA8se encore.")
                                                       : std::string("Aucun type ne correspond \xC3\xA0 la recherche."),
                       kBody, c.textMuted);
        return;
    }
    const std::uint64_t most = std::max<std::uint64_t>(1, rows.front()->totalBytes);
    const float nameW = std::clamp(r.w * 0.30f, 120.f, 200.f);
    const float valueW = 118.f;
    const float barX = r.x + 14.f + nameW + 8.f;
    const float barW = std::max(30.f, r.right() - 14.f - valueW - 8.f - barX);
    for (const auto* t : rows) {
        const gfx::Rect line{r.x + 6.f, y - 1.f, r.w - 12.f, 20.f};
        const bool hot = hover_ == static_cast<int>(hits_.size());
        hits_.push_back({line, "type:" + t->name, typeAction(*t),
                         t->name + " (" + std::string(st::typeGenreLabel(t->genre)) + ") : " + st::thousands(t->instances)
                             + (t->instances == 1 ? " instance de " : " instances de ") + bytesInText(t->unitBytes) + " \xE2\x80\x94 un clic "
                             + (t->genre == st::TypeGenre::Derived ? "ouvre l'onglet des types"
                                : t->genre == st::TypeGenre::Dfb   ? "ouvre l'onglet des blocs DFB"
                                                                   : "cherche ses variables"),
                         false});
        if (hot) ctx.r.fillRoundedRect(line, ctx.theme.brand.hover, 4.f);
        ui::drawIcon(ctx.r, typeIcon(t->genre), {r.x + 14.f, y + 3.f, 12.f, 12.f}, c.textMuted);
        const auto name = fit(ctx, t->name, kBody, nameW - 20.f);
        ui::drawSearchMarks(ctx.r, query_, name, r.x + 32.f, y + 1.f, ctx.r.lineHeight(kBody), nameW - 20.f, kBody);     // lot API 8
        ctx.r.drawText({r.x + 32.f, y + 1.f}, name, kBody, c.text);
        stackedBar(ctx, {barX, y + 4.f, barW, 10.f}, {{static_cast<double>(t->totalBytes), c.accent}}, static_cast<double>(most), c.border);
        drawRight(ctx, r.right() - 14.f, y + 2.f, st::bytesText(t->totalBytes) + kMid + st::percentText(t->share), kSmall, c.textMuted);
        y += 20.f;
    }

    // Qui pese le plus, et avec quoi ; les types inconnus.
    const auto& top = *rows.front();
    std::string sentence = top.name + " p\xC3\xA8se le plus : " + st::thousands(top.instances) + (top.instances == 1 ? " instance" : " instances");
    if (!top.holders.empty()) {
        const std::size_t shown = std::min<std::size_t>(top.holders.size(), 3);
        bool same = top.declared == top.holders.size();
        for (const auto& h : top.holders) same = same && h.second == top.holders.front().second;
        std::string names;
        for (std::size_t i = 0; i < shown; ++i)
            names += (i == 0 ? "" : (i + 1 == shown && top.declared <= shown) ? " et " : ", ") + top.holders[i].first;
        if (top.declared > shown) names += " et " + st::thousands(top.declared - shown) + (top.declared - shown == 1 ? " autre" : " autres");
        if (same && top.holders.size() > 1 && top.holders.front().second > 1)
            names += ", " + st::thousands(top.holders.front().second) + " chacune";
        sentence += " (" + names + ")";
    }
    sentence += " de " + bytesInText(top.unitBytes) + ".";
    y += 10.f;
    const auto lines = wrap(ctx, sentence, kSmall, r.w - 28.f, 2);
    for (const auto& l : lines) {
        ctx.r.drawText({r.x + 14.f, y}, l, kSmall, c.textMuted);
        y += 16.f;
    }
    if (!mem.unresolvedTypes.empty() && y + 16.f <= r.bottom() - 6.f) {
        std::string unknown = mem.unresolvedTypes.size() == 1 ? "Un type inconnu, compt\xC3\xA9 0 octet : " : "Des types inconnus, compt\xC3\xA9s 0 octet : ";
        for (std::size_t i = 0; i < mem.unresolvedTypes.size() && i < 4; ++i) unknown += (i ? ", " : "") + mem.unresolvedTypes[i];
        if (mem.unresolvedTypes.size() > 4) unknown += std::string(", ") + kDots;
        unknown += " \xE2\x80\x94 le total est un minimum.";
        ctx.r.drawText({r.x + 14.f, y}, fit(ctx, unknown, kSmall, r.w - 28.f), kSmall, ctx.theme.onSurface(c.warning));
    }
}

// ----------------------------------------------------------- a regarder ----
void StatisticsPane::paintTodo(const ui::PaintContext& ctx, const gfx::Rect& r) {
    const auto& c = ctx.theme.color;
    card(ctx, r, false);
    caption(ctx, r.x + 14.f, r.y + 14.f, ui::Icon::Warning, "\xC3\x80 REGARDER");
    const auto& list = stats_.findings;
    std::size_t real = 0;
    for (const auto& f : list) real += f.tone == st::Finding::Tone::Ok ? 0u : 1u;
    drawRight(ctx, r.right() - 14.f, r.y + 14.f, std::to_string(real), kSmall, c.textMuted);
    float y = r.y + 44.f;
    const std::size_t shown = todoRows();
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& f = list[i];
        const auto tone = f.tone == st::Finding::Tone::Error   ? c.error
                        : f.tone == st::Finding::Tone::Warning ? c.warning
                        : f.tone == st::Finding::Tone::Ok      ? c.ok
                                                               : c.info;
        const gfx::Rect mark{r.x + 14.f, y + 4.f, 20.f, 20.f};
        ctx.r.fillRoundedRect(mark, tone.withAlpha(ctx.theme.isDark() ? 60 : 40), 10.f);
        const std::string glyph = f.tone == st::Finding::Tone::Ok ? "\xE2\x9C\x93" : f.id == "bibliotheque" ? "\xE2\x86\x91" : "!";
        const float gw = textWidth(ctx, glyph, kSmall);
        ctx.r.drawText({mark.x + (mark.w - gw) * 0.5f, mark.y + (mark.h - ctx.r.lineHeight(kSmall)) * 0.5f}, glyph, kSmall, ctx.theme.onSurface(tone));
        float textRight = r.right() - 14.f;
        if (!f.button.empty()) {
            const bool hot = hover_ == static_cast<int>(hits_.size());
            const auto br = button(ctx, r.right() - 14.f, y + 15.f, f.button, hot);
            hits_.push_back({br, "regarder:" + f.id, f.request, f.title, true});
            textRight = br.x - 10.f;
        }
        const float tx = r.x + 44.f;
        ctx.r.drawText({tx, y + 2.f}, fit(ctx, f.title, kBody, textRight - tx), kBody, c.text);
        const auto det = wrap(ctx, f.detail, kSmall, textRight - tx, 2);
        for (std::size_t k = 0; k < det.size(); ++k)
            ctx.r.drawText({tx, y + 20.f + static_cast<float>(k) * 14.f}, det[k], kSmall, c.textMuted);
        y += 52.f;
        if (i + 1 < shown) ctx.r.fillRect({r.x + 14.f, y - 5.f, r.w - 28.f, 1.f}, c.border);
    }
    if (list.size() > shown) {
        std::string more = "+ " + std::to_string(list.size() - shown) + " autre(s) : ";
        for (std::size_t i = shown; i < list.size(); ++i) more += (i > shown ? kMid : "") + list[i].title;
        ctx.r.drawText({r.x + 14.f, std::min(y, r.bottom() - 20.f)}, fit(ctx, more, kSmall, r.w - 28.f), kSmall, c.textMuted);
    }
}

// --------------------------------------------------------------- la souris ----
int StatisticsPane::hitAt(gfx::Point p) const {
    if (!area().contains(p)) return -1;      // lot API 8 : sous la barre, ce qui a defile dessous ne compte plus
    // Les boutons d'abord : ils sont poses sur des lignes.
    for (std::size_t i = 0; i < hits_.size(); ++i)
        if (hits_[i].button && hits_[i].rect.contains(p)) return static_cast<int>(i);
    for (std::size_t i = 0; i < hits_.size(); ++i)
        if (!hits_[i].button && hits_[i].rect.contains(p)) return static_cast<int>(i);
    return -1;
}

void StatisticsPane::act(const std::string& action) {
    if (action.rfind("seg:", 0) == 0 && action.size() > 4) {
        const int i = action[4] - '0';
        if (i >= 0 && i <= 2 && static_cast<int>(breakdown_) != i) {
            breakdown_ = static_cast<Breakdown>(i);
            invalidate();
        }
        return;
    }
    if (action == "scroll:table") {
        const auto pl = plan(area().w, area().h);      // lot API 8 : sous la barre
        scrollTo(pl.table.y - pl.pad);
        return;
    }
    if (!action.empty() && hosts_.request) hosts_.request(action);
}

void StatisticsPane::openType(std::size_t row) {
    const auto& rows = stats_.memory.types;
    if (row < rows.size() && hosts_.request) hosts_.request(typeAction(rows[row]));
}

ui::EventResult StatisticsPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (!area().contains(w->pos) || maxScroll() <= 0.f) return ui::EventResult::Ignored;     // lot API 8 : sous la barre
        scrollTo(scrollY_ - w->dy * 48.f);
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        if (dragThumb_) {
            const auto b = area();      // lot API 8
            const auto thumb = thumbRect();
            const float travel = b.h - thumb.h;
            if (travel > 0.f) scrollTo((m->pos.y - dragGrab_ - b.y) / travel * maxScroll());
            return ui::EventResult::Consumed;
        }
        const int h = hitAt(m->pos);
        if (h != hover_) {
            hover_ = h;
            setTooltip(h >= 0 ? hits_[static_cast<std::size_t>(h)].tip : std::string{});
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && m->button == ui::MouseButton::Left) {
        const auto thumb = thumbRect();
        if (thumb.w > 0.f && m->pos.x >= thumb.x - 4.f && area().contains(m->pos)) {       // lot API 8 : sous la barre
            // Sur le pouce : on le tire ; a cote, dans la gouttiere : une page.
            if (m->pos.y >= thumb.y && m->pos.y < thumb.bottom()) {
                dragThumb_ = true;
                dragGrab_ = m->pos.y - thumb.y;
            } else {
                scrollTo(scrollY_ + (m->pos.y < thumb.y ? -1.f : 1.f) * area().h * 0.8f);
            }
            invalidate();
            return ui::EventResult::Consumed;
        }
        pressed_ = hitAt(m->pos);
        if (pressed_ >= 0) return ui::EventResult::Consumed;
    }
    // Un clic, c'est l'appui ET le relacher sur la meme zone (comme le tableau de
    // bord) : le relacher seul peut etre celui d'un dialogue qui vient de se fermer.
    if (const auto* m = std::get_if<ui::MouseUp>(&ev); m && m->button == ui::MouseButton::Left) {
        if (dragThumb_) {
            dragThumb_ = false;
            invalidate();
            return ui::EventResult::Consumed;
        }
        const int h = hitAt(m->pos);
        const bool click = h >= 0 && h == pressed_;
        pressed_ = -1;
        if (click) {
            const auto action = hits_[static_cast<std::size_t>(h)].action;   // une copie : act() peut tout redessiner
            act(action);
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

} // namespace app
