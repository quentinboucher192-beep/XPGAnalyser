// =============================================================================
//  app/MacrosPane.cpp - l'onglet Macros (lot macros 1)
// =============================================================================
#include "MacrosPane.hpp"

#include "MacroEditorView.hpp"
#include "MacroFormView.hpp"
#include "hmi/HmiIcons.hpp"
#include "hmi/HmiPaneKit.hpp"
#include "hmi/HmiPanels.hpp"

#include "../ui/Icons.hpp"
#include "../ui/widgets/Containers.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"
#include "../project/LibraryCatalog.hpp"
#include "../xls/MacroXls.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

namespace app {

namespace mm = project::macro;
namespace fs = std::filesystem;
using mm::FieldKind;

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kBody{16};
const gfx::FontId kLead{18};
const gfx::FontId kTitle{26};
const gfx::FontId kHeading{17};

std::string lower(std::string_view s) {
    std::string out = mm::foldAccents(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> wrapText(std::string_view text, gfx::FontId font, float width) {
    std::vector<std::string> out;
    std::string line;
    std::size_t i = 0;
    while (i < text.size()) {
        auto sp = text.find_first_of(" \n", i);
        if (sp == std::string_view::npos) sp = text.size();
        const auto word = text.substr(i, sp - i);
        const std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
        if (!line.empty() && ui::measureWidth(candidate, font) > width) {
            out.push_back(line);
            line = std::string(word);
        } else {
            line = candidate;
        }
        if (sp < text.size() && text[sp] == '\n') {
            out.push_back(line);
            line.clear();
        }
        i = sp + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

std::string thousands(std::size_t n) {
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), "\xE2\x80\xAF");
    return s;
}

std::string nowText() {
    const std::time_t t = std::time(nullptr);
    const std::tm* tm = std::localtime(&t);
    char buf[32] = {};
    if (tm) std::strftime(buf, sizeof buf, "%d/%m %H:%M", tm);
    return buf;
}

// Le genre d'un champ, en glyphe (la liste "ce qu'elle va te demander").
HmiGlyph glyphOf(FieldKind k) {
    switch (k) {
        case FieldKind::File: return HmiGlyph::Import;
        case FieldKind::OutputFile: return HmiGlyph::Export;
        case FieldKind::YesNo: return HmiGlyph::Switch;
        case FieldKind::Choice:
        case FieldKind::SheetChoice: return HmiGlyph::Selector;
        case FieldKind::Number: return HmiGlyph::NumericDisplay;
        case FieldKind::List: return HmiGlyph::List;
        case FieldKind::Checks: return HmiGlyph::CheckBox;
        case FieldKind::Task: return HmiGlyph::Clock;
        case FieldKind::Section:
        case FieldKind::Subroutine:
        case FieldKind::Unit: return HmiGlyph::Code;
        case FieldKind::Dfb:
        case FieldKind::Ddt:
        case FieldKind::Type:
        case FieldKind::LibraryItem: return HmiGlyph::Structure;
        case FieldKind::Name: return HmiGlyph::InputField;
        default: return HmiGlyph::Text;
    }
}

// La famille d'une categorie : sa couleur et son glyphe.
int familyOf(const std::string& category) {
    const auto c = lower(category);
    if (c.rfind("importer depuis le classeur", 0) == 0) return 0;
    if (c.rfind("importer", 0) == 0) return 1;
    if (c.rfind("generer", 0) == 0) return 2;
    if (c.rfind("creer", 0) == 0) return 3;
    if (c.rfind("verifier", 0) == 0) return 4;
    return 5;
}

HmiGlyph familyGlyph(int family) {
    switch (family) {
        case 0: return HmiGlyph::Import;
        case 1: return HmiGlyph::Table;
        case 2: return HmiGlyph::Code;
        case 3: return HmiGlyph::Plus;
        case 4: return HmiGlyph::Check;
        default: return HmiGlyph::Structure;
    }
}

// Lot API 2 : les sept pastilles - leur icone et leur couleur.
ui::Icon pipIcon(int k) {
    switch (k) {
        case 0: return ui::Icon::AnimationTable;   // le classeur : une grille
        case 1: return ui::Icon::Document;         // un tableau CSV
        case 2: return ui::Icon::Layers;           // d'autres macros, empilees
        case 3: return ui::Icon::Library;          // la bibliotheque
        case 4: return ui::Icon::Section;          // des sections
        case 5: return ui::Icon::Variable;         // des variables
        default: return ui::Icon::Export;          // un fichier ecrit a cote
    }
}
ui::Tone pipTone(int k) {
    switch (k) {
        case 0: return ui::Tone::Ok;
        case 1: return ui::Tone::Info;
        case 2: return ui::Tone::Accent;
        case 3: return ui::Tone::Warning;
        case 4: return ui::Tone::Family2;
        case 5: return ui::Tone::Family4;
        default: return ui::Tone::Family5;
    }
}

void chip(const ui::PaintContext& ctx, float x, float y, const std::string& text, bool dashed, gfx::Color tone, float& w) {
    w = ui::measureWidth(text, kSmall) + 18.f;
    const gfx::Rect r{x, y, w, 24.f};
    if (!dashed) {
        ctx.r.fillRoundedRect(r, gfx::Color{tone.r, tone.g, tone.b, 40}, 12.f);
        ctx.r.drawText({x + 9.f, y + 4.f}, text, kSmall, ctx.theme.color.text);
    } else {
        // Un contour en pointilles : facultatif.
        const auto c = ctx.theme.color.borderStrong;
        for (float dx = r.x + 10.f; dx < r.x + r.w - 10.f; dx += 6.f) {
            ctx.r.line({dx, r.y}, {std::min(dx + 3.f, r.x + r.w - 10.f), r.y}, c, 1.f);
            ctx.r.line({dx, r.y + r.h}, {std::min(dx + 3.f, r.x + r.w - 10.f), r.y + r.h}, c, 1.f);
        }
        ctx.r.drawText({x + 9.f, y + 4.f}, text, kSmall, ctx.theme.color.textMuted);
    }
}

} // namespace

// ============================================================ l'arbre ====
class MacroTreeModel final : public ui::ITreeModel {
public:
    enum Kind : std::uint32_t { KRoot = 1, KFolder, KMacro, KTrash, KTrashed };
    explicit MacroTreeModel(const MacrosPane& pane) : pane_(pane) {}

    static ui::NodeId pack(Kind k, std::size_t i) { return (static_cast<ui::NodeId>(k) << 32) | static_cast<ui::NodeId>(i & 0xffffffffu); }
    static Kind kindOf(ui::NodeId n) { return static_cast<Kind>(n >> 32); }
    static std::size_t indexOf(ui::NodeId n) { return static_cast<std::size_t>(n & 0xffffffffu); }

    void rebuild() {
        children_.clear();
        const auto& l = pane_.layout();
        const auto build = [&](const std::string& folder) {
            std::vector<ui::NodeId> out;
            for (std::size_t i = 0; i < l.folders.size(); ++i)
                if (mm::sameFolder(mm::folderParent(l.folders[i]), folder)) out.push_back(pack(KFolder, i));
            for (const auto m : l.macrosIn(folder)) out.push_back(pack(KMacro, m));
            return out;
        };
        root_ = build({});
        if (!pane_.trash().empty()) root_.push_back(pack(KTrash, 0));
        for (std::size_t i = 0; i < l.folders.size(); ++i) children_[i] = build(l.folders[i]);
        modelReset->emit();
    }

    [[nodiscard]] ui::NodeId root() const override { return pack(KRoot, 0); }
    [[nodiscard]] std::size_t childCount(ui::NodeId n) const override {
        switch (kindOf(n)) {
            case KRoot: return root_.size();
            case KFolder: {
                const auto it = children_.find(indexOf(n));
                return it == children_.end() ? 0 : it->second.size();
            }
            case KTrash: return pane_.trash().size();
            default: return 0;
        }
    }
    [[nodiscard]] ui::NodeId childAt(ui::NodeId n, std::size_t i) const override {
        switch (kindOf(n)) {
            case KRoot: return i < root_.size() ? root_[i] : ui::kInvalidNode;
            case KFolder: {
                const auto it = children_.find(indexOf(n));
                return it != children_.end() && i < it->second.size() ? it->second[i] : ui::kInvalidNode;
            }
            case KTrash: return pack(KTrashed, i);
            default: return ui::kInvalidNode;
        }
    }
    [[nodiscard]] bool hasChildren(ui::NodeId n) const override { return childCount(n) > 0; }
    [[nodiscard]] std::string text(ui::NodeId n) const override {
        const auto& l = pane_.layout();
        const auto i = indexOf(n);
        switch (kindOf(n)) {
            case KRoot: return "Macros";
            case KFolder: return i < l.folders.size() ? mm::folderLeaf(l.folders[i]) : std::string{};
            case KMacro: return i < l.macros.size() ? l.macros[i].name : std::string{};
            case KTrash: return "Corbeille";
            case KTrashed: return i < pane_.trash().size() ? pane_.trash()[i].name : std::string{};
        }
        return {};
    }
    [[nodiscard]] ui::CellStyle style(ui::NodeId n) const override {
        ui::CellStyle s;
        const auto& l = pane_.layout();
        const auto i = indexOf(n);
        switch (kindOf(n)) {
            case KFolder:
                s.icon = ui::Icon::Folder;
                s.bold = true;
                if (i < l.folders.size()) {
                    s.badge = std::to_string(l.countIn(l.folders[i], true));
                    s.iconTone = ui::familyTone(familyOf(l.folders[i]));
                }
                break;
            case KMacro: {
                if (i >= l.macros.size()) break;
                const auto& name = l.macros[i].name;
                const bool fav = pane_.memory_ && pane_.memory_->favourite(name);
                // Lot API 2 : le glyphe de la famille (le meme que la tuile de la
                // fiche), l'etoile pour une favorite.
                if (fav) s.icon = ui::Icon::StarFilled;
                else s.customIcon = 1 + familyOf(l.macros[i].folder);
                s.iconTone = fav ? ui::Tone::Warning : ui::familyTone(familyOf(l.macros[i].folder));
                if (const auto* spec = pane_.spec(name)) {
                    if (!spec->version.empty()) s.badge = spec->version;
                    for (int k = 0; k < MacrosPane::kPipCount; ++k)
                        s.pips.push_back({pipIcon(k), pipTone(k), MacrosPane::pipOn(*spec, k)});
                }
                break;
            }
            case KTrash:
                s.icon = ui::Icon::Close;
                s.iconTone = ui::Tone::Muted;
                s.fgTone = ui::Tone::Muted;
                s.badge = std::to_string(pane_.trash().size());
                break;
            case KTrashed:
                s.icon = ui::Icon::Document;
                s.iconTone = ui::Tone::Muted;
                s.fgTone = ui::Tone::Muted;
                if (i < pane_.trash().size()) s.badge = pane_.trash()[i].date.substr(0, 10);
                break;
            default: break;
        }
        return s;
    }

private:
    const MacrosPane& pane_;
    std::vector<ui::NodeId> root_;
    std::map<std::size_t, std::vector<ui::NodeId>> children_;
};

// ============================================================== la fiche ====
class MacroCard final : public ui::Widget {
public:
    explicit MacroCard(MacrosPane& pane) : ui::Widget(pane.id() + ".fiche"), pane_(pane) {
        const std::string base = id();
        launch_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Lancer", base + ".lancer")));
        launch_->setStyle(ui::Button::Style::Primary);
        launch_->setIcon(ui::Icon::Play);
        launch_->setTooltip("Le formulaire de la macro, puis l'aper\xC3\xA7u : rien n'est modifi\xC3\xA9 avant Appliquer (Entr\xC3\xA9" "e)");
        edit_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Modifier le code", base + ".code")));
        edit_->setIcon(ui::Icon::Code);
        // Lot API 6 : le mode Modifier - le code, les questions en cartes, l'apercu, l'essai.
        edit_->setTooltip("Le mode Modifier : le code, les questions en cartes (glissables), l'aper\xC3\xA7u du formulaire, "
                          "Essayer (F5), Enregistrer la version suivante");
        help_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Aide", base + ".aide")));
        help_->setIcon(ui::Icon::Info);
        help_->setTooltip("L'aide de la macro : ce qu'elle demande, ce qu'elle fait (F1)");
        fav_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Favorite", base + ".favori")));
        fav_->setIcon(ui::Icon::Star);
        links_ += launch_->clicked->connect([this] { pane_.launch(pane_.current_); });
        links_ += edit_->clicked->connect([this] { pane_.runAction(MacrosPane::AEdit); });
        links_ += help_->clicked->connect([this] { pane_.runAction(MacrosPane::AHelp); });
        links_ += fav_->clicked->connect([this] { pane_.runAction(MacrosPane::AFavourite); });
    }

    void refresh() {
        for (auto* b : profiles_) (void)removeChild(*b);
        profiles_.clear();
        profileNames_.clear();
        const auto* spec = pane_.spec(pane_.current_);
        const bool hasMacro = spec != nullptr;
        for (auto* b : {launch_, edit_, help_, fav_}) b->setVisibility(hasMacro ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        if (hasMacro) {
            const bool project = pane_.hosts_.project && pane_.hosts_.project();
            launch_->setEnabled(project);
            launch_->setTooltip(project ? "Le formulaire de la macro, puis l'aper\xC3\xA7u : rien n'est modifi\xC3\xA9 avant Appliquer (Entr\xC3\xA9" "e)"
                                        : "Ouvre d'abord un projet : une macro travaille sur le projet ouvert");
            const bool fav = pane_.memory_ && pane_.memory_->favourite(pane_.current_);
            fav_->setText(fav ? "Favorite" : "Favorite");
            fav_->setIcon(fav ? ui::Icon::StarFilled : ui::Icon::Star);
            fav_->setTooltip(fav ? "Retirer des favorites" : "Garder dans les favorites (\xE2\x98\x85 dans la liste)");
            if (pane_.memory_)
                for (const auto& p : pane_.memory_->profiles(pane_.current_)) {
                    auto b = std::make_unique<ui::Button>(p, id() + ".profil." + std::to_string(profiles_.size()));
                    b->setIcon(ui::Icon::Play);
                    b->setStyle(ui::Button::Style::Flat);
                    b->setTooltip("Lancer avec les r\xC3\xA9ponses gard\xC3\xA9" "es sous ce nom");
                    auto* raw = &static_cast<ui::Button&>(addChild(std::move(b)));
                    profiles_.push_back(raw);
                    profileNames_.push_back(p);
                    const std::string name = p;
                    links_ += raw->clicked->connect([this, name] { pane_.launch(pane_.current_, {}, name); });
                    if (profiles_.size() >= 6) break;
                }
        }
        invalidateLayout();
        invalidate();
    }

protected:
    void onLayout() override {
        const auto b = bounds();
        const float x = b.x + 28.f;
        const float w = std::min(b.w - 56.f, 1180.f);
        const auto* spec = pane_.spec(pane_.current_);
        float y = b.y + 26.f + 64.f;
        if (spec) {
            summary_ = wrapText(spec->summary, kLead, std::min(w, 900.f));
            y += 24.f * static_cast<float>(summary_.size()) + 16.f;
        }
        buttonsY_ = y;
        float bx = x;
        for (auto* btn : {launch_, edit_, help_, fav_}) {
            if (!btn->visible()) continue;
            const float bw = std::max(110.f, btn->sizeHint().preferred.w + 16.f);
            btn->setBounds({bx, y, bw, 36.f});
            bx += bw + 10.f;
        }
        // Les profils : a droite, sous leur titre (voir onPaint).
        const bool twoColumns = w >= 820.f;
        const float rightX = twoColumns ? x + w * 0.58f : x;
        float py = profilesY_ + 26.f;
        for (auto* p : profiles_) {
            const float pw = std::min(twoColumns ? w * 0.42f : w, p->sizeHint().preferred.w + 20.f);
            p->setBounds({rightX, py, pw, 30.f});
            py += 34.f;
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.panelBg);
        const float x = b.x + 28.f;
        const float w = std::min(b.w - 56.f, 1180.f);
        const auto* spec = pane_.spec(pane_.current_);
        if (!spec) {
            paintFolder(ctx, x, w);
            return;
        }
        const int family = familyOf(spec->category);
        const auto tone = ctx.theme.onSurface(ctx.theme.brand.family[family]);
        // L'en-tete : la tuile, le nom, ce qu'elle est.
        const gfx::Rect tile{x, b.y + 22.f, 56.f, 56.f};
        ctx.r.fillRoundedRect(tile, gfx::Color{tone.r, tone.g, tone.b, 48}, 12.f);
        drawHmiGlyph(ctx.r, familyGlyph(family), {tile.x + 14.f, tile.y + 14.f, 28.f, 28.f}, tone);
        ctx.r.drawText({x + 72.f, b.y + 22.f}, spec->name, kTitle, c.text);
        std::string sub = "version " + (spec->version.empty() ? std::string("?") : spec->version);
        if (!spec->category.empty()) sub += "  \xC2\xB7  " + spec->category;
        if (!spec->launches.empty()) sub += "  \xC2\xB7  encha\xC3\xAEne " + std::to_string(spec->launches.size()) + " macro" + (spec->launches.size() > 1 ? "s" : "");
        if (pane_.memory_ && pane_.memory_->favourite(spec->name)) sub += "  \xC2\xB7  \xE2\x98\x85 favorite";
        ctx.r.drawText({x + 72.f, b.y + 58.f}, sub, kSmall, c.textMuted);
        float y = b.y + 26.f + 64.f;
        for (const auto& l : summary_) {
            ctx.r.drawText({x, y}, l, kLead, c.text);
            y += 24.f;
        }
        y = buttonsY_ + 58.f;
        const bool twoColumns = w >= 820.f;
        const float leftW = twoColumns ? w * 0.54f : w;
        const float rightX = twoColumns ? x + w * 0.58f : x;
        float ly = y;
        const auto title = [&](float tx, float& ty, const std::string& t) {
            ctx.r.drawText({tx, ty}, t, kHeading, c.accent);
            ty += 28.f;
        };
        // Ses etapes.
        if (!spec->launches.empty()) {
            title(x, ly, "Ses \xC3\xA9tapes");
            float cx = x;
            int n = 0;
            for (const auto& m : spec->launches) {
                const std::string t = std::to_string(++n) + "  " + m;
                float cw = 0.f;
                if (cx + ui::measureWidth(t, kSmall) + 18.f > x + leftW) {
                    cx = x;
                    ly += 30.f;
                }
                chip(ctx, cx, ly, t, false, tone, cw);
                cx += cw + 8.f;
            }
            ly += 44.f;
        }
        // Ce qu'elle lit.
        const auto reads = pane_.readsOf(spec->name, false);
        const auto readsOpt = pane_.readsOf(spec->name, true);
        if (!reads.empty() || !readsOpt.empty() || !spec->tables.empty()) {
            title(x, ly, "Ce qu'elle lit");
            float cx = x;
            const auto put = [&](const std::string& t, bool dashed) {
                float cw = 0.f;
                if (cx + ui::measureWidth(t, kSmall) + 18.f > x + leftW) {
                    cx = x;
                    ly += 30.f;
                }
                chip(ctx, cx, ly, t, dashed, c.info, cw);
                cx += cw + 6.f;
            };
            for (const auto& s : reads) put(s, false);
            for (const auto& s : readsOpt) put(s, true);
            for (const auto& t : spec->tables) put("CSV : " + t.name, false);
            ly += 30.f;
            if (!readsOpt.empty()) {
                ctx.r.drawText({x, ly}, "Plein : onglet requis \xC2\xB7 pointill\xC3\xA9 : facultatif, l'\xC3\xA9tape est saut\xC3\xA9" "e s'il manque",
                               kSmall, c.textMuted);
                ly += 20.f;
            }
            ly += 16.f;
        }
        // Lot API 2 : ce qu'elle touche - les pastilles de la liste, en mots.
        {
            std::vector<std::string> said;
            const auto& t = spec->touches;
            if (t.workbook) said.push_back("lit le classeur de l'affaire");
            if (t.csv) said.push_back("lit un tableau CSV");
            if (t.chains) said.push_back("encha\xC3\xAEne " + std::to_string(spec->launches.size()) + " autre" + (spec->launches.size() > 1 ? "s macros" : " macro"));
            if (t.library) said.push_back("importe des types ou des blocs de la biblioth\xC3\xA8que");
            if (t.sections) said.push_back("\xC3\xA9" "crit ou r\xC3\xA9\xC3\xA9" "crit des sections");
            if (t.variables) said.push_back("cr\xC3\xA9" "e des variables");
            if (t.order) said.push_back("range l'ordre d'ex\xC3\xA9" "cution");
            if (t.fileOut) said.push_back("\xC3\xA9" "crit un fichier \xC3\xA0 c\xC3\xB4t\xC3\xA9 du classeur");
            title(x, ly, "Ce qu'elle touche");
            std::string line;
            for (std::size_t k = 0; k < said.size(); ++k) line += (k ? " \xC2\xB7 " : "") + said[k];
            if (line.empty()) line = "Rien dans le projet : elle v\xC3\xA9rifie et le dit.";
            for (const auto& l : wrapText(line, kBody, leftW)) {
                ctx.r.drawText({x, ly}, l, kBody, c.text);
                ly += 22.f;
            }
            ly += 14.f;
        }
        // Ce qu'elle produit.
        if (!spec->produces.empty()) {
            title(x, ly, "Ce qu'elle produit");
            for (const auto& p : spec->produces)
                for (const auto& l : wrapText("\xE2\x80\xA2  " + p, kBody, leftW)) {
                    ctx.r.drawText({x, ly}, l, kBody, c.text);
                    ly += 22.f;
                }
            ly += 14.f;
        }
        // La derniere execution.
        if (pane_.memory_)
            if (const auto* run = pane_.memory_->lastRun(spec->name)) {
                title(x, ly, "Derni\xC3\xA8re ex\xC3\xA9" "cution");
                ctx.r.drawText({x, ly}, run->when + "  \xC2\xB7  " + run->summary, kBody, c.text);
                ly += 34.f;
            }
        // A droite : ce qu'elle va demander, les profils.
        float ry = twoColumns ? y : ly + 10.f;
        title(rightX, ry, "Ce qu'elle va te demander");
        std::size_t shown = 0, total = 0;
        for (const auto& f : spec->fields) {
            if (f.advanced) continue;
            ++total;
            if (shown >= 14) continue;
            ++shown;
            drawHmiGlyph(ctx.r, glyphOf(f.kind), {rightX, ry + 2.f, 16.f, 16.f}, c.textMuted);
            std::string label = mm::labelFor(f, f.key, f.askPrompt);
            const float maxLabel = (twoColumns ? w * 0.42f : w) * 0.55f;
            while (label.size() > 4 && ui::measureWidth(label, kBody) > maxLabel) label = label.substr(0, label.size() - 4) + "\xE2\x80\xA6";
            ctx.r.drawText({rightX + 26.f, ry}, label, kBody, c.text);
            ctx.r.drawText({rightX + 26.f + maxLabel + 12.f, ry + 2.f}, mm::describe(f), kSmall, c.textMuted);
            ry += 26.f;
        }
        if (total == 0) {
            ctx.r.drawText({rightX, ry}, "Rien : elle se lance telle quelle.", kBody, c.textMuted);
            ry += 26.f;
        } else if (total > shown) {
            ctx.r.drawText({rightX, ry}, "\xE2\x80\xA6 et " + std::to_string(total - shown) + " autre(s)", kSmall, c.textMuted);
            ry += 24.f;
        }
        std::size_t advanced = 0;
        for (const auto& f : spec->fields)
            if (f.advanced) ++advanced;
        if (advanced) {
            ctx.r.drawText({rightX, ry}, "+ " + std::to_string(advanced) + " r\xC3\xA9glage(s) avanc\xC3\xA9(s), repli\xC3\xA9(s)", kSmall, c.textMuted);
            ry += 24.f;
        }
        ry += 18.f;
        profilesY_ = ry;
        if (!profiles_.empty()) {
            ctx.r.drawText({rightX, ry}, "Profils de r\xC3\xA9ponses", kHeading, c.accent);
        }
        if (profilesY_ != laidProfilesY_) {
            laidProfilesY_ = profilesY_;
            invalidateLayout();
        }
    }

private:
    void paintFolder(const ui::PaintContext& ctx, float x, float w) {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        const auto& l = pane_.layout();
        const std::string folder = pane_.currentFolder_;
        ctx.r.drawText({x, b.y + 26.f}, folder.empty() ? std::string("Les macros") : mm::folderLeaf(folder), kTitle, c.text);
        const auto count = l.countIn(folder, true);
        ctx.r.drawText({x, b.y + 64.f},
                       std::to_string(count) + " macro" + (count > 1 ? "s" : "") + (folder.empty() ? " dans libs/Macros" : " dans ce dossier")
                           + "  \xC2\xB7  double-clic ou Entr\xC3\xA9" "e pour lancer",
                       kSmall, c.textMuted);
        float y = b.y + 110.f;
        const std::vector<std::string> tips = {
            "Choisis une macro dans la liste : sa fiche dit ce qu'elle lit, ce qu'elle produit et ce qu'elle va te demander.",
            "Lancer ouvre son formulaire : le classeur se glisse, se choisit avec \xE2\x80\xA6 ou se colle (Ctrl+V) ; les t\xC3\xA2" "ches et les sections se choisissent dans le projet.",
            "Rien n'est modifi\xC3\xA9 avant Appliquer, et un seul Ctrl+Z reprend tout ce qu'une macro a fait.",
            "Les dossiers rangent sans rien changer aux noms : glisse une macro (ou un dossier) sur un dossier. Clic droit : nouvelle macro, nouveau dossier, renommer, supprimer.",
            "Une macro supprim\xC3\xA9" "e va dans la Corbeille : Restaurer la remet dans son dossier."};
        for (const auto& t : tips) {
            for (const auto& line : wrapText(t, kBody, std::min(w, 860.f))) {
                ctx.r.drawText({x, y}, line, kBody, c.text);
                y += 22.f;
            }
            y += 12.f;
        }
    }

    MacrosPane& pane_;
    ui::Button* launch_{nullptr};
    ui::Button* edit_{nullptr};
    ui::Button* help_{nullptr};
    ui::Button* fav_{nullptr};
    std::vector<ui::Button*> profiles_;
    std::vector<std::string> profileNames_;
    std::vector<std::string> summary_;
    float buttonsY_{0.f};
    float profilesY_{0.f};
    float laidProfilesY_{-1.f};
    core::ConnectionScope links_;
};

// ========================================================== le formulaire ====
class MacroRunView final : public ui::Widget {
public:
    explicit MacroRunView(MacrosPane& pane) : ui::Widget(pane.id() + ".lancer"), pane_(pane) {
        const std::string base = id();
        form_ = &static_cast<MacroFormView&>(addChild(std::make_unique<MacroFormView>(base + ".formulaire")));
        auto table = std::make_unique<ui::TableView>(base + ".apercu");
        table->setColumns({{"", 190.f}, {"", 900.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        report_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
        const auto button = [&](const std::string& text, const std::string& idPart, ui::Button::Style style) {
            auto b = std::make_unique<ui::Button>(text, base + "." + idPart);
            b->setStyle(style);
            return &static_cast<ui::Button&>(addChild(std::move(b)));
        };
        close_ = button("Fermer", "fermer", ui::Button::Style::Flat);
        close_->setIcon(ui::Icon::Close);
        close_->setTooltip("Revenir \xC3\xA0 la fiche : rien n'est modifi\xC3\xA9");
        cancel_ = button("Annuler", "annuler", ui::Button::Style::Default);
        keep_ = button("Garder ces r\xC3\xA9ponses\xE2\x80\xA6", "profil", ui::Button::Style::Flat);
        keep_->setTooltip("Garder ces r\xC3\xA9ponses sous un nom (un profil) : la fiche le propose ensuite");
        preview_ = button("Aper\xC3\xA7u \xE2\x86\x92", "suivant", ui::Button::Style::Primary);
        back_ = button("\xE2\x86\x90 Les questions", "retour", ui::Button::Style::Default);
        apply_ = button("Appliquer", "appliquer", ui::Button::Style::Primary);
        again_ = button("Relancer", "relancer", ui::Button::Style::Default);
        done_ = button("Fermer", "termine", ui::Button::Style::Primary);
        links_ += close_->clicked->connect([this] { pane_.closeRun(); });
        links_ += cancel_->clicked->connect([this] { pane_.closeRun(); });
        links_ += done_->clicked->connect([this] { pane_.closeRun(); });
        links_ += preview_->clicked->connect([this] { pane_.goPreview(); });
        links_ += back_->clicked->connect([this] { pane_.goBack(); });
        links_ += apply_->clicked->connect([this] { (void)pane_.applyNow(); });
        links_ += again_->clicked->connect([this] { pane_.launch(pane_.current_); });
        links_ += keep_->clicked->connect([this] {
            if (pane_.hosts_.saveProfile && pane_.session_) pane_.hosts_.saveProfile(pane_.current_, pane_.session_->effectiveAnswers());
        });
    }

    MacroFormView& form() { return *form_; }
    ui::TableView& report() { return *report_; }
    ui::Button& applyButton() { return *apply_; }

    void syncButtons() {
        const int s = pane_.step_;
        const auto show = [](ui::Widget* w, bool on) { w->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed); };
        show(form_, s == 1);
        show(report_, s >= 2);
        show(cancel_, s == 1);
        show(keep_, s == 1);
        show(preview_, s == 1);
        show(back_, s == 2);
        show(apply_, s == 2);
        show(again_, s == 3);
        show(done_, s == 3);
        show(close_, s != 3);
        if (auto* session = pane_.session_.get()) {
            const auto& o = session->outcome();
            preview_->setEnabled(o.failure.empty() && !o.stopped);
            std::string text = session->applyText();
            if (text.empty()) text = "Appliquer";
            if (text.rfind("Appliquer", 0) != 0 && text.size() < 70) text = "Appliquer : " + text;
            if (text.size() > 80) text = text.substr(0, 77) + "\xE2\x80\xA6";
            apply_->setText(text);
            const bool nothing = o.report.actions.empty();
            apply_->setEnabled(o.complete && o.failure.empty() && !nothing);
            apply_->setTooltip(nothing ? "La macro n'a rien \xC3\xA0 changer dans le projet : l'aper\xC3\xA7u est son r\xC3\xA9sultat."
                                       : "Un seul Ctrl+Z reprend tout, imports de biblioth\xC3\xA8que compris. Les fichiers \xC3\xA9" "crits \xC3\xA0 c\xC3\xB4t\xC3\xA9 du classeur restent.");
        }
        invalidateLayout();
        invalidate();
    }

protected:
    void onLayout() override {
        const auto b = bounds();
        const float headH = 76.f, footH = 64.f;
        close_->setBounds({b.x + b.w - 120.f, b.y + 20.f, 104.f, 32.f});
        const float bodyTop = b.y + headH;
        const float bodyH = std::max(0.f, b.h - headH - footH);
        form_->setBounds({b.x, bodyTop, b.w, bodyH});
        report_->setBounds({b.x + 20.f, bodyTop + 96.f, b.w - 40.f, std::max(0.f, bodyH - 104.f)});
        const float fy = b.y + b.h - footH + 14.f;
        float rx = b.x + b.w - 20.f;
        const auto right = [&](ui::Button* btn, float minW) {
            if (!btn->visible()) return;
            const float bw = std::max(minW, btn->sizeHint().preferred.w + 20.f);
            rx -= bw;
            btn->setBounds({rx, fy, bw, 36.f});
            rx -= 10.f;
        };
        right(preview_, 150.f);
        right(apply_, 180.f);
        right(done_, 110.f);
        right(cancel_, 100.f);
        right(back_, 150.f);
        right(again_, 110.f);
        right(keep_, 160.f);
        footerRight_ = rx;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.panelBg);
        const auto* spec = pane_.spec(pane_.current_);
        // L'en-tete : la macro et les trois etapes.
        ctx.r.fillRect({b.x, b.y, b.w, 76.f}, c.headerBg);
        ctx.r.line({b.x, b.y + 76.f}, {b.x + b.w, b.y + 76.f}, c.border, 1.f);
        const int family = spec ? familyOf(spec->category) : 5;
        const auto tone = ctx.theme.onSurface(ctx.theme.brand.family[family]);
        ctx.r.fillRoundedRect({b.x + 20.f, b.y + 14.f, 48.f, 48.f}, gfx::Color{tone.r, tone.g, tone.b, 48}, 10.f);
        drawHmiGlyph(ctx.r, familyGlyph(family), {b.x + 32.f, b.y + 26.f, 24.f, 24.f}, tone);
        ctx.r.drawText({b.x + 82.f, b.y + 14.f}, pane_.current_, gfx::FontId{22}, c.text);
        if (spec)
            ctx.r.drawText({b.x + 82.f, b.y + 46.f}, "version " + spec->version + (spec->category.empty() ? "" : "  \xC2\xB7  " + spec->category),
                           kSmall, c.textMuted);
        const char* names[] = {"Les questions", "L'aper\xC3\xA7u", "Appliqu\xC3\xA9"};
        float sx = b.x + std::max(420.f, b.w * 0.42f);
        for (int i = 0; i < 3; ++i) {
            const int k = i + 1;
            const bool done = pane_.step_ > k || (pane_.step_ == 3 && k == 3);
            const bool on = pane_.step_ == k;
            const gfx::Color dot = done ? c.ok : on ? c.accent : c.border;
            ctx.r.fillRoundedRect({sx, b.y + 26.f, 24.f, 24.f}, dot, 12.f);
            const std::string n = done ? "\xE2\x9C\x93" : std::to_string(k);
            const float nw = ui::measureWidth(n, kSmall);
            ctx.r.drawText({sx + 12.f - nw * 0.5f, b.y + 30.f}, n, kSmall, done || on ? c.textInverted : c.textMuted);
            ctx.r.drawText({sx + 32.f, b.y + 29.f}, names[i], kBody, on ? c.text : c.textMuted);
            sx += 32.f + ui::measureWidth(names[i], kBody) + 16.f;
            if (i < 2) {
                ctx.r.line({sx, b.y + 38.f}, {sx + 40.f, b.y + 38.f}, pane_.step_ > k ? c.ok : c.border, 2.f);
                sx += 56.f;
            }
        }
        // Le pied : ce que donnent les reponses, en chiffres.
        const float fy = b.y + b.h - 64.f;
        ctx.r.fillRect({b.x, fy, b.w, 64.f}, c.headerBg);
        ctx.r.line({b.x, fy}, {b.x + b.w, fy}, c.border, 1.f);
        if (auto* session = pane_.session_.get()) {
            const auto& o = session->outcome();
            std::string live;
            gfx::Color tone2 = c.text;
            if (!o.failure.empty()) {
                live = "\xE2\x9C\x97 " + o.failure.substr(0, o.failure.find('\n'));
                tone2 = c.error;
            } else if (o.stopped) {
                live = "La macro attend ses premi\xC3\xA8res r\xC3\xA9ponses.";
                tone2 = c.textMuted;
            } else {
                const auto& t = o.report.tally;
                live = (pane_.step_ == 3 ? "Appliqu\xC3\xA9 : " : "Avec ces r\xC3\xA9ponses : ") + thousands(t.variables) + " variable"
                     + (t.variables > 1 ? "s" : "") + "  \xC2\xB7  " + thousands(t.sections) + " section" + (t.sections > 1 ? "s" : "")
                     + "  \xC2\xB7  " + thousands(t.lines) + " ligne" + (t.lines > 1 ? "s" : "") + " de code  \xC2\xB7  "
                     + std::to_string(o.report.warnings.size()) + " avertissement" + (o.report.warnings.size() > 1 ? "s" : "");
                tone2 = o.report.warnings.empty() ? c.text : c.warning;
            }
            const float maxW = footerRight_ - b.x - 40.f;
            while (live.size() > 8 && ui::measureWidth(live, kBody) > maxW) live = live.substr(0, live.size() - 4) + "\xE2\x80\xA6";
            ctx.r.drawText({b.x + 20.f, fy + 12.f}, live, kBody, tone2);
            char timing[96];
            std::snprintf(timing, sizeof timing, "recalcul\xC3\xA9 en %.2f s (%zu tour%s)", o.seconds, o.rounds, o.rounds > 1 ? "s" : "");
            ctx.r.drawText({b.x + 20.f, fy + 38.f}, pane_.step_ == 1 ? std::string(timing) : std::string("Ctrl+Z reprend tout ce que la macro a fait"),
                           kSmall, c.textMuted);
        }
        // L'apercu : les chiffres en grand, au-dessus du tableau.
        if (pane_.step_ >= 2 && pane_.session_) {
            const auto& o = pane_.step_ == 3 ? pane_.applied_ : pane_.session_->outcome().report;
            const auto& t = o.tally;
            // Un element importe deux fois (une carte, puis une autre) compte une fois.
            std::size_t files = 0, imports = 0;
            std::vector<std::string> seen;
            for (const auto& a : o.actions) {
                if (a.rfind("ecrire le fichier", 0) == 0) ++files;
                if (a.rfind("import ", 0) == 0 || a.rfind("importe ", 0) == 0) {
                    const std::size_t from = a.rfind("importe ", 0) == 0 ? 8 : 7;
                    const auto name = a.substr(from, a.find(' ', from) - from);
                    if (std::find(seen.begin(), seen.end(), name) == seen.end()) { seen.push_back(name); ++imports; }
                }
            }
            struct Cell { std::string n, label; gfx::Color tone; };
            const std::vector<Cell> cells = {
                {thousands(imports), "imports de biblioth\xC3\xA8que", c.text},
                {thousands(t.variables), "variables (" + thousands(t.instances) + " instances)", c.ok},
                {thousands(t.sections), "sections", c.ok},
                {thousands(t.lines), "lignes de code", c.text},
                {thousands(files), "fichier(s) \xC3\xA9" "crit(s)", c.text},
                {std::to_string(o.warnings.size()), "avertissements", o.warnings.empty() ? c.textMuted : c.warning}};
            float cx = b.x + 20.f;
            const float cy = b.y + 76.f + 14.f;
            const float cw = std::max(120.f, (b.w - 40.f) / static_cast<float>(cells.size()));
            for (const auto& cell : cells) {
                ctx.r.drawText({cx, cy}, cell.n, gfx::FontId{30}, cell.tone);
                ctx.r.drawText({cx, cy + 40.f}, cell.label, kSmall, c.textMuted);
                cx += cw;
            }
            if (pane_.step_ == 3) {
                const std::string msg = "\xE2\x9C\x93 " + pane_.appliedSummary_;
                ctx.r.drawText({b.x + 20.f, b.y + 76.f + 66.f}, msg, kSmall, c.ok);
            }
        }
    }

private:
    MacrosPane& pane_;
    MacroFormView* form_{nullptr};
    ui::TableView* report_{nullptr};
    ui::Button *close_{nullptr}, *cancel_{nullptr}, *keep_{nullptr}, *preview_{nullptr}, *back_{nullptr}, *apply_{nullptr},
        *again_{nullptr}, *done_{nullptr};
    float footerRight_{0.f};
    core::ConnectionScope links_;
};

// =============================================================== le volet ====
// ============================================================ la legende ====
//  Lot API 2 : sous la liste, repliable - les familles (le glyphe et la couleur
//  de chaque dossier) et les sept pastilles, avec leurs mots. Un clic sur une
//  pastille ne garde que les macros qui l'ont ; un second clic rend tout.
class MacroLegend final : public ui::Widget {
public:
    explicit MacroLegend(MacrosPane& pane) : ui::Widget(pane.id() + ".legende"), pane_(pane) {}

    [[nodiscard]] float wantedHeight() const noexcept { return pane_.legendOpen_ ? 196.f : 30.f; }
    [[nodiscard]] gfx::Rect pipRect(int k) const {
        return k >= 0 && k < MacrosPane::kPipCount ? pipRects_[static_cast<std::size_t>(k)] : gfx::Rect{};
    }
    [[nodiscard]] gfx::Rect headerRect() const { return {bounds().x, bounds().y, bounds().w, 30.f}; }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.headerBg);
        ctx.r.fillRect({b.x, b.y, b.w, 1.f}, c.border);
        ctx.r.drawText({b.x + 12.f, b.y + 8.f}, "L\xC3\x89GENDE", kSmall, c.textMuted);
        const std::string state = pane_.pipFilter_ >= 0 ? "filtre : " + std::string(MacrosPane::pipLabel(pane_.pipFilter_)) + "  \xE2\x9C\x95"
                                                        : (pane_.legendOpen_ ? "replier \xE2\x96\xB4" : "d\xC3\xA9plier \xE2\x96\xBE");
        const float sw = ctx.r.measure(state, kSmall).width;
        ctx.r.drawText({b.right() - 12.f - sw, b.y + 8.f}, state, kSmall, pane_.pipFilter_ >= 0 ? c.accent : c.textMuted);
        for (auto& r : pipRects_) r = {};
        if (!pane_.legendOpen_) return;
        const float colW = (b.w - 24.f) * 0.5f;
        // les familles
        static const char* families[] = {"Importer depuis le classeur", "Importer depuis un CSV", "G\xC3\xA9n\xC3\xA9rer", "Cr\xC3\xA9" "er",
                                         "V\xC3\xA9rifier et ranger", "Biblioth\xC3\xA8que"};
        for (int f = 0; f < 6; ++f) {
            const float x = b.x + 12.f + static_cast<float>(f % 2) * colW;
            const float y = b.y + 30.f + static_cast<float>(f / 2) * 20.f;
            const auto tone = ctx.theme.onSurface(ctx.theme.brand.family[f]);
            drawHmiGlyph(ctx.r, familyGlyph(f), {x, y + 1.f, 15.f, 15.f}, tone);
            ctx.r.drawText({x + 22.f, y}, families[f], kSmall, c.text);
        }
        // les pastilles
        for (int k = 0; k < MacrosPane::kPipCount; ++k) {
            const float x = b.x + 12.f + static_cast<float>(k % 2) * colW;
            const float y = b.y + 96.f + static_cast<float>(k / 2) * 22.f;
            const gfx::Rect box{x, y, 18.f, 18.f};
            const auto tone = ctx.theme.tone(pipTone(k), c.textMuted);
            const bool on = pane_.pipFilter_ < 0 || pane_.pipFilter_ == k;
            ctx.r.fillRoundedRect(box, tone.withAlpha(ctx.theme.isDark() ? 64 : 40), 4.f);
            ui::drawIcon(ctx.r, pipIcon(k), {box.x + 3.f, box.y + 3.f, 12.f, 12.f}, ctx.theme.onSurface(tone));
            ctx.r.drawText({x + 24.f, y + 1.f}, std::string(MacrosPane::pipLabel(k)), kSmall, on ? c.text : c.textMuted);
            pipRects_[static_cast<std::size_t>(k)] = {x - 2.f, y - 2.f, colW - 8.f, 22.f};
            if (pane_.pipFilter_ == k) ctx.r.strokeRect(pipRects_[static_cast<std::size_t>(k)], c.accent, 1.f);
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        // -1 : l'en-tete, 0 a 6 : une pastille. Un clic = l'appui et le relacher
        // au meme endroit (le relacher d'un dialogue ferme au-dessus ne compte pas).
        const auto at = [&](gfx::Point p) {
            if (headerRect().contains(p)) return -1;
            for (int k = 0; k < MacrosPane::kPipCount; ++k)
                if (pipRect(k).contains(p)) return k;
            return -2;
        };
        if (const auto* m = std::get_if<ui::MouseUp>(&ev); m && m->button == ui::MouseButton::Left) {
            const int h = at(m->pos);
            const bool click = h != -2 && h == pressed_;
            pressed_ = -2;
            if (click && h == -1) {
                if (pane_.pipFilter_ >= 0) pane_.setPipFilter(-1);
                else pane_.setLegendOpen(!pane_.legendOpen_);
                return ui::EventResult::Consumed;
            }
            if (click) {
                pane_.setPipFilter(pane_.pipFilter_ == h ? -1 : h);
                return ui::EventResult::Consumed;
            }
        }
        if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && m->button == ui::MouseButton::Left && bounds().contains(m->pos)) {
            pressed_ = at(m->pos);
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    MacrosPane& pane_;
    mutable std::array<gfx::Rect, MacrosPane::kPipCount> pipRects_{};
    int pressed_{-2};       // sous l'appui : -1 l'en-tete, 0 a 6 une pastille, -2 rien
};

std::string_view MacrosPane::pipLabel(int pip) noexcept {
    switch (pip) {
        case 0: return "lit le classeur";
        case 1: return "lit un tableau CSV";
        case 2: return "encha\xC3\xAEne d'autres macros";
        case 3: return "importe de la biblioth\xC3\xA8que";
        case 4: return "\xC3\xA9" "crit des sections";
        case 5: return "cr\xC3\xA9" "e des variables";
        case 6: return "\xC3\xA9" "crit un fichier \xC3\xA0 c\xC3\xB4t\xC3\xA9";
        default: return {};
    }
}

bool MacrosPane::pipOn(const mm::MacroSpec& spec, int pip) noexcept {
    const auto& t = spec.touches;
    switch (pip) {
        case 0: return t.workbook;
        case 1: return t.csv;
        case 2: return t.chains;
        case 3: return t.library;
        case 4: return t.sections;
        case 5: return t.variables;
        case 6: return t.fileOut;
        default: return false;
    }
}

void MacrosPane::setPipFilter(int pip) {
    pipFilter_ = pip >= 0 && pip < kPipCount ? pip : -1;
    applyFilter();
    // La liste repart du haut : filtree, elle est plus courte, et le defilement
    // d'avant cachait ses premieres lignes.
    if (const auto first = tree_->visibleNodes(); !first.empty()) tree_->ensureVisible(first.front());
    if (legend_) legend_->invalidate();
    tree_->invalidate();
}

void MacrosPane::setLegendOpen(bool open) {
    legendOpen_ = open;
    invalidateLayout();
    if (legend_) legend_->invalidate();
}

gfx::Rect MacrosPane::legendPipRect(int pip) const { return legend_ ? legend_->pipRect(pip) : gfx::Rect{}; }

MacrosPane::MacrosPane(std::string id, std::string libsRoot, mm::MacroMemory* memory)
    : ui::Widget(std::move(id)), libsRoot_(std::move(libsRoot)), memory_(memory) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".barre");
    tools->add(ANewMacro, HmiGlyph::Plus, "Nouvelle macro, dans le dossier choisi, depuis un mod\xC3\xA8le", "Macro");
    tools->add(ANewFolder, HmiGlyph::Plus, "Nouveau dossier (sans effet sur les noms) : glisse des macros dessus", "Dossier");
    tools->separator();
    tools->add(ADuplicate, HmiGlyph::Duplicate, "Dupliquer la macro choisie, sous un autre nom", "Dupliquer");
    tools->add(ARename, HmiGlyph::InputField, "Renommer (F2) : la macro, et les RunMacro qui l'appellent ; ou le dossier", "Renommer");
    tools->add(ADelete, HmiGlyph::Delete, "Supprimer (Suppr) : la macro va dans la corbeille, Restaurer la remet", "Supprimer");
    tools->add(ARestore, HmiGlyph::Undo, "Restaurer la macro de la corbeille, dans son dossier", "Restaurer");
    tools->separator();
    tools->add(AFavourite, HmiGlyph::Star, "Favorite : \xE2\x98\x85 dans la liste, et le filtre Favorites", "");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(ADuplicate, [this] { return !selectedMacro().empty(); });
    tools_->setEnabledWhen(ARename, [this] { return !selectedMacro().empty() || !selectedFolder().empty(); });
    tools_->setEnabledWhen(ADelete, [this] { return !selectedMacro().empty() || !selectedFolder().empty() || trashSelected(); });
    tools_->setVisibleWhen(ARestore, [this] { return trashSelected(); });
    tools_->setEnabledWhen(AFavourite, [this] { return !selectedMacro().empty(); });
    tools_->setCheckedWhen(AFavourite, [this] { return memory_ && memory_->favourite(selectedMacro()); });
    links_ += tools_->triggered->connect([this](int a) { runAction(a); });

    auto search = std::make_unique<ui::InputText>(base + ".recherche");
    search->setPlaceholder("Rechercher une macro\xE2\x80\xA6");
    search_ = &static_cast<ui::InputText&>(addChild(std::move(search)));
    links_ += search_->textChanged->connect([this](const std::string&) { applyFilter(); });
    for (const char* label : {"Toutes", "\xE2\x98\x85 Favorites", "R\xC3\xA9" "centes"}) {
        auto t = std::make_unique<ui::ToggleButton>(label, base + ".filtre." + std::to_string(chips_.size()));
        auto* raw = &static_cast<ui::ToggleButton&>(addChild(std::move(t)));
        const int index = static_cast<int>(chips_.size());
        chips_.push_back(raw);
        // Des boutons radio : setChecked emet toggled, d'ou le verrou. Recliquer
        // le filtre en cours le laisse coche.
        links_ += raw->toggled->connect([this, index](bool on) {
            if (syncingChips_) return;
            syncingChips_ = true;
            if (on) chip_ = index;
            for (std::size_t i = 0; i < chips_.size(); ++i) chips_[i]->setChecked(static_cast<int>(i) == chip_);
            syncingChips_ = false;
            applyFilter();
        });
    }
    syncingChips_ = true;
    chips_.front()->setChecked(true);
    syncingChips_ = false;

    auto tree = std::make_unique<ui::TreeView>(base + ".liste");
    tree->setShowRootNode(false);
    tree->setSelectionMode(ui::SelectionMode::Extended);
    tree_ = &static_cast<ui::TreeView&>(addChild(std::move(tree)));
    treeModel_ = std::make_shared<MacroTreeModel>(*this);
    tree_->setModel(treeModel_);
    // Lot API 2 : le glyphe de la famille de chaque macro (CellStyle::customIcon).
    tree_->setIconPainter([](gfx::IRenderer& r, int icon, const gfx::Rect& box, gfx::Color color) {
        drawHmiGlyph(r, familyGlyph(icon - 1), box, color);
    });
    legend_ = &static_cast<MacroLegend&>(addChild(std::make_unique<MacroLegend>(*this)));
    links_ += tree_->selectionChanged->connect([this](ui::NodeId n) { onTreeSelection(n); });
    links_ += tree_->activated->connect([this](ui::NodeId n) { onTreeActivated(n); });
    links_ += tree_->contextMenuRequested->connect([this](ui::NodeId n, gfx::Point at) { showContextMenu(n, at); });

    card_ = &static_cast<MacroCard&>(addChild(std::make_unique<MacroCard>(*this)));
    run_ = &static_cast<MacroRunView&>(addChild(std::make_unique<MacroRunView>(*this)));
    run_->setVisibility(ui::Visibility::Collapsed);

    // Lot API 6 : Utiliser / Modifier, en haut a droite (au-dessus de la fiche,
    // ou au bout de la barre de l'editeur).
    auto mode = std::make_unique<macroui::Segmented>(base + ".mode");
    mode->setOptions({"Utiliser", "Modifier"});
    mode->setSelected(0);
    mode->setTooltip("Utiliser : la fiche et le formulaire. Modifier : le code, les questions en cartes, l'aper\xC3\xA7u, l'essai (F5).");
    mode_ = &static_cast<macroui::Segmented&>(addChild(std::move(mode)));
    links_ += mode_->selectionChanged->connect([this](int i) {
        if (i == 1) {
            if (!current_.empty()) edit(current_);
            else mode_->setSelected(0);
        } else {
            use();
        }
    });

    links_ += run_->form().changed->connect(
        [this](const std::string& key, const std::string& value, bool immediate) { onFieldChanged(key, value, immediate); });

    // Le menu contextuel de la liste, au-dessus de tout.
    auto menu = std::make_unique<ui::PopupMenu>(base + ".menu");
    auto* popup = &static_cast<ui::PopupMenu&>(addChild(std::move(menu)));
    contextMenu_ = popup;
    links_ += popup->itemChosen->connect([this](int a) { runAction(a); });

    wireDrag();
    reload();
}

MacrosPane::~MacrosPane() = default;

MacroFormView* MacrosPane::form() const noexcept { return run_ ? &run_->form() : nullptr; }

const mm::MacroSpec* MacrosPane::spec(const std::string& macro) const {
    for (const auto& [name, s] : specs_)
        if (lower(name) == lower(macro)) return &s;
    return nullptr;
}

// --------------------------------------------------------------- relire ----
void MacrosPane::reload() {
    library_ = std::make_unique<project::SharedLibrary>(libsRoot_);
    (void)library_->scan();
    (void)library_->ensureDefaultMacros();
    specs_.clear();
    std::vector<std::pair<std::string, std::string>> known;
    for (const auto& item : library_->items()) {
        if (item.kind != project::LibraryItemKind::Macro) continue;
        auto spec = mm::parseMacroSpec(readFile(item.path), item.name);
        known.emplace_back(item.name, spec.category);
        specs_[item.name] = std::move(spec);
    }
    folders_ = mm::MacroFolders(library_->macroFoldersFile());
    (void)folders_.load();
    folders_.setMacros(known);
    layout_ = folders_.arrange();
    trash_ = library_->trashedMacros();
    rebuildTree();
    refreshCard();
}

void MacrosPane::rebuildTree() {
    // Ce qui etait deplie le reste (par chemin : les index changent).
    std::set<std::string> expanded;
    for (const auto n : tree_->visibleNodes())
        if (MacroTreeModel::kindOf(n) == MacroTreeModel::KFolder && tree_->isExpanded(n))
            expanded.insert(lower(treeModel_->text(n)));
    const bool first = !builtOnce_;
    treeModel_->rebuild();
    for (std::size_t i = 0; i < layout_.folders.size(); ++i) {
        const auto node = MacroTreeModel::pack(MacroTreeModel::KFolder, i);
        if (first || expanded.count(lower(mm::folderLeaf(layout_.folders[i])))) tree_->expand(node);
    }
    builtOnce_ = true;
    applyFilter();
}

void MacrosPane::applyFilter() {
    if (tree_ == nullptr || !treeModel_) return;          // pendant la construction
    const std::string term = search_ ? lower(search_->text()) : std::string{};
    if (term.empty() && chip_ == 0 && pipFilter_ < 0) {
        tree_->setFilter(nullptr);
        return;
    }
    tree_->setFilter([this, term](ui::NodeId n) {
        const auto kind = MacroTreeModel::kindOf(n);
        if (kind != MacroTreeModel::KMacro) return false;
        const auto i = MacroTreeModel::indexOf(n);
        if (i >= layout_.macros.size()) return false;
        const auto& name = layout_.macros[i].name;
        if (chip_ == 1 && !(memory_ && memory_->favourite(name))) return false;
        if (chip_ == 2 && !(memory_ && memory_->lastRun(name))) return false;
        if (pipFilter_ >= 0) {                                  // lot API 2
            const auto* s = spec(name);
            if (!s || !pipOn(*s, pipFilter_)) return false;
        }
        if (term.empty()) return true;
        // Lot recherche : la recherche de toutes les listes - chaque mot (ou
        // "phrase") dans le nom, le resume (la description), la categorie ou
        // ce qu'elle produit ; aucun -mot exclu ; sans casse ni accents.
        const ui::SearchQuery query(term);
        const auto* s = spec(name);
        if (!s) return query.matches({name});
        std::string produces;
        for (const auto& x : s->produces) produces += x + " ";
        return query.matches({name, s->summary, s->category, s->applyText, produces});
    });
    for (std::size_t i = 0; i < layout_.folders.size(); ++i) tree_->expand(MacroTreeModel::pack(MacroTreeModel::KFolder, i));
}

// ------------------------------------------------------------ la selection ----
std::string MacrosPane::selectedMacro() const {
    const auto n = tree_->currentNode();
    if (MacroTreeModel::kindOf(n) != MacroTreeModel::KMacro) return {};
    const auto i = MacroTreeModel::indexOf(n);
    return i < layout_.macros.size() ? layout_.macros[i].name : std::string{};
}

std::string MacrosPane::selectedFolder() const {
    const auto n = tree_->currentNode();
    if (MacroTreeModel::kindOf(n) != MacroTreeModel::KFolder) return {};
    const auto i = MacroTreeModel::indexOf(n);
    return i < layout_.folders.size() ? layout_.folders[i] : std::string{};
}

bool MacrosPane::trashSelected() const {
    const auto k = MacroTreeModel::kindOf(tree_->currentNode());
    return k == MacroTreeModel::KTrashed;
}

ui::NodeId MacrosPane::nodeOf(const std::string& path) const {
    // "Dossier/Sous-dossier/Macro" ou "Macro", ou "Corbeille/Macro".
    const auto slash = path.rfind('/');
    const std::string leaf = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::string parent = slash == std::string::npos ? std::string{} : path.substr(0, slash);
    if (lower(parent) == "corbeille")
        for (std::size_t i = 0; i < trash_.size(); ++i)
            if (lower(trash_[i].name) == lower(leaf)) return MacroTreeModel::pack(MacroTreeModel::KTrashed, i);
    if (lower(path) == "corbeille" && !trash_.empty()) return MacroTreeModel::pack(MacroTreeModel::KTrash, 0);
    for (std::size_t i = 0; i < layout_.macros.size(); ++i)
        if (lower(layout_.macros[i].name) == lower(leaf) && (parent.empty() || mm::sameFolder(layout_.macros[i].folder, parent)))
            return MacroTreeModel::pack(MacroTreeModel::KMacro, i);
    for (std::size_t i = 0; i < layout_.folders.size(); ++i)
        if (mm::sameFolder(layout_.folders[i], path) || (parent.empty() && lower(mm::folderLeaf(layout_.folders[i])) == lower(leaf)))
            return MacroTreeModel::pack(MacroTreeModel::KFolder, i);
    return ui::kInvalidNode;
}

void MacrosPane::onTreeSelection(ui::NodeId n) {
    const auto kind = MacroTreeModel::kindOf(n);
    const auto i = MacroTreeModel::indexOf(n);
    if (kind == MacroTreeModel::KMacro && i < layout_.macros.size()) {
        if (step_ != 0 && layout_.macros[i].name != current_) return;   // un formulaire ouvert reste
        current_ = layout_.macros[i].name;
        currentFolder_.clear();
    } else if (kind == MacroTreeModel::KFolder && i < layout_.folders.size()) {
        if (step_ != 0) return;
        current_.clear();
        currentFolder_ = layout_.folders[i];
    } else if (kind == MacroTreeModel::KTrashed || kind == MacroTreeModel::KTrash) {
        if (step_ != 0) return;
        current_.clear();
        currentFolder_.clear();
    }
    refreshCard();
}

void MacrosPane::onTreeActivated(ui::NodeId n) {
    if (MacroTreeModel::kindOf(n) == MacroTreeModel::KMacro) {
        const auto i = MacroTreeModel::indexOf(n);
        if (i < layout_.macros.size()) launch(layout_.macros[i].name);
    } else if (MacroTreeModel::kindOf(n) == MacroTreeModel::KTrashed) {
        runAction(ARestore);
    }
}

void MacrosPane::select(const std::string& macro) {
    const auto n = nodeOf(macro);
    if (n == ui::kInvalidNode) return;
    tree_->ensureVisible(n);
    // Le noeud devient courant comme d'un clic : passer par la selection.
    current_ = macro;
    for (const auto& m : layout_.macros)
        if (lower(m.name) == lower(macro)) current_ = m.name;
    currentFolder_.clear();
    selectNode(n);
    refreshCard();
}

void MacrosPane::selectFolder(const std::string& folder) {
    const auto n = nodeOf(folder);
    if (n == ui::kInvalidNode) return;
    tree_->ensureVisible(n);
    current_.clear();
    currentFolder_ = folder;
    selectNode(n);
    refreshCard();
}

void MacrosPane::selectNode(ui::NodeId n) {
    gfx::Rect r;
    tree_->layout();
    if (tree_->rowRect(n, r)) {
        // Un clic sur la ligne : la vue garde sa propre notion du noeud courant.
        const gfx::Point p{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
        (void)tree_->dispatch(ui::MouseDown{p, ui::MouseButton::Left, 1, {}});
        (void)tree_->dispatch(ui::MouseUp{p, ui::MouseButton::Left, {}});
    }
}

void MacrosPane::refreshCard() {
    const bool running = step_ != 0;
    card_->setVisibility(running || editing() ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    run_->setVisibility(running && !editing() ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (mode_) mode_->setVisibility(editing() || (!running && !current_.empty()) ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    card_->refresh();
    if (running) run_->syncButtons();
    invalidateLayout();
    invalidate();
}

// ---------------------------------------------------------------- lancer ----
void MacrosPane::launch(const std::string& macro, std::string source, const std::string& profile) {
    if (editing()) use();          // lot API 6 : Utiliser, depuis l'editeur
    std::string name = macro;
    for (const auto& m : layout_.macros)
        if (lower(m.name) == lower(macro)) name = m.name;
    auto project = hosts_.project ? hosts_.project() : nullptr;
    if (!project) {
        say("Lancer " + name + " : ouvre d'abord un projet (une macro travaille sur le projet ouvert)", true);
        return;
    }
    if (source.empty()) source = library_->macroSource(name);
    if (source.empty()) {
        say("la macro " + name + " est introuvable ou vide", true);
        return;
    }
    current_ = name;
    session_ = std::make_unique<mm::MacroSession>(project, libsRoot_, name, std::move(source));
    if (memory_) {
        const std::string key = hosts_.projectKey ? hosts_.projectKey() : std::string{};
        session_->preload(profile.empty() ? memory_->answers(key, name) : memory_->profile(name, profile));
    }
    if (const auto* s = spec(name)) {
        std::vector<std::string> groups;
        for (const auto& g : s->groups) groups.push_back(g.name);
        // Les groupes des macros qu'elle lance, apres les siens.
        for (const auto& launched : s->launches)
            if (const auto* ls = spec(launched))
                for (const auto& g : ls->groups)
                    if (std::find(groups.begin(), groups.end(), g.name) == groups.end()) groups.push_back(g.name);
        run_->form().setGroupOrder(std::move(groups));
        run_->form().setGroupKeys(s->groups);      // lot API 6 : l'ordre dans chaque groupe
    }
    // Ce que le formulaire demande au monde.
    MacroFormHosts h;
    h.names = [this](FieldKind k) { return namesOf(k); };
    h.members = [this](const std::string& type) {
        std::vector<std::pair<std::string, std::string>> out;
        auto p = hosts_.project ? hosts_.project() : nullptr;
        if (p && !type.empty())
            for (const auto& pou : p->pous) {
                if (pou.kind != domain::PouKind::FunctionBlockType || lower(p->strings.text(pou.name)) != lower(type)) continue;
                for (const auto index : pou.parameters)
                    if (index < p->variables.size()) {
                        const auto& v = p->variables[index];
                        if (v.scope == domain::VariableScope::Input || v.scope == domain::VariableScope::InOut)
                            out.emplace_back(std::string(p->strings.text(v.name)), std::string(p->strings.text(v.type.name)));
                    }
                return out;
            }
        // Absent du projet : la bibliotheque.
        if (library_ && !type.empty())
            for (const auto& item : library_->items())
                if (item.kind == project::LibraryItemKind::FunctionBlock && lower(item.name) == lower(type)) {
                    const auto entry = project::parseLibraryFile(readFile(item.path), item.fileName());
                    for (const auto& d : entry.declarations)
                        if (d.scope == "Input" || d.scope == "InOut") out.emplace_back(d.name, d.type);
                }
        return out;
    };
    h.sheets = [this] {
        std::vector<std::string> out;
        const auto path = workbookPath();
        std::string why;
        if (!path.empty() && xls::MacroXls::instance().openWorkbook(path, why) >= 0) out = xls::MacroXls::instance().sheetNames();
        return out;
    };
    h.sheetValues = [this](const std::string& sheet, const std::string& column, const std::string& labelColumn) {
        std::vector<std::pair<std::string, std::string>> out;
        const auto path = workbookPath();
        std::string why;
        auto& xl = xls::MacroXls::instance();
        if (path.empty() || xl.openWorkbook(path, why) < 0) return out;
        const int handle = xl.openSheet(sheet);
        if (handle < 0) return out;
        for (int row = 0; row < xl.rowCount(handle); ++row) {
            auto v = xl.cell(handle, row, column);
            if (v.empty()) continue;
            out.emplace_back(v, labelColumn.empty() ? std::string{} : xl.cell(handle, row, labelColumn));
        }
        return out;
    };
    h.outdated = [this] {
        std::vector<std::pair<std::string, std::string>> out;
        auto p = hosts_.project ? hosts_.project() : nullptr;
        if (!p || !library_) return out;
        for (const auto& n : library_->outdated(*p))
            out.emplace_back(n.name, "projet " + n.projectVersion + " \xE2\x86\x92 biblioth\xC3\xA8que " + n.libraryVersion);
        return out;
    };
    h.recentFiles = [this] { return memory_ ? memory_->recentFiles() : std::vector<std::string>{}; };
    h.fileInfo = [](const std::string& path, std::string& text) {
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            text = "introuvable : " + path;
            return false;
        }
        const auto size = fs::file_size(path, ec);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.0f Ko", static_cast<double>(size) / 1024.0);
        text = fs::path(path).filename().string() + "  \xC2\xB7  " + buf;
        const auto ext = lower(fs::path(path).extension().string());
        if (ext == ".xlsx" || ext == ".xlsm") {
            std::string why;
            if (xls::MacroXls::instance().openWorkbook(path, why) < 0) {
                text = "illisible : " + why;
                return false;
            }
            text += "  \xC2\xB7  " + std::to_string(xls::MacroXls::instance().sheetNames().size()) + " onglets";
        }
        return true;
    };
    h.outputPath = [this](const std::string& file) {
        const auto wb = workbookPath();
        if (wb.empty()) return std::string("\xC3\xA0 c\xC3\xB4t\xC3\xA9 du classeur");
        return "\xC3\xA0 c\xC3\xB4t\xC3\xA9 du classeur : " + (fs::path(wb).parent_path() / file).string();
    };
    h.lastWorkbook = [] { return xls::MacroXls::instance().loadedWorkbook(); };
    run_->form().setHosts(std::move(h));
    run_->form().show({}, now_);
    step_ = 1;
    doRefresh(false);
    refreshCard();
    say(name + " : r\xC3\xA9ponds aux questions, l'aper\xC3\xA7u suit. Rien n'est modifi\xC3\xA9 avant Appliquer.");
}

void MacrosPane::closeRun() {
    session_.reset();
    step_ = 0;
    refreshPending_ = false;
    run_->form().show({}, now_);
    refreshCard();
}

std::string MacrosPane::workbookPath() const {
    if (session_)
        for (const auto& f : session_->outcome().fields)
            if (f.spec.kind == FieldKind::File) {
                const auto& exts = f.spec.extensions;
                if (std::find(exts.begin(), exts.end(), ".xlsm") != exts.end() || std::find(exts.begin(), exts.end(), ".xlsx") != exts.end())
                    return f.value.empty() ? xls::MacroXls::instance().loadedWorkbook() : f.value;
            }
    return xls::MacroXls::instance().loadedWorkbook();
}

std::vector<std::pair<std::string, std::string>> MacrosPane::namesOf(FieldKind kind) const {
    std::vector<std::pair<std::string, std::string>> out;
    auto p = hosts_.project ? hosts_.project() : nullptr;
    const auto name = [&](domain::SymbolId id) { return p ? std::string(p->strings.text(id)) : std::string{}; };
    switch (kind) {
        case FieldKind::Task:
            if (p)
                for (const auto& t : p->tasks)
                    out.emplace_back(name(t.name), (t.period ? std::to_string(t.period) + " ms \xC2\xB7 " : std::string{})
                                                      + std::to_string(t.sections.size()) + " section(s)");
            break;
        case FieldKind::Section:
            if (p)
                for (const auto& s : p->sections)
                    if (!s.isSubroutine) out.emplace_back(name(s.name), std::to_string(s.lineCount) + (s.lineCount == 1 ? " ligne" : " lignes"));   // 1.11 (R111, T3-11)
            break;
        case FieldKind::Subroutine:
            if (p)
                for (const auto& s : p->sections)
                    if (s.isSubroutine) out.emplace_back(name(s.name), "sous-routine");
            break;
        case FieldKind::Unit:
            if (p)
                for (const auto& pou : p->pous)
                    if (pou.kind == domain::PouKind::ProgramUnit) out.emplace_back(name(pou.name), "unit\xC3\xA9");
            break;
        case FieldKind::Dfb:
        case FieldKind::Ddt:
        case FieldKind::Type:
        case FieldKind::LibraryItem: {
            std::set<std::string> seen;
            const bool dfb = kind != FieldKind::Ddt, ddt = kind != FieldKind::Dfb;
            if (p && kind != FieldKind::LibraryItem) {
                if (dfb)
                    for (const auto& pou : p->pous)
                        if (pou.kind == domain::PouKind::FunctionBlockType && seen.insert(lower(name(pou.name))).second)
                            out.emplace_back(name(pou.name), "DFB du projet" + (pou.version.empty() ? std::string{} : " v" + pou.version));
                if (ddt)
                    for (const auto& d : p->derivedTypes)
                        if (seen.insert(lower(name(d.name))).second)
                            out.emplace_back(name(d.name), "DDT du projet" + (d.version.empty() ? std::string{} : " v" + d.version));
            }
            if (library_)
                for (const auto& item : library_->items()) {
                    if (item.kind == project::LibraryItemKind::Macro) continue;
                    const bool isDfb = item.kind == project::LibraryItemKind::FunctionBlock;
                    if ((isDfb && !dfb) || (!isDfb && !ddt)) continue;
                    if (!seen.insert(lower(item.name)).second) continue;
                    out.emplace_back(item.name, std::string(isDfb ? "DFB" : "DDT") + " de la biblioth\xC3\xA8que v" + item.version);
                }
            if (kind == FieldKind::Type)
                for (const char* t : {"BOOL", "INT", "DINT", "UINT", "UDINT", "REAL", "TIME", "STRING", "WORD", "DWORD", "BYTE"})
                    out.emplace_back(t, "type \xC3\xA9l\xC3\xA9mentaire");
            break;
        }
        case FieldKind::Variable:
            if (p)
                for (const auto& v : p->variables)
                    if (v.scope == domain::VariableScope::Global) out.emplace_back(name(v.name), name(v.type.name));
            break;
        case FieldKind::Macro:
            for (const auto& m : layout_.macros) out.emplace_back(m.name, "macro");
            break;
        default: break;
    }
    return out;
}

std::vector<std::string> MacrosPane::readsOf(const std::string& macro, bool optional) const {
    // Les siens, puis ceux des macros qu'elle lance (sur deux niveaux).
    std::vector<std::string> out, required;
    std::set<std::string> visited;
    std::function<void(const std::string&, int)> walk = [&](const std::string& m, int depth) {
        if (depth > 3 || !visited.insert(lower(m)).second) return;
        const auto* s = spec(m);
        if (!s) return;
        for (const auto& r : s->reads)
            if (std::find(required.begin(), required.end(), r) == required.end()) required.push_back(r);
        if (optional)
            for (const auto& r : s->readsOptional)
                if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
        if (!s->readsDeclared)
            for (const auto& l : s->launches) walk(l, depth + 1);
    };
    walk(macro, 0);
    if (!optional) return required;
    std::erase_if(out, [&](const std::string& r) { return std::find(required.begin(), required.end(), r) != required.end(); });
    return out;
}

// ------------------------------------------------------------- l'apercu ----
void MacrosPane::onFieldChanged(const std::string& key, const std::string& value, bool immediate) {
    if (!session_) return;
    if (key.rfind("tableau:", 0) == 0) {
        const auto why = session_->setTable(key.substr(8), value);
        if (!why.empty()) say(why, true);
        else if (memory_ && !value.empty()) {
            memory_->touchFile(value);
            (void)memory_->save();
        }
        scheduleRefresh(true);
        return;
    }
    session_->setAnswer(key, value);
    if (memory_ && immediate && !value.empty()) {
        std::error_code ec;
        const auto* f = session_->field(key);
        if (f && f->spec.kind == FieldKind::File && fs::exists(value, ec)) {
            memory_->touchFile(value);
            (void)memory_->save();
        }
    }
    scheduleRefresh(immediate);
}

void MacrosPane::scheduleRefresh(bool immediate) {
    refreshPending_ = true;
    // Une frappe attend qu'on s'arrete : l'apercu tourne 0,3 s sur un gros classeur.
    refreshAt_ = now_ + (immediate ? 0.0 : 0.6);
}

void MacrosPane::tick(double now) {
    now_ = now;
    for (auto& [name, ed] : editors_) ed->tick(now);   // lot API 6 : le releve des cartes
    if (reorderPending_) {
        reorderPending_ = false;
        if (mode_) {
            auto keep = removeChild(*mode_);
            mode_ = &static_cast<macroui::Segmented&>(addChild(std::move(keep)));
        }
        if (contextMenu_) {
            auto keep = removeChild(*contextMenu_);
            contextMenu_ = &addChild(std::move(keep));
        }
        invalidateLayout();
    }
    if (refreshPending_ && now >= refreshAt_ && step_ == 1) {
        refreshPending_ = false;
        doRefresh(false);
    }
    // CTRL+Z A REPRIS L'APPLIQUER : l'ecran " Applique " mentirait. On revient
    // aux questions, reponses gardees - Apercu puis Appliquer la refait.
    if (step_ == 3 && appliedCommand_ && hosts_.stillApplied && !hosts_.stillApplied(appliedCommand_)) {
        appliedCommand_ = nullptr;
        step_ = 1;
        doRefresh(false);
        refreshCard();
        say(current_ + " : d\xC3\xA9" "fait par Ctrl+Z, le projet est revenu \xC3\xA0 son \xC3\xA9tat d'avant. Les r\xC3\xA9ponses "
                       "sont gard\xC3\xA9" "es : Aper\xC3\xA7u puis Appliquer la refait.");
    }
}

void MacrosPane::refreshNow() {
    refreshPending_ = false;
    doRefresh(step_ >= 2);
}

void MacrosPane::doRefresh(bool exact) {
    if (!session_) return;
    const auto& o = session_->refresh(exact);
    // Les champs : les questions atteintes, puis les tableaux CSV.
    std::vector<FormField> fields;
    const auto* own = spec(current_);
    for (const auto& t : session_->spec().tables) {
        FormField f;
        f.key = "tableau:" + t.name;
        f.table = true;
        f.spec.kind = FieldKind::File;
        f.spec.extensions = {".csv", ".txt"};
        f.spec.key = f.key;
        f.label = t.label.empty() ? "Le tableau " + t.name + " (CSV)" : t.label;
        f.help = "Enregistrer sous CSV n'exporte QUE la feuille active : ouvre l'onglet voulu, exporte-le, puis donne le fichier ici.";
        const auto it = session_->tablePaths().find(t.name);
        f.value = it == session_->tablePaths().end() ? std::string{} : it->second;
        fields.push_back(std::move(f));
    }
    for (const auto& sf : o.fields) {
        FormField f;
        f.key = sf.question.key;
        f.spec = sf.spec;
        f.label = sf.label;
        f.help = sf.spec.help;
        // Une question posee par une macro lancee (RunMacro) : son libelle et son aide.
        if (!sf.spec.declared && own)
            for (const auto& launched : own->launches)
                if (const auto* ls = spec(launched))
                    if (const auto* lf = ls->field(f.key); lf && lf->declared) {
                        auto merged = *lf;
                        if (merged.options.empty()) merged.options = sf.spec.options;
                        if (!merged.hasRange && sf.spec.hasRange) {
                            merged.hasRange = true;
                            merged.minimum = sf.spec.minimum;
                            merged.maximum = sf.spec.maximum;
                        }
                        f.spec = merged;
                        f.label = mm::labelFor(merged, f.key, sf.question.prompt);
                        f.help = merged.help;
                        if (f.spec.group.empty()) f.spec.group = ls->groupOf(f.key);
                        break;
                    }
        f.group = sf.group.empty() ? f.spec.group : sf.group;
        f.advanced = sf.advanced || f.spec.advanced;
        f.value = sf.value;
        f.proposed = sf.question.preset;
        f.origin = sf.origin == mm::MacroSession::Field::Origin::Typed ? FormField::Origin::Typed
                 : sf.origin == mm::MacroSession::Field::Origin::Remembered ? FormField::Origin::Remembered
                                                                            : FormField::Origin::Proposed;
        fields.push_back(std::move(f));
    }
    run_->form().show(fields, now_);
    if (!o.failure.empty()) run_->form().setMessage("La macro s'arr\xC3\xAAte : " + o.failure, ui::Tone::Error);
    else if (o.stopped) run_->form().setMessage("R\xC3\xA9ponds \xC3\xA0 ces questions : les suivantes en d\xC3\xA9pendent.", ui::Tone::Info);
    else if (!o.report.warnings.empty())
        run_->form().setMessage(std::to_string(o.report.warnings.size()) + " avertissement(s) dans l'aper\xC3\xA7u : "
                                    + o.report.warnings.front() + (o.report.warnings.size() > 1 ? " \xE2\x80\xA6" : ""),
                                ui::Tone::Warning);
    else run_->form().setMessage({}, ui::Tone::None);
    // L'apercu : les actions rangees par genre, les avertissements d'abord.
    if (step_ >= 2) {
        const auto& r = step_ == 3 ? applied_ : o.report;
        std::vector<std::vector<std::string>> rows;
        std::vector<ui::Tone> tones;
        std::vector<bool> heads;
        const auto head = [&](const std::string& title, std::size_t count, ui::Tone tone) {
            rows.push_back({title, std::to_string(count)});
            tones.push_back(tone);
            heads.push_back(true);
        };
        const auto item = [&](const std::string& a, const std::string& b, ui::Tone tone) {
            rows.push_back({a, b});
            tones.push_back(tone);
            heads.push_back(false);
        };
        if (!r.failure.empty()) {
            head("\xE2\x9C\x97 La macro s'arr\xC3\xAAte", 1, ui::Tone::Error);
            item("", r.failure, ui::Tone::Error);
        }
        if (!r.warnings.empty()) {
            head("\xE2\x9A\xA0 Avertissements", r.warnings.size(), ui::Tone::Warning);
            for (const auto& w : r.warnings) item("", w, ui::Tone::Warning);
        }
        struct Group { std::string title; std::vector<std::string> items; };
        std::vector<Group> groups = {{"Biblioth\xC3\xA8que (import\xC3\xA9s)", {}}, {"Variables cr\xC3\xA9\xC3\xA9" "es", {}},
                                     {"Sections cr\xC3\xA9\xC3\xA9" "es", {}}, {"Sous-routines cr\xC3\xA9\xC3\xA9" "es", {}},
                                     {"Ordre d'ex\xC3\xA9" "cution", {}}, {"Fichiers \xC3\xA9" "crits", {}}, {"Autres", {}}};
        std::size_t appended = 0, cleared = 0;
        const auto quoted = [](const std::string& a) {
            const auto q = a.find('\'');
            if (q == std::string::npos) return a;
            const auto e = a.find('\'', q + 1);
            return a.substr(q + 1, e == std::string::npos ? std::string::npos : e - q - 1);
        };
        // Une meme chose annoncee deux fois (un import par carte) ne se lit qu'une fois.
        const auto once = [](std::vector<std::string>& list, std::string text) {
            if (std::find(list.begin(), list.end(), text) == list.end()) list.push_back(std::move(text));
        };
        for (const auto& a : r.actions) {
            if (a.rfind("append", 0) == 0) ++appended;
            else if (a.rfind("clear section", 0) == 0) ++cleared;
            else if (a.rfind("import ", 0) == 0) once(groups[0].items, a.substr(7, a.find(' ', 7) - 7));
            else if (a.rfind("importe ", 0) == 0) once(groups[0].items, a.substr(8, a.find(' ', 8) - 8));
            else if (a.rfind("add variable", 0) == 0) once(groups[1].items, quoted(a));
            else if (a.rfind("add section", 0) == 0) once(groups[2].items, quoted(a));
            else if (a.rfind("add subroutine", 0) == 0) once(groups[3].items, quoted(a));
            else if (a.rfind("deplacer", 0) == 0) once(groups[4].items, quoted(a));
            else if (a.rfind("ecrire le fichier", 0) == 0) once(groups[5].items, a.substr(18));
            else once(groups[6].items, a);
        }
        for (const auto& g : groups) {
            if (g.items.empty()) continue;
            head(g.title, g.items.size(), ui::Tone::Accent);
            std::string line;
            for (const auto& it : g.items) {
                if (!line.empty() && line.size() + it.size() > 110) {
                    item("", line, ui::Tone::None);
                    line.clear();
                }
                line += (line.empty() ? "" : ",  ") + it;
            }
            if (!line.empty()) item("", line, ui::Tone::None);
        }
        if (appended || cleared) {
            head("Code \xC3\xA9" "crit", appended, ui::Tone::Accent);
            item("", thousands(r.tally.lines) + " ligne(s) \xC3\xA9" "crite(s) (" + thousands(appended) + " ajouts), " + std::to_string(cleared)
                         + " section(s) vid\xC3\xA9" "e(s) puis r\xC3\xA9\xC3\xA9" "crite(s) en entier",
                 ui::Tone::None);
        }
        if (!r.log.empty()) {
            head("Journal de la macro", r.log.size(), ui::Tone::Muted);
            for (const auto& l : r.log) item("", l, ui::Tone::Muted);
        }
        auto style = [tones, heads](ui::RowIndex row, std::size_t col) {
            ui::CellStyle s;
            if (row >= tones.size()) return s;
            s.fgTone = tones[row];
            if (heads[row]) {
                s.bold = true;
                if (col == 1) {
                    s.badge = "";
                    s.fgTone = ui::Tone::Muted;
                }
            }
            return s;
        };
        run_->report().setModel(std::make_shared<hmikit::Rows>(std::vector<std::string>{"", ""}, std::move(rows), style));
    }
    run_->syncButtons();
    invalidate();
}

void MacrosPane::goPreview() {
    if (!session_) return;
    refreshPending_ = false;
    step_ = 2;
    doRefresh(true);
    const auto& o = session_->outcome();
    if (!o.failure.empty()) say("L'aper\xC3\xA7u s'arr\xC3\xAAte : " + o.failure, true);
    else say(current_ + " : l'aper\xC3\xA7u. Rien n'est encore modifi\xC3\xA9.");
    refreshCard();
}

void MacrosPane::goBack() {
    step_ = 1;
    doRefresh(false);
    refreshCard();
}

bool MacrosPane::applyNow() {
    if (!session_ || !hosts_.apply) return false;
    if (hosts_.refuseApply) {
        if (const auto why = hosts_.refuseApply(); !why.empty()) {
            say(current_ + " : Appliquer est impossible, " + why + ". Rien n'a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9.", true);
            return false;
        }
    }
    // L'apercu exact d'abord : c'est lui qui a ete montre.
    project::MacroReport report;
    auto command = session_->apply(report);
    applied_ = report;
    if (!report.ok) {
        say(current_ + " n'a pas pu s'appliquer : " + (report.failure.empty() ? std::string("\xC3\xA9" "chec") : report.failure)
                + ". Rien n'a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9.",
            true);
        return false;
    }
    const auto& t = report.tally;
    appliedSummary_ = current_ + " appliqu\xC3\xA9" "e : " + thousands(t.variables) + " variable(s), " + thousands(t.sections) + " section(s), "
                    + thousands(t.lines) + " ligne(s) de code. Ctrl+Z reprend tout.";
    appliedCommand_ = command.get();
    // Lot API 7 : TOUJOURS l'hote, meme sans commande - une mise a jour de la
    // bibliotheque ecrit dans le projet sans commande, et l'ecran doit le savoir
    // (les onglets de l'API se relisent, la simulation se prepare a nouveau).
    hosts_.apply(std::move(command), appliedSummary_);
    if (memory_) {
        auto answers = session_->effectiveAnswers();
        answers.erase("confirm");
        memory_->remember(hosts_.projectKey ? hosts_.projectKey() : std::string{}, current_, answers);
        memory_->noteRun(current_, nowText(),
                         thousands(t.variables) + " variables, " + thousands(t.sections) + " sections, " + thousands(t.lines) + " lignes"
                             + (report.warnings.empty() ? std::string{} : ", " + std::to_string(report.warnings.size()) + " avert."));
        (void)memory_->save();
    }
    step_ = 3;
    doRefresh(true);
    refreshCard();
    say(appliedSummary_);
    return true;
}

bool MacrosPane::answer(const std::string& key, const std::string& value) {
    if (!session_ || step_ != 1) return false;
    auto& f = run_->form();
    if (!f.setValueForTest(key, value)) return false;
    refreshNow();
    return true;
}

// ---------------------------------------------------------------- gestes ----
void MacrosPane::runAction(int action) {
    const std::string macro = selectedMacro().empty() ? current_ : selectedMacro();
    const std::string folder = selectedFolder();
    std::string trashed;
    if (trashSelected()) {
        const auto i = MacroTreeModel::indexOf(tree_->currentNode());
        if (i < trash_.size()) trashed = trash_[i].name;
    }
    // La macro vise d'abord ce qui est choisi dans la liste.
    const std::string target = selectedMacro();
    switch (action) {
        case ANewMacro:
            if (hosts_.newMacro) hosts_.newMacro(!folder.empty() ? folder : target.empty() ? std::string{} : layout_.folderOf(target));
            break;
        case ANewFolder:
            if (hosts_.newFolder) hosts_.newFolder(folder);
            break;
        case ADuplicate:
            if (!target.empty() && hosts_.duplicateMacro) hosts_.duplicateMacro(target);
            break;
        case ARename:
            if (!target.empty() && hosts_.renameMacro) hosts_.renameMacro(target);
            else if (!folder.empty() && hosts_.renameFolder) hosts_.renameFolder(folder);
            break;
        case ADelete:
            if (!trashed.empty() && hosts_.purgeMacro) hosts_.purgeMacro(trashed);
            else if (!target.empty() && hosts_.deleteMacro) hosts_.deleteMacro(target);
            else if (!folder.empty() && hosts_.deleteFolder) hosts_.deleteFolder(folder);
            break;
        case APurge:
            if (!trashed.empty() && hosts_.purgeMacro) hosts_.purgeMacro(trashed);
            break;
        case ARestore:
            if (!trashed.empty() && hosts_.restoreMacro) hosts_.restoreMacro(trashed);
            break;
        case AFavourite:
            if (!macro.empty() && memory_) {
                memory_->setFavourite(macro, !memory_->favourite(macro));
                (void)memory_->save();
                applyFilter();
                refreshCard();
                tree_->invalidate();
            }
            break;
        case AEdit:
            if (macro.empty()) break;
            if (hosts_.makeEditor) edit(macro);
            else if (hosts_.editCode) hosts_.editCode(macro);
            break;
        case ALaunch:
            if (!macro.empty()) launch(macro);
            break;
        case AHelp:
            if (!macro.empty() && hosts_.help) hosts_.help(macro);
            break;
        default: break;
    }
}

void MacrosPane::showContextMenu(ui::NodeId n, gfx::Point at) {
    contextNode_ = n;
    auto* menu = static_cast<ui::PopupMenu*>(contextMenu_);
    std::vector<ui::PopupMenu::Item> items;
    const auto kind = MacroTreeModel::kindOf(n);
    const auto sep = [&] { items.push_back({"", "", "", ui::Icon::None, true, true, -1}); };
    if (kind == MacroTreeModel::KMacro) {
        const bool project = hosts_.project && hosts_.project();
        items.push_back({"Lancer", "Entr\xC3\xA9" "e", project ? "" : "ouvre un projet d'abord", ui::Icon::Play, project, false, ALaunch});
        items.push_back({"Modifier le code", "", "", ui::Icon::Code, true, false, AEdit});
        items.push_back({"Aide", "F1", "", ui::Icon::Info, true, false, AHelp});
        sep();
        items.push_back({"Nouvelle macro ici\xE2\x80\xA6", "", "", ui::Icon::Document, true, false, ANewMacro});
        items.push_back({"Dupliquer\xE2\x80\xA6", "", "", ui::Icon::Export, true, false, ADuplicate});
        items.push_back({"Renommer\xE2\x80\xA6", "F2", "", ui::Icon::Document, true, false, ARename});
        items.push_back({"Supprimer (dans la corbeille)", "Suppr", "", ui::Icon::Close, true, false, ADelete});
        sep();
        const bool fav = memory_ && memory_->favourite(treeModel_->text(n));
        items.push_back({fav ? "Retirer des favorites" : "Favorite", "", "", fav ? ui::Icon::StarFilled : ui::Icon::Star, true, false, AFavourite});
    } else if (kind == MacroTreeModel::KFolder) {
        items.push_back({"Nouvelle macro ici\xE2\x80\xA6", "", "", ui::Icon::Document, true, false, ANewMacro});
        items.push_back({"Nouveau dossier ici\xE2\x80\xA6", "", "", ui::Icon::Folder, true, false, ANewFolder});
        sep();
        items.push_back({"Renommer le dossier\xE2\x80\xA6", "F2", "", ui::Icon::Document, true, false, ARename});
        items.push_back({"Supprimer le dossier", "Suppr", "", ui::Icon::Close, true, false, ADelete});
    } else if (kind == MacroTreeModel::KTrashed) {
        items.push_back({"Restaurer", "Entr\xC3\xA9" "e", "", ui::Icon::Undo, true, false, ARestore});
        items.push_back({"Supprimer d\xC3\xA9" "finitivement", "", "", ui::Icon::Close, true, false, APurge});
    } else {
        items.push_back({"Nouvelle macro\xE2\x80\xA6", "", "", ui::Icon::Document, true, false, ANewMacro});
        items.push_back({"Nouveau dossier\xE2\x80\xA6", "", "", ui::Icon::Folder, true, false, ANewFolder});
    }
    menu->setItems(std::move(items));
    const auto surface = ui::surfaceSize();
    menu->openAt(at, surface.w > 0 ? surface : gfx::Size{bounds().x + bounds().w, bounds().y + bounds().h});
}

void MacrosPane::wireDrag() {
    using Where = ui::TreeView::DropWhere;
    tree_->setDragPredicate([](ui::NodeId n) {
        const auto k = MacroTreeModel::kindOf(n);
        return k == MacroTreeModel::KMacro || k == MacroTreeModel::KFolder;
    });
    tree_->setDropPredicate([this](ui::NodeId dragged, ui::NodeId target, Where where) {
        (void)dragged;
        const auto tk = MacroTreeModel::kindOf(target);
        bool anyFolder = false;
        auto nodes = tree_->draggedNodes();
        if (nodes.empty()) nodes.push_back(dragged);
        for (const auto n : nodes) {
            const auto k = MacroTreeModel::kindOf(n);
            if (k != MacroTreeModel::KMacro && k != MacroTreeModel::KFolder) return false;
            if (k == MacroTreeModel::KFolder) {
                anyFolder = true;
                const auto i = MacroTreeModel::indexOf(n);
                if (tk == MacroTreeModel::KFolder && i < layout_.folders.size()
                    && mm::insideFolder(layout_.folders[MacroTreeModel::indexOf(target)], layout_.folders[i]))
                    return false;
            }
        }
        if (tk == MacroTreeModel::KFolder) return where == Where::Into;
        if (tk == MacroTreeModel::KMacro) return !anyFolder && where != Where::Into;
        return false;
    });
    links_ += tree_->dropped->connect([this](ui::NodeId dragged, ui::NodeId target, Where where) {
        auto nodes = tree_->draggedNodes();
        if (nodes.empty()) nodes.push_back(dragged);
        std::vector<std::string> macros, folders;
        for (const auto n : nodes) {
            const auto i = MacroTreeModel::indexOf(n);
            if (MacroTreeModel::kindOf(n) == MacroTreeModel::KMacro && i < layout_.macros.size()) macros.push_back(layout_.macros[i].name);
            if (MacroTreeModel::kindOf(n) == MacroTreeModel::KFolder && i < layout_.folders.size()) folders.push_back(layout_.folders[i]);
        }
        const auto ti = MacroTreeModel::indexOf(target);
        std::string said;
        if (MacroTreeModel::kindOf(target) == MacroTreeModel::KFolder && ti < layout_.folders.size()) {
            const auto dest = layout_.folders[ti];
            std::size_t moved = 0;
            std::string why;
            for (const auto& f : folders)
                if (!mm::sameFolder(mm::folderParent(f), dest) && folders_.moveFolder(f, dest, &why)) ++moved;
            moved += folders_.moveMacros(macros, dest);
            if (moved == 0) {
                if (!why.empty()) say("Rien n'est d\xC3\xA9plac\xC3\xA9 : " + why, true);
                return;
            }
            said = std::to_string(moved) + (moved > 1 ? " \xC3\xA9l\xC3\xA9ments rang\xC3\xA9s" : " \xC3\xA9l\xC3\xA9ment rang\xC3\xA9") + " dans " + dest;
        } else if (MacroTreeModel::kindOf(target) == MacroTreeModel::KMacro && ti < layout_.macros.size()) {
            if (!folders_.placeNear(macros, layout_.macros[ti].name, where == Where::After)) return;
            said = std::to_string(macros.size()) + " macro(s) plac\xC3\xA9" "e(s) " + (where == Where::After ? "apr\xC3\xA8s " : "avant ")
                 + layout_.macros[ti].name;
        } else {
            return;
        }
        if (auto st = folders_.save(); !st) {
            say("dossiers.txt : " + st.error().message(), true);
            return;
        }
        const std::string keep = selectedMacro();
        layout_ = folders_.arrange();
        rebuildTree();
        if (!keep.empty()) select(keep);
        say(said + " (libs/Macros/dossiers.txt)");
        if (hosts_.libraryChanged) hosts_.libraryChanged();
    });
}

void MacrosPane::say(std::string text, bool error) {
    message_ = std::move(text);
    if (hosts_.status) hosts_.status(message_, error);
}

// --------------------------------------------------------------- la page ----
void MacrosPane::onLayout() {
    const auto b = bounds();
    const float leftW = std::clamp(b.w * 0.34f, 320.f, 540.f);   // lot API 2 : la place des pastilles
    tools_->setBounds({b.x, b.y, leftW, 38.f});
    search_->setBounds({b.x + 10.f, b.y + 44.f, leftW - 20.f, 30.f});
    float cx = b.x + 10.f;
    for (auto* c : chips_) {
        const float w = ui::measureWidth(c->text(), kSmall) + 30.f;
        c->setBounds({cx, b.y + 80.f, w, 28.f});
        cx += w + 6.f;
    }
    const float legendH = legend_ ? std::min(legend_->wantedHeight(), std::max(0.f, b.h - 114.f - 120.f)) : 0.f;
    tree_->setBounds({b.x, b.y + 114.f, leftW, std::max(0.f, b.h - 114.f - legendH)});
    if (legend_) legend_->setBounds({b.x, b.y + b.h - legendH, leftW, legendH});
    const gfx::Rect right{b.x + leftW + 1.f, b.y, std::max(0.f, b.w - leftW - 1.f), b.h};
    card_->setBounds(right);
    run_->setBounds(right);
    // Lot API 6 : le mode Modifier prend tout l'onglet ; Utiliser / Modifier
    // au bout de sa barre (ou en haut a droite de la fiche).
    for (auto& [name, ed] : editors_) ed->setBounds(b);
    if (mode_) {
        const float w = std::max(150.f, mode_->preferredWidth());
        mode_->setBounds({b.right() - w - 12.f, b.y + 5.f, w, 28.f});
    }
}

MacroEditorView* MacrosPane::editor() const {
    if (editing_.empty()) return nullptr;
    const auto it = editors_.find(editing_);
    return it == editors_.end() ? nullptr : it->second;
}

void MacrosPane::edit(const std::string& macro) {
    std::string name = macro;
    for (const auto& m : layout_.macros)
        if (lower(m.name) == lower(macro)) name = m.name;
    if (name.empty() || !hosts_.makeEditor) return;
    if (!editors_.count(name)) {
        auto made = hosts_.makeEditor(name);
        if (!made) return;
        // Sous le menu contextuel et le choix Utiliser / Modifier : ajoute avant eux.
        auto* raw = made.get();
        addChild(std::move(made));
        editors_[name] = raw;
        // Utiliser / Modifier et le menu doivent rester au-dessus de l'editeur :
        // ils repassent en fin de liste - a la prochaine image, pas pendant le
        // clic qui nous a amenes ici (la liste des enfants est en cours de visite).
        reorderPending_ = true;
    }
    if (step_ != 0) closeRun();
    current_ = name;
    editing_ = name;
    applyMode();
    say(name + " : le mode Modifier - le code, les questions en cartes, l'aper\xC3\xA7u du formulaire, Essayer (F5), Enregistrer (Ctrl+S).");
}

void MacrosPane::use() {
    if (editing_.empty()) {
        if (mode_) mode_->setSelected(0);
        return;
    }
    editing_.clear();
    applyMode();
}

void MacrosPane::applyMode() {
    const bool ed = editing();
    const auto show = [](ui::Widget* w, bool on) { if (w) w->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed); };
    show(tools_, !ed);
    show(search_, !ed);
    for (auto* c : chips_) show(c, !ed);
    show(tree_, !ed);
    show(legend_, !ed);
    for (auto& [name, e] : editors_) show(e, ed && name == editing_);
    if (mode_) mode_->setSelected(ed ? 1 : 0);
    refreshCard();
    invalidateLayout();
    invalidate();
}

void MacrosPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    ctx.r.fillRect(b, ctx.theme.color.panelBg);
    if (editing()) return;
    const float leftW = std::clamp(b.w * 0.34f, 320.f, 540.f);
    ctx.r.line({b.x + leftW, b.y}, {b.x + leftW, b.y + b.h}, ctx.theme.color.border, 1.f);
}

ui::EventResult MacrosPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (tree_->focused()) {
            if (k->key == ui::Key::F2) {
                runAction(ARename);
                return ui::EventResult::Consumed;
            }
            if (k->key == ui::Key::Delete) {
                runAction(ADelete);
                return ui::EventResult::Consumed;
            }
        }
        if (k->key == ui::Key::Return && step_ == 0 && !editing() && !current_.empty() && !search_->focused()) {
            launch(current_);
            return ui::EventResult::Consumed;
        }
    }
    if (const auto* drop = std::get_if<ui::FileDropped>(&ev); drop && bounds().contains(drop->pos) && step_ == 1) {
        if (run_->form().dropFile(drop->path, drop->pos)) return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app
