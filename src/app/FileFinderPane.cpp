// =============================================================================
//  app/FileFinderPane.cpp - 1.14.0 (lot M5) : voir FileFinderPane.hpp
// =============================================================================
#include "FileFinderPane.hpp"

#include "../ui/Commands.hpp"
#include "../ui/Layout.hpp"
#include "../ui/TextSearch.hpp"
#include "../ui/Theme.hpp"
#include "../ui/widgets/Containers.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"
#include "../ui/widgets/PathBrowse.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>

namespace app {

namespace {

const gfx::FontId kText{14};
const gfx::FontId kSmall{12};
constexpr int kMenuBase = 1000;

// Les noms plies (minuscules, sans accents) compares comme on les lit : "Vue_2" avant "Vue_10".
int naturalCompare(std::string_view a, std::string_view b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const bool da = std::isdigit(static_cast<unsigned char>(a[i])) != 0;
        const bool db = std::isdigit(static_cast<unsigned char>(b[j])) != 0;
        if (da && db) {
            std::size_t ei = i, ej = j;
            while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ++ei;
            while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ++ej;
            std::size_t si = i, sj = j;
            while (si + 1 < ei && a[si] == '0') ++si;
            while (sj + 1 < ej && b[sj] == '0') ++sj;
            if (ei - si != ej - sj) return ei - si < ej - sj ? -1 : 1;
            for (std::size_t k = 0; k < ei - si; ++k)
                if (a[si + k] != b[sj + k]) return a[si + k] < b[sj + k] ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        if (a[i] != b[j]) return static_cast<unsigned char>(a[i]) < static_cast<unsigned char>(b[j]) ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

std::size_t catalogIndex(std::string_view key) {
    const auto& c = finder::catalog();
    for (std::size_t i = 0; i < c.size(); ++i)
        if (c[i].key == key) return i;
    return c.size();
}

std::string mineLabel() {
    switch (core::edition()) {
        case core::Edition::Api: return "Celles d'API Studio";
        case core::Edition::Ihm: return "Celles d'IHM Studio";
        case core::Edition::Both: break;
    }
    return "Celles de l'appli";
}

// Un texte trop long pour sa place finit par "..." (sans couper un caractere en deux).
std::string elide(const gfx::IRenderer& r, const std::string& s, gfx::FontId f, float maxW) {
    if (maxW <= 0.f) return {};
    if (r.fitCharacters(s, f, maxW) >= s.size()) return s;
    static const std::string kDots = "\xE2\x80\xA6";
    const float room = maxW - r.measure(kDots, f).width;
    std::size_t keep = room > 0.f ? std::min(r.fitCharacters(s, f, room), s.size()) : 0;
    while (keep > 0 && keep < s.size() && (static_cast<unsigned char>(s[keep]) & 0xC0) == 0x80) --keep;
    return s.substr(0, keep) + kDots;
}

// Des lignes de `maxW` au plus (les mots entiers).
std::vector<std::string> wrapText(const std::string& text, gfx::FontId f, float maxW) {
    std::vector<std::string> out;
    std::string line;
    std::size_t pos = 0;
    while (pos < text.size()) {
        auto sp = text.find(' ', pos);
        if (sp == std::string::npos) sp = text.size();
        const std::string word = text.substr(pos, sp - pos);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(candidate, f) > maxW) {
            out.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        pos = sp + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

const char* const kNote = "La recherche tourne en arri\xC3\xA8re-plan : tu peux continuer \xC3\xA0 travailler. "
                          "Les r\xC3\xA9sultats restent jusqu'\xC3\xA0 la prochaine recherche, tant que l'appli est ouverte.";

} // namespace

// =================================================================== le modele ==
class FinderModel final : public ui::ITableModel {
public:
    explicit FinderModel(const FileFinderPane& pane) : pane_(pane) {}
    [[nodiscard]] std::size_t rowCount() const override { return pane_.rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return FileFinderPane::ColCount; }
    [[nodiscard]] std::string headerText(std::size_t col) const override {
        static const char* const kTitles[] = {"Nom", "Type", "Dossier", "Taille", "Modifi\xC3\xA9 le", "Version"};
        return col < FileFinderPane::ColCount ? kTitles[col] : std::string{};
    }
    [[nodiscard]] const finder::Hit& hit(ui::RowIndex r) const { return pane_.hits_[pane_.rows_[r]]; }
    [[nodiscard]] std::string cellText(ui::RowIndex row, std::size_t col) const override {
        if (row >= pane_.rows_.size()) return {};
        const auto& h = hit(row);
        switch (col) {
            case FileFinderPane::ColName:    return h.name;
            case FileFinderPane::ColType:    return finder::typeText(h);
            case FileFinderPane::ColDir:     return h.dir;
            case FileFinderPane::ColSize:    return finder::sizeText(h.size);
            case FileFinderPane::ColDate:    return finder::dateText(h.mtime);
            case FileFinderPane::ColVersion: return h.version.empty() ? std::string("\xE2\x80\x94") : h.version;
            default:                         return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex row, std::size_t col) const override {
        ui::CellStyle s;
        if (row >= pane_.rows_.size()) return s;
        if (col == FileFinderPane::ColDir || (col == FileFinderPane::ColVersion && hit(row).version.empty())) s.fgTone = ui::Tone::Muted;
        if (col == FileFinderPane::ColName) s.icon = finder::openable(hit(row).key) ? ui::Icon::Project : ui::Icon::Document;
        return s;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t col) const override {
        const auto& x = hit(a);
        const auto& y = hit(b);
        switch (col) {
            case FileFinderPane::ColName: {
                const int c = naturalCompare(x.folded, y.folded);
                return c != 0 ? c < 0 : x.dir < y.dir;
            }
            case FileFinderPane::ColType: {
                const int c = naturalCompare(ui::foldForSearch(finder::typeText(x)), ui::foldForSearch(finder::typeText(y)));
                return c != 0 ? c < 0 : naturalCompare(x.folded, y.folded) < 0;
            }
            case FileFinderPane::ColDir: {
                const int c = naturalCompare(ui::foldForSearch(x.dir), ui::foldForSearch(y.dir));
                return c != 0 ? c < 0 : naturalCompare(x.folded, y.folded) < 0;
            }
            case FileFinderPane::ColSize:    return x.size != y.size ? x.size < y.size : naturalCompare(x.folded, y.folded) < 0;
            case FileFinderPane::ColDate:    return x.mtime != y.mtime ? x.mtime < y.mtime : naturalCompare(x.folded, y.folded) < 0;
            case FileFinderPane::ColVersion: {
                const int c = naturalCompare(x.version, y.version);
                return c != 0 ? c < 0 : naturalCompare(x.folded, y.folded) < 0;
            }
            default: return false;
        }
    }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex row) const override {
        if (row >= pane_.rows_.size()) return {};
        const auto& h = hit(row);
        std::string tip = h.path;
        if (!h.archive.empty()) tip += "\n(dans l'archive " + h.archive + ")";
        return tip;
    }

private:
    const FileFinderPane& pane_;
};

// ======================================================= la colonne de gauche ==
class FinderSide final : public ui::Widget {
public:
    FinderSide(FileFinderPane& pane, std::string id) : ui::Widget(std::move(id)), pane_(pane) {
        const auto& cat = finder::catalog();
        for (std::size_t i = 0; i < cat.size(); ++i) {
            auto box = std::make_unique<ui::Checkbox>("." + std::string(cat[i].key) + "  " + std::string(cat[i].label),
                                                      this->id() + ".ext." + std::string(cat[i].key));
            box->setTooltip("Montrer les fichiers ." + std::string(cat[i].key) + " (" + std::string(cat[i].label) + ", "
                            + std::string(finder::studioName(cat[i].studio)) + ")");
            boxes.push_back(&static_cast<ui::Checkbox&>(addChild(std::move(box))));
        }
        all = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Tout", this->id() + ".tout")));
        none = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Rien", this->id() + ".rien")));
        mine = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>(mineLabel(), this->id() + ".miennes")));
        for (auto* b : {all, none, mine}) b->setStyle(ui::Button::Style::Flat);
        all->setTooltip("Cocher toutes les extensions");
        none->setTooltip("Tout d\xC3\xA9" "cocher (puis cocher celles qu'on veut)");
        mine->setTooltip("Les extensions dont " + core::productName() + " se sert");
        // 1.14.0 (captures finales) : des libelles qui tiennent dans la colonne (avant : "Passer Windows, Program
        // Files, la Cor...") ; le detail dans l'infobulle de chacun.
        static const char* const kOptions[] = {"Lecteurs r\xC3\xA9seau", "Cl\xC3\xA9s USB et disques externes",
                                               "Passer les dossiers syst\xC3\xA8me et cach\xC3\xA9s",
                                               "Dans les archives .zip aussi"};
        static const char* const kTips[] = {
            "Tout le PC : les lecteurs r\xC3\xA9seau mapp\xC3\xA9s aussi (lus \xC3\xA0 leur rythme, sans charger le r\xC3\xA9seau)",
            "Tout le PC : les cl\xC3\xA9s USB, les cartes et les disques externes branch\xC3\xA9s",
            "Ni Windows, ni Program Files, ni ProgramData, ni la Corbeille, ni les dossiers syst\xC3\xA8me et cach\xC3\xA9s : plus vite, et rien d'utile n'y est",
            "Lire aussi la table des mati\xC3\xA8res des .zip : ce qu'ils contiennent se montre (Ouvrir le dossier montre le .zip ; extrais-le pour ouvrir)"};
        static const char* const kKeys[] = {"reseau", "usb", "systeme", "zip"};
        for (int i = 0; i < 4; ++i) {
            auto box = std::make_unique<ui::Checkbox>(kOptions[i], this->id() + ".option." + kKeys[i]);
            box->setTooltip(kTips[i]);
            options[i] = &static_cast<ui::Checkbox&>(addChild(std::move(box)));
        }
    }

    float heightFor(float w) const {
        float y = 10.f + 22.f;
        std::string_view group;
        for (const auto& e : finder::catalog()) {
            if (e.group != group) {
                group = e.group;
                y += 22.f;
            }
            y += 22.f;
        }
        y += 8.f + 30.f + 12.f + 22.f + 4.f * 24.f + 10.f;
        y += static_cast<float>(wrapText(kNote, kSmall, std::max(60.f, w - 24.f)).size()) * ui::lineHeight(kSmall) + 12.f;
        return y;
    }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        const float w = std::max(240.f, bounds().w);
        return ui::SizeHint{{w, heightFor(w)}, {200.f, 120.f}, 0.f, 0.f};
    }
    // 1.14.0 (captures finales) : la plus large des cases (une extension, une option) - la colonne la tient entiere.
    [[nodiscard]] float widestRow() const {
        float w = 0.f;
        for (const auto* b : boxes) w = std::max(w, b->sizeHint().preferred.w + 44.f);   // + le nombre trouve, a droite
        for (const auto* o : options) w = std::max(w, o->sizeHint().preferred.w);
        return w;
    }

    std::vector<ui::Checkbox*> boxes;
    ui::Button*                all{nullptr};
    ui::Button*                none{nullptr};
    ui::Button*                mine{nullptr};
    ui::Checkbox*              options[4]{};

protected:
    void onLayout() override {
        const auto b = bounds();
        const float x0 = b.x + 12.f, w = std::max(60.f, b.w - 24.f);
        float y = b.y + 10.f;
        heads_.clear();
        heads_.push_back({"EXTENSIONS", y, true});
        y += 22.f;
        std::string_view group;
        rowY_.assign(boxes.size(), 0.f);
        const auto& cat = finder::catalog();
        for (std::size_t i = 0; i < cat.size(); ++i) {
            if (cat[i].group != group) {
                group = cat[i].group;
                std::string up(group);
                // Les capitales du groupe, accents compris (les lettres latines de l'UTF-8 : C3 A0..BE -> C3 80..9E).
                for (std::size_t k = 0; k < up.size(); ++k) {
                    const auto ch = static_cast<unsigned char>(up[k]);
                    if (ch < 0x80) up[k] = static_cast<char>(std::toupper(ch));
                    else if (ch == 0xC3 && k + 1 < up.size()) {
                        const auto next = static_cast<unsigned char>(up[k + 1]);
                        if (next >= 0xA0 && next <= 0xBE && next != 0xB7) up[k + 1] = static_cast<char>(next - 0x20);
                        ++k;
                    }
                }
                heads_.push_back({std::move(up), y + 4.f, false});
                y += 22.f;
            }
            rowY_[i] = y;
            boxes[i]->setBounds({x0, y, w - 44.f, 22.f});
            y += 22.f;
        }
        y += 8.f;
        float bx = x0;
        for (auto* btn : {all, none, mine}) {
            const float bw = std::min(btn->sizeHint().preferred.w + 6.f, x0 + w - bx);
            btn->setBounds({bx, y, std::max(30.f, bw), 28.f});
            bx += bw + 6.f;
        }
        y += 30.f + 12.f;
        heads_.push_back({"OPTIONS", y, true});
        y += 22.f;
        for (auto* o : options) {
            o->setBounds({x0, y, w, 24.f});
            y += 24.f;
        }
        noteY_ = y + 10.f;
        noteW_ = w;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.panelBg);
        const float x0 = b.x + 12.f, w = std::max(60.f, b.w - 24.f);
        for (const auto& h : heads_)
            ctx.r.drawText({x0, h.y}, h.text, h.big ? kText : kSmall, h.big ? c.text : c.textMuted);
        const auto& cat = finder::catalog();
        for (std::size_t i = 0; i < cat.size() && i < rowY_.size(); ++i) {
            const std::size_t n = pane_.counts_.size() > i ? pane_.counts_[i] : 0;
            if (n == 0) continue;
            const std::string t = finder::thousands(n);
            const float tw = ctx.r.measure(t, kSmall).width;
            // A gauche de la barre de defilement (12 px au bord), quand elle est la.
            ctx.r.drawText({x0 + w - 8.f - tw, rowY_[i] + (22.f - ctx.r.lineHeight(kSmall)) * 0.5f}, t, kSmall, c.textMuted);
        }
        float y = noteY_;
        for (const auto& line : wrapText(kNote, kSmall, noteW_)) {
            ctx.r.drawText({x0, y}, line, kSmall, c.textMuted);
            y += ctx.r.lineHeight(kSmall);
        }
    }

private:
    struct Head {
        std::string text;
        float       y{0.f};
        bool        big{false};
    };
    FileFinderPane&    pane_;
    std::vector<Head>  heads_;
    std::vector<float> rowY_;
    float              noteY_{0.f};
    float              noteW_{200.f};
};

// ================================================================== les puces ==
class FinderChips final : public ui::Widget {
public:
    FinderChips(FileFinderPane& pane, std::string id) : ui::Widget(std::move(id)), pane_(pane) {}

