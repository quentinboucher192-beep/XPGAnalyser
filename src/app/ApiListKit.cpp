// app/ApiListKit.cpp - la ligne des filtres des listes de l'API (lot API 5).
#include "ApiListKit.hpp"

#include "../ui/TextSearch.hpp"
#include "../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>

namespace app {

namespace {
const gfx::FontId kSmall{13};
}

ApiFilterBar::ApiFilterBar(std::string id, std::string placeholder) : ui::Widget(std::move(id)) {
    field_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(this->id() + ".chercher")));
    field_->setPlaceholder(std::move(placeholder));
    field_->setEscapeClears(true);          // lot API 8 : finitions - Echap efface la recherche
    // Lot recherche : la meme recherche partout (ui::SearchQuery).
    field_->setTooltip("Chaque mot est cherch\xC3\xA9 dans toutes les colonnes, commentaires compris (ET), sans casse ni accents ; "
                       "\"une phrase\" entre guillemets ; -mot : l'exclure. Les entonnoirs des titres filtrent une colonne.");
    links_ += field_->textChanged->connect([this](const std::string&) { changed->emit(); });
    // Lot API 8 : la recherche et la pastille retenues (relues par recall()).
    // Un geste - ou une recherche posee par programme - avant la relecture
    // l'emporte sur elle.
    links_ += field_->textChanged->connect([this](const std::string&) {
        recalled_ = true;
        memory_.save();
    });
    memory_.bind(this->id(),
                 [this] {
                     // La premiere pastille (Toutes, Tous) : rien a retenir.
                     return ui::SearchMemory::State{search(), current_ > 0 && current_ < chips_.size() ? chips_[current_].first : std::string{}};
                 },
                 [this](const ui::SearchMemory::State& s) { applyRemembered(s); }, false);
}

void ApiFilterBar::setChips(std::vector<std::pair<std::string, std::size_t>> chips) {
    const std::string keep = current_ < chips_.size() ? chips_[current_].first : std::string{};
    chips_ = std::move(chips);
    current_ = 0;
    for (std::size_t i = 0; i < chips_.size(); ++i)
        if (chips_[i].first == keep) current_ = i;
    // Lot API 8 : la pastille retenue, arrivee avant les pastilles.
    if (!pendingChip_.empty() && !chips_.empty()) {
        for (std::size_t i = 0; i < chips_.size(); ++i)
            if (apikit::lower(chips_[i].first) == apikit::lower(pendingChip_)) current_ = i;
        pendingChip_.clear();
    }
    invalidate();
}

void ApiFilterBar::setCurrent(std::size_t index) {
    if (index >= chips_.size() || index == current_) return;
    current_ = index;
    invalidate();
    changed->emit();
    recalled_ = true;          // lot API 8 : retenue
    memory_.save();
}

// ---- Lot API 8 : retenue d'une seance a l'autre, et le compte ----
void ApiFilterBar::recall() {
    recalled_ = true;
    memory_.recall();
}

void ApiFilterBar::applyRemembered(const ui::SearchMemory::State& s) {
    // La pastille d'abord, sans signal : un seul `changed` pour les deux.
    bool chipMoved = false;
    pendingChip_.clear();
    if (chips_.empty()) {
        pendingChip_ = s.chip;
    } else {
        std::size_t want = 0;           // pas retenue, ou plus la : la premiere
        for (std::size_t i = 0; i < chips_.size() && !s.chip.empty(); ++i)
            if (apikit::lower(chips_[i].first) == apikit::lower(s.chip)) want = i;
        if (want != current_) {
            current_ = want;
            chipMoved = true;
            invalidate();
        }
    }
    if (field_->text() != s.search) field_->setText(s.search);     // textChanged : changed
    else if (chipMoved) changed->emit();
}

void ApiFilterBar::setCount(std::size_t shown, std::size_t total) {
    if (counted_ && shown == countShown_ && total == countTotal_) return;
    counted_ = true;
    countShown_ = shown;
    countTotal_ = total;
    invalidate();
}

std::string ApiFilterBar::countText() const {
    if (!counted_ || search().empty()) return {};
    return apikit::thousands(countShown_) + " sur " + apikit::thousands(countTotal_);
}
// ---- fin Lot API 8 ----

bool ApiFilterBar::chooseChip(std::string_view label) {
    const auto want = apikit::lower(label);
    for (std::size_t i = 0; i < chips_.size(); ++i)
        if (apikit::lower(chips_[i].first).rfind(want, 0) == 0) {
            if (i == current_) return true;
            setCurrent(i);
            return true;
        }
    return false;
}

std::string ApiFilterBar::search() const { return field_ ? field_->text() : std::string{}; }

void ApiFilterBar::setSearch(const std::string& text) {
    if (field_) field_->setText(text);   // textChanged -> changed
}

gfx::Rect ApiFilterBar::chipRect(std::size_t index) const {
    return index < rects_.size() ? rects_[index] : gfx::Rect{};
}

ui::SizeHint ApiFilterBar::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {600.f, 44.f};
    h.minimum = {200.f, 44.f};
    h.stretchX = 1.f;
    return h;
}

void ApiFilterBar::onLayout() {
    // Lot API 8 : le volet n'a pas demande la recherche retenue - elle arrive
    // au premier placement, avant le premier dessin.
    if (!recalled_) recall();
    const auto b = bounds();
    field_->setBounds({b.x + 12.f, b.y + 7.f, std::clamp(b.w * 0.26f, 180.f, 340.f), 30.f});
}

void ApiFilterBar::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    r.fillRect(b, c.panelBg);
    r.fillRect({b.x, b.bottom() - 1.f, b.w, 1.f}, c.border);
    rects_.clear();
    float x = field_->bounds().right() + 14.f;
    for (std::size_t i = 0; i < chips_.size(); ++i) {
        const std::string label = chips_[i].first;
        const std::string count = std::to_string(chips_[i].second);
        const float lw = r.measure(label, kSmall).width, cw = r.measure(count, kSmall).width;
        const gfx::Rect chip{x, b.y + 10.f, lw + cw + 32.f, 24.f};
        const bool on = i == current_;
        r.fillRoundedRect(chip, on ? c.accent.withAlpha(60) : (static_cast<int>(i) == hover_ ? c.rowAltBg : c.headerBg), 12.f);
        if (on) r.strokeRect(chip, c.accent, 1.f);
        const float ty = chip.y + (chip.h - r.lineHeight(kSmall)) * 0.5f;
        r.drawText({chip.x + 12.f, ty}, label, kSmall, on ? c.text : c.textMuted);
        r.drawText({chip.x + 20.f + lw, ty}, count, kSmall, on ? c.accent : c.textMuted);
        rects_.push_back(chip);
        x += chip.w + 8.f;
    }
    // Lot API 8 : "12 sur 40", a droite, tant qu'une recherche est tapee.
    if (const auto count = countText(); !count.empty()) {
        const float cx = b.right() - 14.f - r.measure(count, kSmall).width;
        if (cx > x) r.drawText({cx, b.y + (b.h - r.lineHeight(kSmall)) * 0.5f}, count, kSmall,
                               countShown_ == 0 ? ctx.theme.onSurface(c.warning) : c.textMuted);
    }
}

ui::EventResult ApiFilterBar::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        int h = -1;
        for (std::size_t i = 0; i < rects_.size(); ++i)
            if (rects_[i].contains(m->pos)) h = static_cast<int>(i);
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        for (std::size_t i = 0; i < rects_.size(); ++i)
            if (rects_[i].contains(d->pos)) {
                setCurrent(i);
                return ui::EventResult::Consumed;
            }
        // Lot API 8 : un clic sur la barre reste a la barre - ce qui defile dessous
        // (la table des statistiques) ne le recoit pas.
        if (bounds().contains(d->pos)) return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ================================================================= les aides ====
namespace apikit {

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool containsNoCase(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    return lower(hay).find(lower(needle)) != std::string::npos;
}

bool matches(std::initializer_list<std::string_view> texts, std::string_view query) {
    return ui::SearchQuery(query).matches(texts);
}

std::string thousands(std::size_t n) {
    std::string d = std::to_string(n), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += d[i];
    }
    return out;
}

std::string plural(std::size_t n, std::string_view one, std::string_view many) {
    return std::to_string(n) + " " + std::string(n == 1 ? one : many);
}

std::string LibState::label() const {
    switch (kind) {
        case UpToDate:  return "\xC3\xA0 jour";
        case Different: return version + " disponible";
        default:        return "projet";
    }
}

LibState libraryState(const project::SharedLibrary* library, std::string_view name, std::string_view projectVersion,
                      project::LibraryItemKind kind) {
    LibState s;
    if (!library) return s;
    const auto* item = library->find(name);
    if (!item || item->kind != kind) return s;
    s.version = item->version;
    // Compare comme SharedLibrary::outdated : en texte, different = a regarder.
    s.kind = item->version.empty() || item->version == projectVersion ? LibState::UpToDate : LibState::Different;
    return s;
}