    // La hauteur pour une largeur : les puces passent a la ligne.
    float heightFor(float w) const {
        const auto keys = pane_.chipKeys();
        if (keys.empty()) return 34.f;
        float x = 8.f + ui::measureWidth("Afficher :", kSmall) + 10.f;
        float lines = 1.f;
        for (const auto& k : keys) {
            const float cw = ui::measureWidth(label(k), kSmall) + 18.f;
            if (x + cw > w - 8.f && x > 100.f) {
                x = 8.f + ui::measureWidth("Afficher :", kSmall) + 10.f;
                lines += 1.f;
            }
            x += cw + 6.f;
        }
        return 8.f + lines * 28.f;
    }
    std::string label(const std::string& key) const { return "." + key + "  " + finder::thousands(pane_.countOf(key)); }

protected:
    void onLayout() override {
        rects_.clear();
        const auto b = bounds();
        const float start = b.x + 8.f + ui::measureWidth("Afficher :", kSmall) + 10.f;
        float x = start, y = b.y + 6.f;
        for (const auto& k : pane_.chipKeys()) {
            const float cw = ui::measureWidth(label(k), kSmall) + 18.f;
            if (x + cw > b.right() - 8.f && x > start) {
                x = start;
                y += 28.f;
            }
            rects_.push_back({k, {x, y, cw, 22.f}});
            x += cw + 6.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.panelBg);
        ctx.r.fillRect({b.x, b.bottom() - 1.f, b.w, 1.f}, c.border);
        const float ty = b.y + 6.f + (22.f - ctx.r.lineHeight(kSmall)) * 0.5f;
        ctx.r.drawText({b.x + 8.f, ty}, "Afficher :", kSmall, c.textMuted);
        if (rects_.empty()) {
            const float x = b.x + 8.f + ctx.r.measure("Afficher :", kSmall).width + 10.f;
            ctx.r.drawText({x, ty}, "les extensions trouv\xC3\xA9" "es appara\xC3\xAEtront ici", kSmall, c.textMuted);
            return;
        }
        for (const auto& [key, r] : rects_) {
            const bool on = pane_.extensionOn(key);
            // 1.14.0 (captures finales) : survolee seulement si la souris est encore sur les puces (avant : la
            // derniere position vue restait - la puce cliquee gardait le bleu du survol, la souris partie).
            const bool hot = hovered() && r.contains(hover_);
            if (on) ctx.r.fillRoundedRect(r, hot ? c.accentHover : c.accent, 4.f);
            else {
                ctx.r.fillRoundedRect(r, hot ? c.rowAltBg : c.inputBg, 4.f);
                ctx.r.strokeRect(r, c.border, 1.f);
            }
            ctx.r.drawText({r.x + 9.f, r.y + (r.h - ctx.r.lineHeight(kSmall)) * 0.5f}, label(key), kSmall, on ? c.textInverted : c.text);
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            if (hover_.x != m->pos.x || hover_.y != m->pos.y) {
                hover_ = m->pos;
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
            for (const auto& [key, r] : rects_)
                if (r.contains(d->pos)) {
                    pane_.toggleChip(key);
                    return ui::EventResult::Consumed;
                }
        }
        return ui::EventResult::Ignored;
    }
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override {
        for (const auto& [key, r] : rects_)
            if (r.contains(mouse))
                return (pane_.extensionOn(key) ? "Cacher les fichiers ." : "Montrer les fichiers .") + key + " (un clic)";
        return {};
    }
    [[nodiscard]] bool hasTooltip() const override { return true; }

public:
    [[nodiscard]] const std::vector<std::pair<std::string, gfx::Rect>>& chipRects() const noexcept { return rects_; }

private:
    FileFinderPane&                                   pane_;
    std::vector<std::pair<std::string, gfx::Rect>>    rects_;
    gfx::Point                                        hover_{-1.f, -1.f};
};

// ==================================================================== le volet ==
FileFinderPane::FileFinderPane(std::string id, finder::Search& search) : ui::Widget(std::move(id)), search_(search) {
    const std::string base = this->id();
    const auto& cat = finder::catalog();
    counts_.assign(cat.size(), 0);
    on_.assign(cat.size(), true);

    name_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".nom")));
    name_->setPlaceholder("Nom contient\xE2\x80\xA6 (ex. relevage)");
    name_->setTooltip("Filtre les r\xC3\xA9sultats au fil de la frappe : chaque mot dans le nom (sans la casse ni les accents) ; -mot l'exclut");
    where_ = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + ".ou")));
    where_->setTooltip("O\xC3\xB9 chercher : tout le PC (ses lecteurs ; les lecteurs r\xC3\xA9seau et les cl\xC3\xA9s USB selon les options), le lecteur du syst\xC3\xA8me, tes Documents, ou un dossier");
    folderField_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".dossier")));
    folderField_->setPlaceholder("le dossier o\xC3\xB9 chercher");
    browse_ = &static_cast<ui::BrowseButton&>(addChild(std::make_unique<ui::BrowseButton>(
        *folderField_, ui::chooseFolder({}, "Chercher dans ce dossier"), base + ".parcourir")));
    browse_->setFieldLabel("Le dossier o\xC3\xB9 chercher");
    go_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Chercher", base + ".chercher")));
    go_->setIcon(ui::Icon::Search);
    go_->setStyle(ui::Button::Style::Primary);
    go_->setTooltip("Chercher les fichiers de l'appli (toutes les extensions du catalogue ; les cases choisissent ce qui se montre)");
    stop_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Arr\xC3\xAAter", base + ".arreter")));
    stop_->setIcon(ui::Icon::Stop);
    stop_->setTooltip("Arr\xC3\xAAter la recherche tout de suite : ce qui est trouv\xC3\xA9 reste");

    auto side = std::make_unique<FinderSide>(*this, base + ".cote");
    side_ = side.get();
    auto panel = std::make_unique<ui::ScrollablePanel>(base + ".cote.defile");
    panel->setScrollPolicy(false, true);
    panel->setContent(std::move(side));
    sidePanel_ = &static_cast<ui::ScrollablePanel&>(addChild(std::move(panel)));

    chips_ = &static_cast<FinderChips&>(addChild(std::make_unique<FinderChips>(*this, base + ".puces")));

    model_ = std::make_shared<FinderModel>(*this);
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(base + ".resultats")));
    table_->setColumns({{"Nom", 260.f}, {"Type", 170.f}, {"Dossier", 360.f}, {"Taille", 96.f, 50.f, true, true, true, ui::Align::End},
                        {"Modifi\xC3\xA9 le", 144.f}, {"Version", 200.f}});
    table_->setSelectionMode(ui::SelectionMode::Single);
    table_->setAlternatingRowColors(true);
    table_->setEnterActivates(true);
    table_->setDoubleClickEdits(false);
    table_->setExportName("fichiers-trouves");
    table_->setModel(model_);
    table_->sortBy(ColName, ui::SortOrder::Ascending);

    static const char* const kLabels[] = {"Ouvrir", "Ouvrir le dossier", "Copier le chemin", "Importer dans le projet", "Ajouter aux r\xC3\xA9" "cents"};
    static const char* const kIds[] = {"ouvrir", "dossier", "copier", "importer", "recents"};
    for (int i = 0; i < 5; ++i) {
        actions_[i] = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>(kLabels[i], base + "." + kIds[i])));
        links_ += actions_[i]->clicked->connect([this, i] { (void)run(static_cast<Act>(i)); });
    }
    actions_[0]->setStyle(ui::Button::Style::Primary);
    actions_[0]->setIcon(ui::Icon::Open);
    actions_[1]->setIcon(ui::Icon::Folder);
    actions_[2]->setIcon(ui::Icon::Copy);
    actions_[3]->setIcon(ui::Icon::Import);

    // Le menu du clic droit sur une ligne : les memes gestes, le fichier en tete.
    table_->setExtraContextItems([this] {
        std::vector<ui::PopupMenu::Item> items;
        const auto* h = selected();
        if (!h) return items;
        items.push_back(ui::cmd::heading(h->name, finder::typeText(*h)));
        static const ui::Icon kIcons[] = {ui::Icon::Open, ui::Icon::Folder, ui::Icon::Copy, ui::Icon::Import, ui::Icon::History};
        for (int i = 0; i < 5; ++i) {
            const auto g = gesture(static_cast<Act>(i));
            ui::PopupMenu::Item it;
            it.label = g.label;
            it.enabled = g.enabled;
            it.disabledReason = g.why;
            it.icon = kIcons[i];
            it.id = kMenuBase + i;
            if (i == 0) it.shortcut = "Entr\xC3\xA9" "e";
            items.push_back(std::move(it));
        }
        ui::PopupMenu::Item sep;
        sep.separator = true;
        items.push_back(sep);
        return items;
    });
    links_ += table_->contextAction->connect([this](int item) {
        if (item >= kMenuBase && item < kMenuBase + 5) (void)run(static_cast<Act>(item - kMenuBase));
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (filling_) return;
        selectedPath_ = rows.empty() || rows.front() >= rows_.size() ? std::string{} : hits_[rows_[rows.front()]].path;
        message_.clear();
        refreshButtons();
        invalidate();
    });
    links_ += table_->activated->connect([this](ui::RowIndex row) {
        if (row < rows_.size()) selectedPath_ = hits_[rows_[row]].path;
        (void)activate();
    });

    links_ += name_->textChanged->connect([this](const std::string& t) {
        if (!filling_) setNameFilter(t);
    });
    links_ += go_->clicked->connect([this] { start(); });
    links_ += stop_->clicked->connect([this] { stop(); });
    links_ += where_->selectionChanged->connect([this](int) {
        if (filling_) return;
        invalidateLayout();
        refreshButtons();
        saveChoices();
    });
    links_ += folderField_->textChanged->connect([this](const std::string& t) {
        folder_ = t;
        if (!filling_) saveChoices();
    });
    for (std::size_t i = 0; i < side_->boxes.size(); ++i) {
        const std::string key(cat[i].key);
        links_ += side_->boxes[i]->stateChanged->connect([this, key](ui::Checkbox::State s) {
            if (!filling_) setExtension(key, s == ui::Checkbox::State::Checked);
        });
    }
    links_ += side_->all->clicked->connect([this] { checkAll(); });
    links_ += side_->none->clicked->connect([this] { checkNone(); });
    links_ += side_->mine->clicked->connect([this] { checkMine(); });
    for (int i = 0; i < 4; ++i)
        links_ += side_->options[i]->stateChanged->connect([this, i](ui::Checkbox::State s) {
            if (!filling_) setOption(static_cast<Option>(i), s == ui::Checkbox::State::Checked);
        });

    // Les lecteurs du libelle "Tout le PC (C:, D:, E:)" : lus une fois (vite, sans les volumes).
    {
        const std::string drives = finder::drivesLabel(true, true);
        std::vector<ui::DropDown::Item> items;
        items.push_back({"Tout le PC (" + drives + ")", "0"});
        items.push_back({finder::systemDrive() + " seulement", "1"});
        items.push_back({"Mes documents", "2"});
        items.push_back({"Un dossier\xE2\x80\xA6", "3"});
        where_->setItems(std::move(items));
        where_->setSelectedIndex(0);
    }
    setChoices(finder::parseChoices({}));
    // Une recherche deja faite (le volet se rouvre) : ses resultats, tout de suite.
    poll(true);
}

FileFinderPane::~FileFinderPane() {
    // L'onglet se ferme : la recherche qu'il a lancee s'arrete (ses resultats restent).
    if (mine_ != 0) {
        const auto p = search_.progress();
        if (p.generation == mine_ && p.running) search_.stop();
    }
}

void FileFinderPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    refreshButtons();
}

// ---------------------------------------------------------------- le choix ----
void FileFinderPane::setChoices(const finder::Choices& c) {
    filling_ = true;
    const auto& cat = finder::catalog();
    for (std::size_t i = 0; i < cat.size(); ++i) {
        on_[i] = std::find(c.extensions.begin(), c.extensions.end(), cat[i].key) != c.extensions.end();
        side_->boxes[i]->setState(on_[i] ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    }
    where_->setSelectedIndex(static_cast<int>(c.where));
    folder_ = c.folder;
    folderField_->setText(c.folder);
    options_[0] = c.network;
    options_[1] = c.removable;
    options_[2] = c.skipSystem;
    options_[3] = c.zips;
    for (int i = 0; i < 4; ++i) side_->options[i]->setState(options_[i] ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    filling_ = false;
    refilter();
    invalidateLayout();
}

finder::Choices FileFinderPane::choices() const {
    finder::Choices c;
    const auto& cat = finder::catalog();
    for (std::size_t i = 0; i < cat.size(); ++i)
        if (on_[i]) c.extensions.emplace_back(cat[i].key);
    c.where = where();
    c.folder = folder_;
    c.network = options_[0];
    c.removable = options_[1];
    c.skipSystem = options_[2];
    c.zips = options_[3];
    return c;
}

void FileFinderPane::saveChoices() {
    if (hosts_.saveChoices) hosts_.saveChoices(finder::serialize(choices()));
}

finder::Where FileFinderPane::where() const {
    const int i = where_->selectedIndex();
    return i >= 0 && i <= 3 ? static_cast<finder::Where>(i) : finder::Where::WholePc;
}

void FileFinderPane::setWhere(finder::Where w, std::string folder) {
    filling_ = true;
    where_->setSelectedIndex(static_cast<int>(w));
    if (w == finder::Where::Folder) {
        folder_ = std::move(folder);
        folderField_->setText(folder_);
    }
    filling_ = false;
    invalidateLayout();
    refreshButtons();
    saveChoices();
}

void FileFinderPane::setOption(Option o, bool on) {
    const auto i = static_cast<std::size_t>(o);
    if (i >= 4) return;
    options_[i] = on;
    filling_ = true;
    side_->options[i]->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    filling_ = false;
    saveChoices();
}

bool FileFinderPane::option(Option o) const {
    const auto i = static_cast<std::size_t>(o);
    return i < 4 && options_[i];
}

// ------------------------------------------------------------- la recherche ----
void FileFinderPane::start() {
    finder::Request r;
    r.where = where();
    r.folder = folder_;
    while (!r.folder.empty() && (r.folder.back() == ' ' || r.folder.back() == '\t')) r.folder.pop_back();
    r.network = options_[0];
    r.removable = options_[1];
    r.skipSystem = options_[2];
    r.zips = options_[3];
    r.roots = testRoots_;
    r.refuseForTests = testRefused_;
    if (r.roots.empty() && r.where == finder::Where::Folder && r.folder.empty()) {
        say("Un dossier\xE2\x80\xA6 : choisis le dossier o\xC3\xB9 chercher (le bouton \xE2\x80\xA6 \xC3\xA0 c\xC3\xB4t\xC3\xA9 du champ).");
        return;
    }
    search_.start(std::move(r));
    mine_ = search_.progress().generation;
    message_.clear();
    poll(true);
    saveChoices();
}

void FileFinderPane::stop() {
    search_.stop();
    poll(true);
}

void FileFinderPane::poll(bool force) {
    const auto p = search_.progress();
    bool reset = false;
    if (p.generation != generation_) {
        generation_ = p.generation;
        hits_.clear();
        rows_.clear();
        std::fill(counts_.begin(), counts_.end(), 0);
        reset = true;
    }
    auto fresh = search_.since(hits_.size());
    for (auto& h : fresh) {
        const std::size_t ci = catalogIndex(h.key);
        if (ci < counts_.size()) ++counts_[ci];
        hits_.push_back(std::move(h));
        if (passes(hits_.back())) rows_.push_back(hits_.size() - 1);
    }
    if (!fresh.empty() || reset) pendingRefresh_ = true;
    const bool runningChanged = progress_.running != p.running || progress_.generation != p.generation;
    progress_ = p;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (pendingRefresh_ && (force || reset || !p.running || now - lastRefresh_ >= 0.25)) {
        pendingRefresh_ = false;
        lastRefresh_ = now;
        refreshTable(!reset);
        if (reset) {
            selectedPath_.clear();
            filling_ = true;
            table_->selectModelRows({}, false);
            filling_ = false;
        }
    }
    if (runningChanged) refreshButtons();
    invalidate();
}

// ---------------------------------------------------------------- le filtre ----
bool FileFinderPane::passes(const finder::Hit& h) const {
    const std::size_t ci = catalogIndex(h.key);
    if (ci >= on_.size() || !on_[ci]) return false;
    if (nameFilter_.empty()) return true;
    const ui::SearchQuery q(nameFilter_);
    return q.matches({std::string_view(h.name)});
}

void FileFinderPane::refilter() {
    rows_.clear();
    if (!nameFilter_.empty()) {
        const ui::SearchQuery q(nameFilter_);
        for (std::size_t i = 0; i < hits_.size(); ++i) {
            const std::size_t ci = catalogIndex(hits_[i].key);
            if (ci < on_.size() && on_[ci] && q.matches({std::string_view(hits_[i].name)})) rows_.push_back(i);
        }
    } else {
        for (std::size_t i = 0; i < hits_.size(); ++i) {
            const std::size_t ci = catalogIndex(hits_[i].key);
            if (ci < on_.size() && on_[ci]) rows_.push_back(i);
        }
    }
    refreshTable(false);
    // Le fichier choisi reste choisi s'il est encore montre.
    filling_ = true;
    std::vector<ui::RowIndex> keep;
    for (std::size_t r = 0; r < rows_.size() && !selectedPath_.empty(); ++r)
        if (hits_[rows_[r]].path == selectedPath_) keep.push_back(static_cast<ui::RowIndex>(r));
    table_->selectModelRows(keep, false);
    if (keep.empty()) selectedPath_.clear();
    filling_ = false;
    refreshButtons();
}

void FileFinderPane::refreshTable(bool keepScroll) {
    const float y = table_->scrollOffset();
    model_->modelReset->emit();
    if (keepScroll) table_->setScrollOffset(y);
    chips_->invalidateLayout();
    side_->invalidate();
    invalidateLayout();
    refreshButtons();
}

void FileFinderPane::setNameFilter(std::string text) {
    if (text == nameFilter_) return;
    nameFilter_ = std::move(text);
    if (name_->text() != nameFilter_) {
        filling_ = true;
        name_->setText(nameFilter_);
        filling_ = false;
    }
    table_->setHighlight(nameFilter_);
    refilter();
}

void FileFinderPane::setExtension(std::string_view key, bool on) {
    const std::size_t i = catalogIndex(key);
    if (i >= on_.size()) return;
    if (on_[i] != on) {
        on_[i] = on;
        refilter();
    }
    filling_ = true;
    side_->boxes[i]->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    filling_ = false;
    chips_->invalidate();
    saveChoices();
}

bool FileFinderPane::extensionOn(std::string_view key) const {
    const std::size_t i = catalogIndex(key);
    return i < on_.size() && on_[i];
}

void FileFinderPane::checkAll() {
    filling_ = true;
    for (std::size_t i = 0; i < on_.size(); ++i) {
        on_[i] = true;
        side_->boxes[i]->setState(ui::Checkbox::State::Checked);
    }
    filling_ = false;
    refilter();
    chips_->invalidate();
    saveChoices();
}

void FileFinderPane::checkNone() {
    filling_ = true;
    for (std::size_t i = 0; i < on_.size(); ++i) {
        on_[i] = false;
        side_->boxes[i]->setState(ui::Checkbox::State::Unchecked);
    }
    filling_ = false;
    refilter();
    chips_->invalidate();
    saveChoices();
}

void FileFinderPane::checkMine() {
    const auto& cat = finder::catalog();
    filling_ = true;
    for (std::size_t i = 0; i < on_.size(); ++i) {
        on_[i] = finder::mine(cat[i], core::edition());
        side_->boxes[i]->setState(on_[i] ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    }
    filling_ = false;
    refilter();
    chips_->invalidate();
    saveChoices();
}

void FileFinderPane::toggleChip(std::string_view key) { setExtension(key, !extensionOn(key)); }

std::size_t FileFinderPane::countOf(std::string_view key) const {
    const std::size_t i = catalogIndex(key);
    return i < counts_.size() ? counts_[i] : 0;
}

std::vector<std::string> FileFinderPane::chipKeys() const {
    std::vector<std::string> out;
    const auto& cat = finder::catalog();
    for (std::size_t i = 0; i < cat.size(); ++i)
        if (counts_[i] > 0) out.emplace_back(cat[i].key);
    std::sort(out.begin(), out.end());
    return out;
}

// ------------------------------------------------------------ les resultats ----
std::vector<const finder::Hit*> FileFinderPane::rows() const {
    std::vector<const finder::Hit*> out;
    for (std::size_t i = 0; i < table_->visibleRowCount(); ++i) {
        const auto r = table_->viewRow(i);
        if (r < rows_.size()) out.push_back(&hits_[rows_[r]]);
    }
    return out;
}

void FileFinderPane::sortBy(Column c, bool ascending) {
    table_->sortBy(c, ascending ? ui::SortOrder::Ascending : ui::SortOrder::Descending);
}

bool FileFinderPane::select(const std::string& path) {
    for (std::size_t r = 0; r < rows_.size(); ++r)
        if (hits_[rows_[r]].path == path) {
            table_->selectModelRows({static_cast<ui::RowIndex>(r)}, true);
            selectedPath_ = path;
            refreshButtons();
            return true;
        }
    return false;
}

const finder::Hit* FileFinderPane::selected() const {
    if (selectedPath_.empty()) return nullptr;
    for (const auto r : rows_)
        if (hits_[r].path == selectedPath_) return &hits_[r];
    return nullptr;
}

finder::Context FileFinderPane::context() const {
    if (hosts_.context) return hosts_.context();
    finder::Context c;
    c.edition = core::edition();
    return c;
}

finder::Gesture FileFinderPane::gesture(Act a) const {
    finder::Gesture g;
    const auto* h = selected();
    static const char* const kLabels[] = {"Ouvrir", "Ouvrir le dossier", "Copier le chemin", "Importer dans le projet", "Ajouter aux r\xC3\xA9" "cents"};
    g.label = kLabels[static_cast<int>(a)];
    if (!h) {
        g.why = "choisis d'abord un fichier dans la liste";
        return g;
    }
    const auto ctx = context();
    switch (a) {
        case Act::Open:     g = finder::openGesture(*h, ctx); break;
        case Act::Import:   g = finder::importGesture(*h, ctx); break;
        case Act::Recent:   g = finder::recentGesture(*h, ctx); break;
        case Act::Reveal:
        case Act::CopyPath: g.enabled = true; break;
    }
    // Sans l'ecran pour le faire (les essais) : grise.
    if (g.enabled && ((a == Act::Open && !hosts_.open) || (a == Act::Import && !hosts_.import) || (a == Act::Recent && !hosts_.addRecent))) {
        g.enabled = false;
        g.why = "pas ici";
    }
    return g;
}

bool FileFinderPane::run(Act a) {
    const auto* h = selected();
    const auto g = gesture(a);
    if (!h || !g.enabled) {
        say(g.label + " : " + g.why);
        return false;
    }
    const finder::Hit hit = *h;   // une copie : le geste peut refaire la liste
    switch (a) {
        case Act::Open:
            hosts_.open(hit);
            return true;
        case Act::Import:
            hosts_.import(hit);
            return true;
        case Act::Recent:
            hosts_.addRecent(hit);
            return true;
        case Act::CopyPath:
            ui::setClipboardText(hit.path);
            say("Chemin copi\xC3\xA9 : " + hit.path);
            return true;
        case Act::Reveal: {
            const std::string target = hit.archive.empty() ? hit.path : hit.archive;
            const std::string why = hosts_.reveal ? hosts_.reveal(target) : finder::revealInExplorer(target);
            say(why.empty() ? "Explorateur : " + target : "Ouvrir le dossier : " + why);
            return why.empty();
        }
    }
    return false;
}

bool FileFinderPane::activate() {
    const auto* h = selected();
    if (!h) return false;
    if (gesture(Act::Open).enabled) return run(Act::Open);
    if (gesture(Act::Import).enabled) return run(Act::Import);
    // Ni l'un ni l'autre : dire pourquoi, celui du genre (un projet s'ouvre, un paquet s'importe).
    const auto g = gesture(finder::openable(h->key) ? Act::Open : Act::Import);
    say(g.label + " : " + g.why);
    return false;
}

void FileFinderPane::say(std::string text) {
    message_ = std::move(text);
    invalidate();
}

ui::Button& FileFinderPane::actionButton(Act a) noexcept { return *actions_[static_cast<int>(a)]; }
ui::Button& FileFinderPane::mineButton() noexcept { return *side_->mine; }

ui::Checkbox* FileFinderPane::extensionBox(std::string_view key) noexcept {
    const std::size_t i = catalogIndex(key);
    return i < side_->boxes.size() ? side_->boxes[i] : nullptr;
}

gfx::Rect FileFinderPane::chipRect(std::string_view key) const {
    for (const auto& [k, r] : chips_->chipRects())
        if (k == key) return r;
    return {};
}

ui::Checkbox* FileFinderPane::optionBox(Option o) noexcept {
    const auto i = static_cast<std::size_t>(o);
    return i < std::size(side_->options) ? side_->options[i] : nullptr;
}

std::vector<std::string> FileFinderPane::contextLabels() const {
    std::vector<std::string> out;
    const auto* h = selected();
    if (!h) return out;
    out.push_back(h->name);
    for (int i = 0; i < 5; ++i) {
        const auto g = gesture(static_cast<Act>(i));
        out.push_back(g.label + (g.enabled ? std::string{} : " (gris\xC3\xA9 : " + g.why + ")"));
    }
    return out;
}

bool FileFinderPane::chooseContext(Act a) {
    table_->contextAction->emit(kMenuBase + static_cast<int>(a));
    return true;
}

void FileFinderPane::refreshButtons() {
    const bool running = progress_.running;
    go_->setEnabled(!running);
    stop_->setEnabled(running);
    go_->setTooltip(running ? "Une recherche tourne : Arr\xC3\xAAter d'abord, ou attends sa fin"
                            : "Chercher les fichiers de l'appli (toutes les extensions du catalogue ; les cases choisissent ce qui se montre)");
    const bool wholePc = where() == finder::Where::WholePc;
    for (int i = 0; i < 2; ++i) side_->options[i]->setEnabled(wholePc);
    for (int i = 0; i < 5; ++i) {
        const auto g = gesture(static_cast<Act>(i));
        actions_[i]->setEnabled(g.enabled);
        if (i == static_cast<int>(Act::Open) || i == static_cast<int>(Act::Import)) actions_[i]->setText(g.label);
        actions_[i]->setTooltip(g.enabled ? g.label : g.label + " : " + g.why);
    }
    invalidateLayout();
}

// ----------------------------------------------------------- les evenements ----
ui::EventResult FileFinderPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // Ctrl+F : le champ du nom ; Echap dans le champ : il s'efface.
        if (k->key == ui::Key::F && k->mods.ctrl && !k->mods.shift && !k->mods.alt) {
            name_->takeFocus();
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Escape && k->mods.none() && name_->focused() && !nameFilter_.empty()) {
            setNameFilter({});
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::F1 && k->mods.none() && hosts_.help) {
            hosts_.help();
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

// ---------------------------------------------------------------- la page ----
void FileFinderPane::onLayout() {
    const auto b = bounds();
    const float pad = 8.f;
    float x = b.x + pad;
    const float y = b.y + 8.f;
    barRect_ = {b.x, b.y, b.w, 66.f};
    const float nameW = std::clamp(b.w * 0.2f, 180.f, 280.f);
    name_->setBounds({x, y, nameW, 28.f});
    x += nameW + 10.f + ui::measureWidth("O\xC3\xB9 :", kText) + 8.f;
    where_->setBounds({x, y, 230.f, 28.f});
    x += 230.f + 6.f;
    const bool folder = where() == finder::Where::Folder;
    folderField_->setVisibility(folder ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    browse_->setVisibility(folder ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (folder) {
        const float fw = std::clamp(b.w * 0.22f, 160.f, 320.f);
        folderField_->setBounds({x, y, fw, 28.f});
        browse_->setBounds({x + fw + 4.f, y, 30.f, 28.f});
        x += fw + 4.f + 30.f + 8.f;
    }
    const float goW = go_->sizeHint().preferred.w + 8.f, stopW = stop_->sizeHint().preferred.w + 8.f;
    go_->setBounds({x, y, goW, 28.f});
    stop_->setBounds({x + goW + 6.f, y, stopW, 28.f});

    const float top = b.y + 66.f;
    // 1.14.0 (captures finales) : la colonne de gauche tient sa plus large case (les marges, l'ascenseur).
    const float sideMin = std::clamp(side_->widestRow() + 24.f + 14.f, 260.f, 380.f);
    const float sideW = std::clamp(b.w * 0.24f, sideMin, std::max(sideMin, 340.f));
    sideRect_ = {b.x, top, sideW, std::max(0.f, b.h - 66.f)};
    sidePanel_->setBounds(sideRect_);
    const float rx = b.x + sideW + 1.f, rw = std::max(100.f, b.w - sideW - 1.f);
    const float chipsH = chips_->heightFor(rw);
    chips_->setBounds({rx, top, rw, chipsH});
    footRect_ = {rx, b.bottom() - 44.f, rw, 44.f};
    table_->setBounds({rx, top + chipsH, rw, std::max(40.f, footRect_.y - top - chipsH)});
    // 1.14.0 (captures finales) : LES COLONNES DANS LA LARGEUR DU TABLEAU - le Dossier (un chemin, coupe de toute facon)
    // prend ce que les autres laissent (160 px au moins) ; plus etroit, le Nom, le Type et la Version cedent jusqu'a
    // leur minimum. Avant, la Version etait coupee au bord droit ("Control Expert V15..."). Refait quand la largeur
    // change, depuis les largeurs de depart (une colonne tiree a la main reste telle quelle jusque-la).
    if (std::fabs(rw - fittedWidth_) > 0.5f) {
        fittedWidth_ = rw;
        auto cols = table_->columns();
        if (cols.size() == ColCount) {
            static constexpr float kStart[ColCount] = {260.f, 170.f, 360.f, 96.f, 144.f, 200.f};
            static constexpr float kLeast[ColCount] = {150.f, 110.f, 160.f, 96.f, 144.f, 160.f};
            const float room = std::floor(rw - 18.f);                            // l'ascenseur, le cadre
            float fixed = 0.f, give = 0.f;
            for (std::size_t i = 0; i < ColCount; ++i) {
                cols[i].width = kStart[i];
                if (i != ColDir && cols[i].visible) fixed += kStart[i];
                if ((i == ColName || i == ColType || i == ColVersion) && cols[i].visible) give += kStart[i] - kLeast[i];
            }
            float dir = room - fixed;
            if (dir < kLeast[ColDir] && give > 0.f) {
                const float take = std::min(give, kLeast[ColDir] - dir);
                for (const std::size_t i : {std::size_t{ColName}, std::size_t{ColType}, std::size_t{ColVersion}})
                    if (cols[i].visible) cols[i].width -= std::ceil(take * (kStart[i] - kLeast[i]) / give);
                float used = 0.f;
                for (std::size_t i = 0; i < ColCount; ++i)
                    if (i != ColDir && cols[i].visible) used += cols[i].width;
                dir = std::max(kLeast[ColDir], room - used);
            }
            cols[ColDir].width = std::floor(dir);
            table_->setColumns(std::move(cols));
        }
    }
    float fx = rx + 8.f;
    for (auto* a : actions_) {
        const float aw = a->sizeHint().preferred.w + 8.f;
        a->setBounds({fx, footRect_.y + 8.f, aw, 28.f});
        fx += aw + 6.f;
    }
}

void FileFinderPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.windowBg);
    // La barre du haut : le champ, Ou, les boutons ; dessous, la progression.
    ctx.r.fillRect(barRect_, c.panelBg);
    ctx.r.fillRect({barRect_.x, barRect_.bottom() - 1.f, barRect_.w, 1.f}, c.border);
    const auto nb = name_->bounds();
    ctx.r.drawText({nb.right() + 10.f, nb.y + (nb.h - ctx.r.lineHeight(kText)) * 0.5f}, "O\xC3\xB9 :", kText, c.textMuted);
    const std::string line = progressLine();
    const gfx::Color tone = progress_.running ? c.text : progress_.stopped ? c.warning : c.textMuted;
    ctx.r.drawText({b.x + 10.f, b.y + 42.f}, elide(ctx.r, line, kSmall, b.w - 20.f), kSmall, tone);
    if (progress_.running) {
        // Un trait qui court sous la barre : la recherche vit.
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const float w = std::max(60.f, b.w * 0.15f);
        const float x = b.x + static_cast<float>(std::fmod(t * 240.0, static_cast<double>(b.w + w))) - w;
        ctx.r.fillRect({std::max(b.x, x), barRect_.bottom() - 2.f, std::min(w, b.right() - std::max(b.x, x)), 2.f}, c.accent);
    }
    ctx.r.fillRect({sideRect_.right(), sideRect_.y, 1.f, sideRect_.h}, c.border);
    // Le pied : les gestes, puis le fichier choisi (ou le message, ou l'aide).
    ctx.r.fillRect(footRect_, c.panelBg);
    ctx.r.fillRect({footRect_.x, footRect_.y, footRect_.w, 1.f}, c.border);
    const float textX = actions_[4]->bounds().right() + 12.f;
    std::string foot = message_;
    if (foot.empty())
        if (const auto* h = selected()) foot = h->path;
    if (foot.empty()) foot = "Double-clic : ouvrir avec la bonne appli (API Studio ou IHM Studio) ; clic droit : les gestes";
    ctx.r.drawText({textX, footRect_.y + (footRect_.h - ctx.r.lineHeight(kSmall)) * 0.5f},
                   elide(ctx.r, foot, kSmall, footRect_.right() - textX - 8.f), kSmall, c.textMuted);
}

void FileFinderPane::onPaintOverlay(const ui::PaintContext& ctx) {
    // Rien a montrer : le dire, au milieu du tableau (par-dessus lui).
    if (!rows_.empty()) return;
    const auto tb = table_->bounds();
    const std::string empty = progress_.running ? "Recherche en cours\xE2\x80\xA6"
                            : hits_.empty()     ? "Lance une recherche : Chercher."
                                                : "Rien \xC3\xA0 montrer : coche d'autres extensions, ou efface le nom.";
    const float tw = ctx.r.measure(empty, kText).width;
    ctx.r.drawText({tb.x + (tb.w - tw) * 0.5f, tb.y + 60.f}, empty, kText, ctx.theme.color.textMuted);
}

} // namespace app