// ---- lot API 6 : coller depuis Excel ----------------------------------------------
bool validPlcType(const domain::Project& p, std::string_view type, std::string* why) {
    std::string t(type);
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    std::size_t a = 0;
    while (a < t.size() && std::isspace(static_cast<unsigned char>(t[a]))) ++a;
    t = t.substr(a);
    const auto no = [&](std::string text) { if (why) *why = std::move(text); return false; };
    if (t.empty()) return no("type vide");
    std::string u = t;
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (u.rfind("ARRAY", 0) == 0) {
        const auto of = u.find(" OF ");
        if (u.find('[') == std::string::npos || u.find("..") == std::string::npos || of == std::string::npos)
            return no("tableau illisible : ARRAY[0..9] OF INT");
        const std::string element = t.substr(of + 4);
        for (domain::Index i = 0; i < p.pous.size(); ++i)
            if (p.pous[i].kind == domain::PouKind::FunctionBlockType && lower(p.strings.text(p.pous[i].name)) == lower(element))
                return no("pas de tableau de blocs DFB : une instance par variable");
        return validPlcType(p, element, why);
    }
    if (u == "STRING" || (u.rfind("STRING[", 0) == 0 && u.back() == ']')) return true;
    static const char* const kTypes[] = {"BOOL", "EBOOL", "INT", "UINT", "DINT", "UDINT", "WORD", "DWORD", "BYTE", "REAL", "LREAL",
                                         "TIME", "DATE", "TOD", "DT", "SINT", "USINT", "LINT", "ULINT", "TIME_OF_DAY", "DATE_AND_TIME"};
    for (const auto* k : kTypes)
        if (u == k) return true;
    for (const auto& d : p.derivedTypes)
        if (lower(p.strings.text(d.name)) == lower(t)) return true;
    for (const auto& pou : p.pous)
        if (pou.kind == domain::PouKind::FunctionBlockType && lower(p.strings.text(pou.name)) == lower(t)) return true;
    return no("type inconnu \xC2\xAB " + t + " \xC2\xBB : ni \xC3\xA9l\xC3\xA9mentaire, ni un DDT ou un DFB du projet");
}

bool validAddress(std::string_view address, std::string* why) {
    if (address.empty()) return true;
    if (domain::Address::parse(address).valid()) return true;
    if (why) *why = "\xC2\xAB " + std::string(address) + " \xC2\xBB n'est pas une adresse : %MW10, %I0.3, %Q0.1\xE2\x80\xA6";
    return false;
}

std::string freeName(std::string_view wanted, const std::vector<std::string>& taken) {
    const auto used = [&](const std::string& n) {
        return std::any_of(taken.begin(), taken.end(), [&](const std::string& t) { return lower(t) == lower(n); });
    };
    std::string base(wanted);
    if (!used(base)) return base;
    for (int k = 2; k < 10000; ++k) {
        const std::string n = base + "_" + std::to_string(k);
        if (!used(n)) return n;
    }
    return base;
}

// ---- Lot API 8 : chercher dans les listes qui n'avaient pas de recherche ----
std::string leadingComment(std::string_view code, std::size_t max) {
    std::size_t i = 0;
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    const auto skipBlank = [&] {
        while (i < code.size() && blank(code[i])) ++i;
    };
    skipBlank();
    std::string raw;
    if (code.compare(i, 2, "(*") == 0) {
        const auto end = code.find("*)", i + 2);
        raw = std::string(code.substr(i + 2, (end == std::string_view::npos ? code.size() : end) - (i + 2)));
    } else {
        while (i + 1 < code.size() && code.compare(i, 2, "//") == 0) {
            auto eol = code.find('\n', i);
            if (eol == std::string_view::npos) eol = code.size();
            raw += " " + std::string(code.substr(i + 2, eol - (i + 2)));
            i = eol;
            skipBlank();
        }
    }
    // Une ligne : les blancs (et les * ou - de decoration en tete) resserres.
    std::string out;
    bool space = false;
    for (const char c : raw) {
        if (blank(c)) {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    while (!out.empty() && (out.front() == '*' || out.front() == '-' || out.front() == '=' || out.front() == ' ')) out.erase(out.begin());
    while (!out.empty() && (out.back() == '*' || out.back() == '-' || out.back() == '=' || out.back() == ' ')) out.pop_back();
    if (out.size() > max) {
        std::size_t cut = max;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out = out.substr(0, cut) + "\xE2\x80\xA6";
    }
    return out;
}

TwoLevelMatch searchTwoLevels(const ui::SearchQuery& query, const std::vector<std::string>& parentTexts,
                              const std::vector<std::vector<std::string>>& childTexts) {
    TwoLevelMatch m;
    m.children.assign(childTexts.size(), true);
    if (query.empty()) return m;
    bool anyChild = false;
    for (std::size_t i = 0; i < childTexts.size(); ++i) {
        std::vector<std::string> texts = childTexts[i];
        texts.insert(texts.end(), parentTexts.begin(), parentTexts.end());
        m.children[i] = query.matches(texts);
        anyChild = anyChild || m.children[i];
    }
    m.parent = anyChild || query.matches(parentTexts);
    return m;
}
// ---- fin Lot API 8 ----

} // namespace apikit
} // namespace app
