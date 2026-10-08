// =============================================================================
//  app/screens/TreeWorkspace.cpp - lot API 8 : l'arbre du projet (la colonne
//  de gauche de l'ecran d'analyse), plus pratique et plus lisible.
// -----------------------------------------------------------------------------
//  LE FILTRE AU-DESSUS DE L'ARBRE (Ctrl+Maj+F, "Filtrer l'arbre").
//   - En direct, l'arbre ne garde que les noeuds dont le texte correspond
//     (ui::SearchQuery : mots ET, "phrase", -exclu ; ni casse ni accents), et
//     leurs ancetres ; il deplie ce qu'il faut et surligne les lettres trouvees
//     (TreeView::setHighlight). Les membres des variables ne sont pas parcourus
//     (ils sont des milliers) : Aller a... les trouve.
//   - Il cherche aussi DANS le contenu, avec l'index d'Aller a... (Ctrl+K) :
//     sections, types, vues, variables API et IHM, recettes, alarmes, scripts.
//     Ce qui n'est pas deja une ligne trouvee de l'arbre devient un noeud
//     FilterHit a la fin du dossier de son domaine (API, IHM), avec, en gris,
//     ou il est ; un clic l'ouvre comme Aller a... (goToResult).
//   - Sous le champ : "N resultats pour << purge >>". Echap efface le filtre,
//     et l'arbre revient deplie comme il etait avant.
//  Les scripts : arbre-filtre "texte" (vide : efface), arbre-lignes.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../GoToSearch.hpp"
#include "../../project/SharedLibrary.hpp"
#include "../../ui/TextSearch.hpp"
#include "../../hmi/HmiBuildState.hpp"   // 1.11 (chantier T3, C4) : la legende des icones
#include "../../ui/widgets/TrailSymbols.hpp"   // 1.11 (chantier T3, tranche 11) : la legende tracee au trait

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace app {

namespace {

using Kind = ProjectTreeModel::NodeKind;

// "N resultats pour << purge >>", en petit et en gris, sous le champ.
class TreeFilterInfo final : public ui::Widget {
public:
    explicit TreeFilterInfo(std::string id) : Widget(std::move(id)) {}
    void setText(std::string t) {
        if (t == text_) return;
        text_ = std::move(t);
        invalidate();
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto r = bounds();
        const auto font = ctx.theme.font.smallUi;
        const float h = ctx.r.lineHeight(font);
        ctx.r.drawText({r.x + 8.f, r.y + (r.h - h) * 0.5f}, text_, font, ctx.theme.color.textMuted);
    }

private:
    std::string text_;
};

// Les categories d'Aller a... que le filtre montre dans l'arbre, et leur domaine.
bool wantedGroup(int group, bool& hmi) {
    using namespace gotosearch;
    switch (group) {
    case GSection: case GType: case GApiVar: hmi = false; return true;
    case GView: case GHmiVar: case GRecipe: case GAlarm: case GScript: hmi = true; return true;
    default: return false;
    }
}

// Le premier mot d'une ligne de l'arbre ("Vue_A [3]" -> "Vue_A").
std::string firstWord(const std::string& s) {
    const auto end = s.find_first_of(" \t([");
    return s.substr(0, end);
}

// ---- 2e partie : le rail et le pied ------------------------------------------
// La couleur d'un domaine (celle de l'arbre : CellStyle::domain).
gfx::Color domainColour(const ui::Theme& th, std::uint8_t d) {
    switch (d) {
    case 1: return th.color.accent;
    case 2: return th.color.info;
    case 3: return th.color.ok;
    case 4: return th.color.syntaxKeyword;
    default: return th.color.textMuted;
    }
}

// LE RAIL : une colonne d'icones a gauche de l'arbre - en haut Tout, Epingles,
// API, IHM, Simulation, Versions (la portee) ; en bas Suivre l'onglet actif,
// Tout replier, la densite. Chaque icone porte son etat (une pastille, un
// point) ; le libelle est en infobulle ; un clic appelle onPick(cle).
class TreeRail final : public ui::Widget {
public:
    struct Item {
        std::string  key, tip;
        ui::Icon     icon{ui::Icon::None};
        std::uint8_t domain{0};
        std::string  badge;                  // "5" ; "." : un point seul ; vide : rien
        ui::Tone     tone{ui::Tone::None};
        bool         on{false};
        bool operator==(const Item& o) const {
            return key == o.key && tip == o.tip && icon == o.icon && domain == o.domain && badge == o.badge && tone == o.tone && on == o.on;
        }
    };
    explicit TreeRail(std::string id) : Widget(std::move(id)) {}
    std::function<void(const std::string&)> onPick;
    void setItems(std::vector<Item> top, std::vector<Item> bottom) {
        if (top == top_ && bottom == bottom_) return;
        top_ = std::move(top);
        bottom_ = std::move(bottom);
        invalidate();
    }
    [[nodiscard]] const std::vector<Item>& top() const noexcept { return top_; }
    [[nodiscard]] bool hasTooltip() const override { return true; }
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override {
        for (const auto& [r, k] : hits_)
            if (r.contains(mouse)) {
                const Item* it = itemAt(k);
                return it ? it->tip : std::string{};
            }
        return {};
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        ctx.r.fillRect(b, c.rowAltBg);
        ctx.r.line({b.right() - 0.5f, b.y}, {b.right() - 0.5f, b.bottom()}, c.border, 1.f);
        hits_.clear();
        const float h = 32.f;
        const auto font = ctx.theme.font.caption;
        const float lh = ctx.r.lineHeight(font);
        const auto draw = [&](const Item& it, float y, std::size_t index) {
            const gfx::Rect r{b.x + 3.f, y, std::max(0.f, b.w - 7.f), h};
            const auto col = domainColour(ctx.theme, it.domain);
            if (it.on) {
                ctx.r.fillRoundedRect(r, (it.domain ? col : c.textMuted).withAlpha(ctx.theme.isDark() ? 60 : 40), 6.f);
                ctx.r.fillRect({r.x, r.y + 6.f, 3.f, r.h - 12.f}, it.domain ? col : c.text);
            } else if (hovered() && r.contains(mouse_)) {
                ctx.r.fillRoundedRect(r, ctx.theme.brand.hover, 6.f);
            }
            ui::drawIcon(ctx.r, it.icon, {r.x + (r.w - 18.f) * 0.5f, r.y + (r.h - 18.f) * 0.5f, 18.f, 18.f},
                         it.domain ? col : (it.on ? c.text : c.textMuted));
            if (it.badge == ".") {
                ctx.r.fillRoundedRect({r.right() - 10.f, r.y + 4.f, 7.f, 7.f}, ctx.theme.tone(it.tone, c.textMuted), 3.5f);
            } else if (!it.badge.empty()) {
                const float w = ctx.r.measure(it.badge, font).width + 8.f;
                const gfx::Rect p{r.right() - w + 2.f, r.y + 1.f, w, lh + 1.f};
                const bool toned = it.tone != ui::Tone::None;
                ctx.r.fillRoundedRect(p, toned ? ctx.theme.tone(it.tone, c.textMuted) : c.border, p.h * 0.5f);
                ctx.r.drawText({p.x + 4.f, p.y + 0.5f}, it.badge, font, toned ? c.panelBg : c.text);
            }
            hits_.push_back({r, index});
        };
        float y = b.y + 6.f;
        for (std::size_t k = 0; k < top_.size(); ++k, y += h + 2.f) draw(top_[k], y, k);
        float yb = std::max(y + 8.f, b.bottom() - 6.f - static_cast<float>(bottom_.size()) * (h + 2.f));
        for (std::size_t k = 0; k < bottom_.size(); ++k, yb += h + 2.f) draw(bottom_[k], yb, top_.size() + k);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            mouse_ = m->pos;
            invalidate();
            return ui::EventResult::Ignored;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (!bounds().contains(d->pos)) return ui::EventResult::Ignored;
            if (d->button != ui::MouseButton::Left) return ui::EventResult::Consumed;
            for (const auto& [r, k] : hits_)
                if (r.contains(d->pos)) {
                    const Item* it = itemAt(k);
                    const std::string key = it ? it->key : std::string{};
                    if (!key.empty() && onPick) onPick(key);
                    return ui::EventResult::Consumed;
                }
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    [[nodiscard]] const Item* itemAt(std::size_t k) const {
        if (k < top_.size()) return &top_[k];
        return k - top_.size() < bottom_.size() ? &bottom_[k - top_.size()] : nullptr;
    }
    std::vector<Item> top_, bottom_;
    mutable std::vector<std::pair<gfx::Rect, std::size_t>> hits_;
    gfx::Point mouse_{-1.f, -1.f};
};

// LA SANTE DU PROJET, en pied : "Simulation en marche . 2 expressions
// impossibles . V48 modifie" - chaque morceau cliquable (onPick(cle)).
class TreeFoot final : public ui::Widget {
public:
    struct Part {
        std::string text, key;
        ui::Tone    tone{ui::Tone::None};
        bool        dot{false};             // un point de couleur devant (la simulation)
        bool operator==(const Part& o) const { return text == o.text && key == o.key && tone == o.tone && dot == o.dot; }
    };
    explicit TreeFoot(std::string id) : Widget(std::move(id)) {}
    std::function<void(const std::string&)> onPick;
    void setParts(std::vector<Part> parts) {
        if (parts == parts_) return;
        parts_ = std::move(parts);
        invalidate();
    }
    [[nodiscard]] const std::vector<Part>& parts() const noexcept { return parts_; }
    [[nodiscard]] bool hasTooltip() const override { return true; }
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override {
        for (const auto& [r, k] : hits_)
            if (r.contains(mouse) && k < parts_.size()) return "Un clic : " + parts_[k].text;
        return "La sant\xC3\xA9 du projet : chaque morceau ouvre l'endroit qui r\xC3\xA8gle.";
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        ctx.r.fillRect(b, c.rowAltBg);
        ctx.r.line({b.x, b.y + 0.5f}, {b.right(), b.y + 0.5f}, c.border, 1.f);
        hits_.clear();
        const auto font = ctx.theme.font.smallUi;
        const float lh = ctx.r.lineHeight(font);
        const float ty = b.y + (b.h - lh) * 0.5f;
        float x = b.x + 8.f;
        ctx.r.pushClip(b);
        for (std::size_t k = 0; k < parts_.size(); ++k) {
            const auto& p = parts_[k];
            if (k > 0) {
                ctx.r.drawText({x, ty}, "\xC2\xB7", font, c.textMuted);
                x += ctx.r.measure("\xC2\xB7", font).width + 6.f;
            }
            const float x0 = x;
            const auto col = p.tone == ui::Tone::None ? c.textMuted : ctx.theme.onSurface(ctx.theme.tone(p.tone, c.textMuted));
            if (p.dot) {
                ctx.r.fillRoundedRect({x, b.y + (b.h - 8.f) * 0.5f, 8.f, 8.f}, ctx.theme.tone(p.tone, c.textMuted), 4.f);
                x += 12.f;
            }
            ctx.r.drawText({x, ty}, p.text, font, p.dot ? c.textMuted : col);
            x += ctx.r.measure(p.text, font).width;
            hits_.push_back({{x0, b.y, x - x0, b.h}, k});
            x += 6.f;
        }
        ctx.r.popClip();
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (!bounds().contains(d->pos)) return ui::EventResult::Ignored;
            for (const auto& [r, k] : hits_)
                if (r.contains(d->pos) && k < parts_.size()) {
                    const std::string key = parts_[k].key;
                    if (onPick) onPick(key);
                    break;
                }
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    std::vector<Part> parts_;
    mutable std::vector<std::pair<gfx::Rect, std::size_t>> hits_;
};

} // namespace

// ------------------------------------------------------------ la colonne ----
ui::WidgetPtr MainAnalysisScreen::wrapExplorer(std::unique_ptr<ui::TreeView> tree) {
    using Side = ui::DockLayout::Side;
    auto column = std::make_unique<ui::DockLayout>("analysis.explorerColumn");
    // 2e partie : le rail a gauche (toute la hauteur), la sante du projet en pied.
    auto rail = std::make_unique<TreeRail>("analysis.treeRail");
    rail->onPick = [this](const std::string& key) {
        if (key == "suivre") setTreeFollow(!treeFollow_);
        else if (key == "replier") collapseTree();
        else if (key == "legende") {}           // 1.11 (chantier T3, C4) : la legende est son infobulle
        else if (key == "densite") {
            const float h = explorer_ ? explorer_->rowHeightOverride() : 0.f;
            (void)setTreeDensity(h == 22.f ? "large" : h == 26.f ? "normal" : "serre");
        } else (void)setTreeScope(key);
    };
    treeRail_ = &column->dock(std::move(rail), Side::Left, 40.f);
    auto foot = std::make_unique<TreeFoot>("analysis.treeFoot");
    foot->onPick = [this](const std::string& key) { openTreeHealth(key); };
    treeFoot_ = &column->dock(std::move(foot), Side::Bottom, 24.f);
    auto field = std::make_unique<ui::InputText>("analysis.treeFilter");
    field->setPlaceholder("Filtrer l'arbre   (Ctrl+Maj+F)");
    field->setEscapeClears(true);
    field->setTooltip("Filtrer l'arbre (Ctrl+Maj+F) : il ne garde que ce qui correspond, et cherche aussi dans le contenu "
                      "(sections, types, vues, variables, recettes, alarmes, scripts). \xC3\x89" "chap efface le filtre.");
    treeFilter_ = &static_cast<ui::InputText&>(column->dock(std::move(field), Side::Top, 30.f));
    auto info = std::make_unique<TreeFilterInfo>("analysis.treeFilterInfo");
    info->setVisibility(ui::Visibility::Collapsed);
    treeFilterInfo_ = &column->dock(std::move(info), Side::Top, 20.f);
    explorer_ = &static_cast<ui::TreeView&>(column->dock(std::move(tree), Side::Center, 0.f));
    // 2e partie : la densite retenue, suivre l'onglet actif (en marche par defaut),
    // les actions au survol d'une ligne.
    const auto density = app_.settings().getString("arbre.densite", "normal");
    explorer_->setRowHeightOverride(density == "serre" ? 22.f : density == "large" ? 26.f : 0.f);
    treeFollow_ = app_.settings().getBool("arbre.suivre", true);
    // Pas pendant un script : ses clics visent la droite des lignes (ScriptRunner, arbre "...").
    if (!app_.scripted())
        explorer_->setHoverActions({{ui::Icon::Star, "\xC3\x89pingler en haut / D\xC3\xA9s\xC3\xA9pingler"},
                                {ui::Icon::Export, "Ouvrir dans une fen\xC3\xAAtre d\xC3\xA9tach\xC3\xA9" "e"},
                                {ui::Icon::Settings, "Plus\xE2\x80\xA6 (le menu du clic droit)"}});
    refreshTreeChrome();
    return column;
}

// -------------------------------------------------------------- le filtre ----
void MainAnalysisScreen::setTreeFilter(const std::string& text) {
    if (treeFilter_ && treeFilter_->text() != text) treeFilter_->setText(text);
    applyTreeFilter(text);     // setText ne previent pas toujours : on applique ici
}

void MainAnalysisScreen::focusTreeFilter() {
    if (treeFilter_) treeFilter_->focusAndSelectAll();
}

void MainAnalysisScreen::applyTreeFilter(const std::string& raw) {
    // Les espaces au bord ne comptent pas.
    const auto b = raw.find_first_not_of(" \t");
    const std::string text = b == std::string::npos ? std::string{} : raw.substr(b, raw.find_last_not_of(" \t") - b + 1);
    if (!explorer_ || !treeModel_) return;
    const bool was = !treeFilterText_.empty();
    if (text == treeFilterText_ && was) return;
    auto* info = dynamic_cast<TreeFilterInfo*>(treeFilterInfo_);

    if (text.empty()) {
        treeFilterText_.clear();
        treeFilterCount_ = 0;
        treeFilterResults_.clear();
        treeModel_->setFilterHits({});
        treeModel_->setFiltering(false);                 // les versions en bref reviennent
        explorer_->setFilter({});
        explorer_->setHighlight("");
        if (was) explorer_->setExpandedNodes(treeFilterSaved_);
        treeFilterSaved_.clear();
        if (treeFilterInfo_) treeFilterInfo_->setVisibility(ui::Visibility::Collapsed);
        return;
    }
    if (!was) treeFilterSaved_ = explorer_->expandedNodes();
    treeFilterText_ = text;
    const ui::SearchQuery query(text);
    // 1.11 (chantier T3, C4) : les deux filtres de l'arbre, des mots reserves du
    // filtre : ce qui ne compile pas (✕ et ⊘), ce qui n'est pas genere (les scripts).
    const std::uint8_t buildWant = text == kTreeFilterNotCompiling ? ProjectTreeModel::BuildNotCompiling
                                 : text == kTreeFilterNotGenerated ? ProjectTreeModel::BuildNotGenerated : 0;

    // 1. L'arbre lui-meme, sans les resultats d'un filtre precedent. En
    //    profondeur, le chemin en pile : un noeud trouve garde ses ancetres,
    //    qui se deplient. Les membres des variables ne sont pas parcourus.
    treeModel_->setFilterHits({});
    treeModel_->setFiltering(true);                      // toutes les versions sont parcourues
    const auto* model = treeModel_.get();
    std::unordered_set<ui::NodeId> keep;
    std::vector<ui::NodeId> open;
    std::unordered_set<std::string> found;           // le premier mot des lignes trouvees
    std::size_t matches = 0, visited = 0;
    struct Frame { ui::NodeId node; std::size_t depth; };
    std::vector<Frame> stack{{model->root(), 0}};
    std::vector<ui::NodeId> path;                    // les ancetres du noeud en cours
    keep.insert(model->root());
    while (!stack.empty() && visited < 60000) {
        const auto f = stack.back();
        stack.pop_back();
        ++visited;
        path.resize(f.depth);
        if (f.depth > 0) {
            const std::string t = model->text(f.node);
            if (buildWant != 0 ? (model->buildFlags(f.node) & buildWant) != 0 : query.matches({std::string_view(t)})) {
                ++matches;
                found.insert(firstWord(t));
                keep.insert(f.node);
                for (const auto a : path) {
                    keep.insert(a);
                    open.push_back(a);
                }
            }
        }
        path.push_back(f.node);
        if (ProjectTreeModel::holdsMembers(f.node) || f.depth >= 8) continue;
        for (std::size_t i = model->childCount(f.node); i > 0; --i)
            stack.push_back({model->childAt(f.node, i - 1), f.depth + 1});
    }

    // 2. Le contenu, par l'index d'Aller a... : ce que l'arbre n'a pas deja.
    std::vector<ProjectTreeModel::FilterHitRow> rows;
    treeFilterResults_.clear();
    auto content = buildWant != 0 ? GoToPanel::Outcome{} : goToSearchAll(text, -1);   // 1.11 (C4) : pas pour les deux filtres
    for (auto& r : content.results) {
        bool hmi = false;
        if (!wantedGroup(r.group, hmi)) continue;
        if (found.count(r.title) != 0) continue;
        ProjectTreeModel::FilterHitRow row;
        row.title = r.title;
        row.hint = r.subtitle.empty() ? std::string(gotosearch::groupChip(r.group)) : r.subtitle;
        row.icon = r.icon;
        row.hmi = hmi && app_.hmi() != nullptr;
        rows.push_back(std::move(row));
        treeFilterResults_.push_back(std::move(r));
    }
    const std::size_t hits = rows.size();
    treeModel_->setFilterHits(std::move(rows));
    // Le dossier de leur domaine se deplie, et reste (API, IHM ; sans IHM, la racine).
    for (std::size_t c = 0; c < model->childCount(model->root()); ++c) {
        const auto child = model->childAt(model->root(), c);
        const auto k = ProjectTreeModel::kindOf(child);
        if (hits > 0 && (k == Kind::ApiFolder || k == Kind::HmiFolder)) {
            keep.insert(child);
            open.push_back(child);
        }
    }
    open.push_back(model->root());

    explorer_->setExpandedNodes(std::move(open));
    explorer_->setFilter([keep = std::move(keep)](ui::NodeId n) {
        return keep.count(n) != 0 || ProjectTreeModel::kindOf(n) == Kind::FilterHit;
    });
    explorer_->setHighlight(text);

    treeFilterCount_ = matches + hits;
    if (info) {
        // 1.11 (chantier T3, C4) : un filtre de Compiler / Generer dit ce qu'il montre, pas son mot reserve.
        if (buildWant != 0)
            info->setText((buildWant == ProjectTreeModel::BuildNotCompiling ? std::string("Ce qui ne compile pas : ")
                                                                           : std::string("Ce qui n'est pas g\xC3\xA9n\xC3\xA9r\xC3\xA9 : "))
                          + std::to_string(treeFilterCount_));
        else
            info->setText(std::to_string(treeFilterCount_) + (treeFilterCount_ > 1 ? " r\xC3\xA9sultats" : " r\xC3\xA9sultat")
                          + " pour \xC2\xAB " + text + " \xC2\xBB");
        info->setVisibility(ui::Visibility::Visible);
    }
}

bool MainAnalysisScreen::openTreeFilterHit(ui::NodeId node) {
    // "Voir les N versions..." : toutes les versions, a leur place.
    if (ProjectTreeModel::kindOf(node) == Kind::VersionsMore) {
        showAllTreeVersions(true);
        return true;
    }
    // Un raccourci (Epingles, Recents) : le noeud vise, comme un clic dessus.
    if (treeModel_) {
        if (const auto target = treeModel_->shortcutTarget(node); target != ui::kInvalidNode) {
            onTreeSelection(target);
            return true;
        }
    }
    if (ProjectTreeModel::kindOf(node) != Kind::FilterHit) {
        noteTreeRecent(node);                            // ouvert depuis l'arbre : Recents
        return false;
    }
    const auto i = ProjectTreeModel::indexOf(node);
    if (i < treeFilterResults_.size()) goToResult(treeFilterResults_[i]);
    return true;
}

// ------------------------------------------------ les outils sortis de l'arbre ----
bool MainAnalysisScreen::openTreeTool(bool hmi, std::size_t k) {
    if (!treeModel_ || (hmi && !app_.hmi()) || k >= ProjectTreeModel::toolCount(hmi)) return false;
    onTreeSelection(ProjectTreeModel::toolNode(hmi, k));
    return true;
}

bool MainAnalysisScreen::openTreeTool(bool hmi, const std::string& name) {
    if (!treeModel_ || name.empty()) return false;
    const auto lowerAscii = [](std::string s) {
        for (auto& ch : s)
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        return s;
    };
    const auto want = lowerAscii(name);
    const auto chips = treeModel_->style(ProjectTreeModel::toolRowNode(hmi)).chips;
    for (std::size_t k = 0; k < ProjectTreeModel::toolCount(hmi); ++k) {
        const auto old = lowerAscii(treeModel_->text(ProjectTreeModel::toolNode(hmi, k)));
        const auto chip = k < chips.size() ? lowerAscii(chips[k].label) : std::string{};
        if (old.rfind(want, 0) == 0 || chip.rfind(want, 0) == 0) return openTreeTool(hmi, k);
    }
    return false;
}

// -------------------------------------------------------- les versions en bref ----
void MainAnalysisScreen::showAllTreeVersions(bool all) {
    if (!treeModel_ || !explorer_ || !treeModel_->setAllVersions(all)) return;
    explorer_->setExpandedNodes(explorer_->expandedNodes());   // les lignes se refont
    if (all) explorer_->ensureVisible(ProjectTreeModel::versionsFolderNode());
}

// ------------------------------------------------------------ les pastilles ----
void MainAnalysisScreen::setTreeExprErrors(std::size_t count, std::size_t inViews) {
    treeExprErrors_ = count;
    treeExprErrorsInViews_ = inViews;
    treeExprFolder_ = app_.projectFolder();
    if (treeModel_ && treeModel_->setHmiExprErrors(count, inViews) && explorer_) explorer_->invalidate();
}

// ------------------------------------------------------ epingles et recents ----
namespace {

// Ce qu'on epingle ou retient : pas les titres de domaine ni les lignes du lot.
bool shortcutable(ui::NodeId n) {
    switch (ProjectTreeModel::kindOf(n)) {
    case Kind::Root: case Kind::ApiFolder: case Kind::HmiFolder: case Kind::ToolRow: case Kind::VersionsMore:
    case Kind::FilterHit: case Kind::PinsFolder: case Kind::PinItem: case Kind::RecentFolder: case Kind::RecentItem:
    case Kind::MemberNode:
        return false;
    default:
        return true;
    }
}

// Le premier mot, sans '|' (le separateur des listes des reglages).
std::string pinWord(const std::string& s) {
    std::string w = firstWord(s);
    w.erase(std::remove(w.begin(), w.end(), '|'), w.end());
    return w;
}

} // namespace

std::string MainAnalysisScreen::treePinsKey() const {
    std::string id = app_.projectFolder();
    if (id.empty())
        if (const auto p = app_.project()) id = p->header.projectName;
    std::string key = "arbre.epingles.";
    for (const char ch : id) key += std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_';
    return key;
}

bool MainAnalysisScreen::treePinnable(ui::NodeId node) { return shortcutable(node); }

bool MainAnalysisScreen::isTreePinned(ui::NodeId node) const {
    return std::find(treePins_.begin(), treePins_.end(), node) != treePins_.end();
}

void MainAnalysisScreen::loadTreePins() {
    treePins_.clear();
    treeRecents_.clear();
    treeShortcutsDirty_ = false;
    if (!treeModel_) return;
    for (const auto& line : app_.settings().getList(treePinsKey())) {
        const auto sep = line.find('~');
        const auto id = static_cast<ui::NodeId>(std::strtoull(line.substr(0, sep).c_str(), nullptr, 10));
        const std::string was = sep == std::string::npos ? std::string{} : line.substr(sep + 1);
        if (id == 0 || id == ui::kInvalidNode || !shortcutable(id) || isTreePinned(id)) continue;
        const auto now = treeModel_->text(id);
        if (now.empty() || (!was.empty() && pinWord(now) != was)) continue;      // plus le meme noeud
        treePins_.push_back(id);
    }
    refreshTreeShortcuts();
}

void MainAnalysisScreen::saveTreePins() const {
    std::vector<std::string> lines;
    for (const auto n : treePins_) lines.push_back(std::to_string(n) + "~" + (treeModel_ ? pinWord(treeModel_->text(n)) : std::string{}));
    app_.settings().setList(treePinsKey(), lines);
}

bool MainAnalysisScreen::pinTreeNode(ui::NodeId node, bool pin) {
    if (treeModel_) {
        const auto target = treeModel_->shortcutTarget(node);
        if (target != ui::kInvalidNode) node = target;              // depuis Epingles ou Recents
    }
    if (!treeModel_ || !shortcutable(node) || isTreePinned(node) == pin) return false;
    if (pin) treePins_.push_back(node);
    else treePins_.erase(std::remove(treePins_.begin(), treePins_.end(), node), treePins_.end());
    saveTreePins();
    refreshTreeShortcuts();
    return true;
}

bool MainAnalysisScreen::pinTreePath(const std::string& path, bool pin) {
    const auto at = treeNodeOfPath(path);
    return at != ui::kInvalidNode && pinTreeNode(at, pin);
}

ui::NodeId MainAnalysisScreen::treeNodeOfPath(const std::string& path) const {
    if (!treeModel_ || path.empty()) return ui::kInvalidNode;
    // Chaque morceau : le premier descendant (4 niveaux au plus, sans les
    // membres des variables) dont le texte commence ainsi, sous le precedent.
    ui::NodeId at = treeModel_->root();
    for (std::size_t from = 0; from <= path.size();) {
        const auto slash = path.find('/', from);
        const std::string part = path.substr(from, slash == std::string::npos ? std::string::npos : slash - from);
        std::vector<std::pair<ui::NodeId, int>> todo{{at, 0}};
        ui::NodeId hit = ui::kInvalidNode;
        for (std::size_t i = 0; i < todo.size() && hit == ui::kInvalidNode && i < 20000; ++i) {
            const auto [n, depth] = todo[i];
            if (ProjectTreeModel::holdsMembers(n) || depth >= 4) continue;
            for (std::size_t c = 0; c < treeModel_->childCount(n); ++c) {
                const auto child = treeModel_->childAt(n, c);
                const auto kind = ProjectTreeModel::kindOf(child);
                if (kind == Kind::PinsFolder || kind == Kind::RecentFolder) continue;
                if (treeModel_->text(child).rfind(part, 0) == 0) { hit = child; break; }
                todo.emplace_back(child, depth + 1);
            }
        }
        if (hit == ui::kInvalidNode) return ui::kInvalidNode;
        at = hit;
        if (slash == std::string::npos) break;
        from = slash + 1;
    }
    return at;
}

// 1.11.12 : arbre-parcourir. Profondeur d'abord, dans l'ordre des lignes.
std::vector<ui::NodeId> MainAnalysisScreen::treeSubtree(ui::NodeId from, std::size_t cap) const {
    std::vector<ui::NodeId> out;
    if (!treeModel_ || from == ui::kInvalidNode) return out;
    std::vector<ui::NodeId> todo{from};
    while (!todo.empty() && out.size() < cap) {
        const auto n = todo.back();
        todo.pop_back();
        const auto kind = ProjectTreeModel::kindOf(n);
        if (kind == Kind::PinsFolder || kind == Kind::PinItem || kind == Kind::RecentFolder || kind == Kind::RecentItem) continue;
        out.push_back(n);
        if (ProjectTreeModel::holdsMembers(n)) continue;
        const auto count = treeModel_->childCount(n);
        for (std::size_t c = count; c-- > 0;) todo.push_back(treeModel_->childAt(n, c));
    }
    return out;
}

std::string MainAnalysisScreen::visitTreeNode(ui::NodeId node) {
    if (!treeModel_ || node == ui::kInvalidNode) return {};
    std::string text = treeModel_->text(node);
    (void)treeModel_->style(node);
    (void)treeCardText(node);
    // Le menu du clic droit, construit comme a l'ouverture (sans s'ouvrir).
    if (!contextMenu_ || !contextMenu_->isOpen()) {
        contextNode_ = node;
        std::vector<ui::PopupMenu::Item> items;
        TreeKeys keys;
        buildExplorerMenu(node, items, keys);
        contextCalls_.clear();
    }
    onTreeSelection(node);
    return text;
}

void MainAnalysisScreen::noteTreeRecent(ui::NodeId node) {
    if (!shortcutable(node)) return;
    treeRecents_.erase(std::remove(treeRecents_.begin(), treeRecents_.end(), node), treeRecents_.end());
    treeRecents_.insert(treeRecents_.begin(), node);
    if (treeRecents_.size() > 8) treeRecents_.resize(8);
    // Les lignes se refont un peu plus tard (tickTree) : pas sous la souris,
    // entre les deux clics d'un double-clic.
    treeShortcutsDirty_ = true;
    treeRecentAt_ = treeNow_;
}

void MainAnalysisScreen::tickTree(double now) {
    treeNow_ = now;
    if (treeShortcutsDirty_ && now - treeRecentAt_ > 0.8) {
        treeShortcutsDirty_ = false;
        refreshTreeShortcuts();
    }
    // 2e partie : suivre l'onglet actif ; le rail et le pied deux fois par seconde ;
    // les mises a jour de bibliotheque quand le projet a change (au plus une fois par seconde).
    followActiveTab();
    if (explorer_ && app_.scripted() && explorer_->hasHoverActions()) explorer_->setHoverActions({});   // un script : pas d'actions au survol
    if (now - treeChromeAt_ >= 0.5) {
        treeChromeAt_ = now;
        const auto project = app_.project();
        const std::uint64_t rev = project ? app_.commands().revision() * 1000003ULL + reinterpret_cast<std::uintptr_t>(project.get()) : 0u;
        if (rev != treeLibRevision_ && treeModel_) {
            treeLibRevision_ = rev;
            std::size_t ddt = 0, dfb = 0;
            if (const auto library = project ? apiLibrary() : nullptr)
                for (const auto& n : library->outdated(*project))
                    (n.kind == project::LibraryItemKind::FunctionBlock ? dfb : ddt) += 1;
            if (treeModel_->setLibraryUpdates(ddt, dfb) && explorer_) explorer_->invalidate();
        }
        refreshTreeChrome();
    }
}

void MainAnalysisScreen::refreshTreeShortcuts() {
    if (!treeModel_ || !explorer_) return;
    const auto domain = [](ui::NodeId n) -> std::string {
        const auto k = ProjectTreeModel::kindOf(n);
        if (ProjectTreeModel::isSimNode(n)) return "Simulation";
        if (k == Kind::VersionsFolder || k == Kind::VersionItem) return "Versions";
        return isHmiNode(n) ? "IHM" : "API";
    };
    std::vector<ProjectTreeModel::ShortcutRow> pins, recents;
    for (const auto n : treePins_) pins.push_back({n, domain(n)});
    // Les 4 derniers, sans ceux qui sont deja epingles.
    for (const auto n : treeRecents_)
        if (!isTreePinned(n) && recents.size() < 4) recents.push_back({n, domain(n)});
    // Un dossier qui apparait (il etait vide) arrive deplie ; ensuite, il reste comme on l'a mis.
    const bool pinsNew = treeModel_->childCount(ProjectTreeModel::pinsFolderNode()) == 0 && !pins.empty();
    const bool recentsNew = treeModel_->childCount(ProjectTreeModel::recentsFolderNode()) == 0 && !recents.empty();
    treeModel_->setShortcuts(std::move(pins), std::move(recents));
    auto open = explorer_->expandedNodes();
    if (pinsNew) open.push_back(ProjectTreeModel::pinsFolderNode());
    if (recentsNew) open.push_back(ProjectTreeModel::recentsFolderNode());
    explorer_->setExpandedNodes(std::move(open));
}

// ------------------------------------------------------ ce que montre l'arbre ----
std::vector<std::string> MainAnalysisScreen::treeLines() const {
    std::vector<std::string> out;
    if (!explorer_ || !explorer_->model()) return out;
    const auto& model = *explorer_->model();
    for (const auto n : explorer_->visibleNodes()) {
        if (out.size() >= 3000) { out.emplace_back("..."); break; }
        const int depth = explorer_->visibleDepth(n);
        std::string line(static_cast<std::size_t>(depth > 0 ? depth * 2 : 0), ' ');
        line += model.text(n);
        const auto st = model.style(n);
        if (!st.chips.empty()) {                     // la rangee des outils : ses boutons
            line += "[outils :";
            for (const auto& chip : st.chips) line += " " + chip.label + " |";
            line.back() = ']';
        }
        if (!st.hint.empty()) line += "   (" + st.hint + ")";
        if (!st.pill.empty()) line += "   [" + st.pill + "]";          // 2e partie : la 2e pastille (erreurs, mises a jour)
        if (!st.badge.empty()) line += "   [" + st.badge + "]";
        if (st.dotTone != ui::Tone::None) line += "   (*" + st.dotTip + ")";
        out.push_back(std::move(line));
    }
    return out;
}

// =============================================================================
//  2e PARTIE : les didacticiels vers les boutons, la portee (le rail), la
//  densite, tout replier, suivre l'onglet actif, le point orange, les mises a
//  jour de bibliotheque, la sante du projet (le pied), la carte au survol, les
//  actions au survol d'une ligne.
// =============================================================================
bool MainAnalysisScreen::treeToolRect(ui::NodeId oldNode, gfx::Rect& out) const {
    if (!explorer_ || oldNode == ui::kInvalidNode) return false;
    for (const bool hmi : {false, true})
        for (std::size_t k = 0; k < ProjectTreeModel::toolCount(hmi); ++k)
            if (ProjectTreeModel::toolNode(hmi, k) == oldNode)
                return explorer_->chipRect(ProjectTreeModel::toolRowNode(hmi), k, out);
    return false;
}

bool MainAnalysisScreen::revealTreeTool(ui::NodeId oldNode) {
    if (oldNode == ui::kInvalidNode) return false;
    for (const bool hmi : {false, true})
        for (std::size_t k = 0; k < ProjectTreeModel::toolCount(hmi); ++k)
            if (ProjectTreeModel::toolNode(hmi, k) == oldNode) return revealTreeNode(ProjectTreeModel::toolRowNode(hmi));
    return false;
}

namespace {
const char* const kTreeScopes[] = {"tout", "epingles", "api", "ihm", "simulation", "versions"};

// L'onglet -> le dossier de l'arbre qui l'ouvre (pas les outils) ; kInvalidNode : aucun.
ui::NodeId pageTreeNode(const std::map<std::string, ui::Widget*>& apiTabs, const std::map<std::string, ui::Widget*>& hmiTabs,
                        bool hasHmi, const ui::Widget* page) {
    static const std::pair<const char*, Kind> apiKeys[] = {
        {"variables", Kind::VariablesFolder}, {"types", Kind::TypesFolder}, {"dfb", Kind::DfbFolder},
        {"unites", Kind::UnitsFolder}, {"ordre", Kind::ExecOrderFolder}, {"taches", Kind::TaskFolder},
        {"tables", Kind::TablesFolder}, {"sous-routines", Kind::SubroutinesFolder},
        {"configuration", Kind::ConfigurationFolder}, {"simulation", Kind::ApiSimulation}};
    static const std::pair<const char*, Kind> hmiKeys[] = {
        {"vues", Kind::HmiViews}, {"symboles", Kind::HmiSymbolsFolder}, {"styles", Kind::HmiStyles},
        {"essais", Kind::HmiTests}, {"config", Kind::HmiConfig}, {"fichiers", Kind::HmiExternalFiles},
        {"ressources", Kind::HmiResources}, {"scripts", Kind::HmiScripts}, {"alarmes", Kind::HmiAlarms},
        {"recettes", Kind::HmiRecipes}, {"utilisateurs", Kind::HmiUsers}, {"langues", Kind::HmiLanguages},
        {"unites", Kind::HmiUnits}, {"communication", Kind::HmiComm}, {"simulation", Kind::HmiSimulation},
        {"versions", Kind::VersionsFolder}};
    if (!page) return ui::kInvalidNode;
    for (const auto& [key, w] : apiTabs)
        if (w == page)
            for (const auto& [k, kind] : apiKeys)
                if (key == k) return ProjectTreeModel::pack(kind, 0);
    if (hasHmi)
        for (const auto& [key, w] : hmiTabs)
            if (w == page)
                for (const auto& [k, kind] : hmiKeys)
                    if (key == k) return ProjectTreeModel::pack(kind, 0);
    return ui::kInvalidNode;
}
}

bool MainAnalysisScreen::setTreeScope(const std::string& scope) {
    int k = -1;
    for (int i = 0; i < 6; ++i)
        if (scope == kTreeScopes[i]) k = i;
    if (k < 0 || !treeModel_ || !explorer_) return false;
    treeScope_ = kTreeScopes[k];
    if (treeModel_->setScope(k)) {
        // Le domaine choisi arrive deplie ; le reste garde son etat.
        auto open = explorer_->expandedNodes();
        open.push_back(treeModel_->root());
        switch (k) {
        case 1: open.push_back(ProjectTreeModel::pinsFolderNode()); open.push_back(ProjectTreeModel::recentsFolderNode()); break;
        case 2: open.push_back(ProjectTreeModel::apiFolderNode()); break;
        case 3: open.push_back(ProjectTreeModel::hmiFolderNode()); break;
        case 4: open.push_back(ProjectTreeModel::simFolderNode()); break;
        case 5: open.push_back(ProjectTreeModel::versionsFolderNode()); break;
        default: break;
        }
        explorer_->setExpandedNodes(std::move(open));
    }
    refreshTreeChrome();
    return true;
}

bool MainAnalysisScreen::setTreeDensity(const std::string& density) {
    if (!explorer_) return false;
    float h = -1.f;
    if (density == "serre" || density == "serr\xC3\xA9") h = 22.f;
    else if (density == "large") h = 26.f;
    else if (density == "normal" || density == "theme") h = 0.f;
    if (h < 0.f) return false;
    explorer_->setRowHeightOverride(h);
    app_.settings().set("arbre.densite", std::string(h == 22.f ? "serre" : h == 26.f ? "large" : "normal"));
    refreshTreeChrome();
    return true;
}

void MainAnalysisScreen::collapseTree() {
    if (!explorer_ || !treeModel_) return;
    if (!treeFilterText_.empty()) setTreeFilter("");          // le filtre rendrait l'arbre deplie d'avant
    explorer_->setExpandedNodes({treeModel_->root(), ProjectTreeModel::pinsFolderNode(), ProjectTreeModel::recentsFolderNode()});
}

void MainAnalysisScreen::setTreeFollow(bool on) {
    treeFollow_ = on;
    app_.settings().set("arbre.suivre", on);
    treeFollowedPage_ = nullptr;
    refreshTreeChrome();
    if (on) followActiveTab();
}

void MainAnalysisScreen::followActiveTab() {
    if (!treeFollow_ || !centre_ || !explorer_ || !treeModel_) return;
    const auto cur = centre_->currentIndex();
    ui::Widget* page = cur < centre_->tabCount() ? centre_->page(cur) : nullptr;
    if (page == treeFollowedPage_) return;
    treeFollowedPage_ = page;
    // Ouvert depuis l'arbre (a l'instant) : la ligne cliquee reste choisie.
    if (!page || (treeRecentAt_ > 0.0 && treeNow_ - treeRecentAt_ < 1.5)) return;
    const ui::NodeId node = pageTreeNode(apiTabs_, hmiTabs_, app_.hmi() != nullptr, page);
    if (node == ui::kInvalidNode || explorer_->currentNode() == node) return;
    // Hors de la portee du rail (IHM seulement, et l'onglet est de l'API) : l'arbre ne bouge pas.
    if (treeModel_->scope() != 0 && treeModel_->scope() != treeModel_->domainOf(node) + 1) return;
    if (revealTreeNode(node)) explorer_->setCurrentNode(node);
}

void MainAnalysisScreen::setTreeChanges(const std::vector<std::string>& categories, int since) {
    if (!treeModel_) return;
    std::vector<Kind> kinds;
    const auto add = [&kinds](Kind k) {
        if (std::find(kinds.begin(), kinds.end(), k) == kinds.end()) kinds.push_back(k);
    };
    for (const auto& c : categories) {
        if (c == "Sections") { add(Kind::ExecOrderFolder); add(Kind::TaskFolder); }
        else if (c.rfind("Unit", 0) == 0) add(Kind::UnitsFolder);
        else if (c == "DFB") add(Kind::DfbFolder);
        else if (c == "DDT") add(Kind::TypesFolder);
        else if (c == "Variables globales") add(Kind::VariablesFolder);
        else if (c == "Configuration") add(Kind::ConfigurationFolder);
        else if (c == "Tables d'animation") add(Kind::TablesFolder);
        else if (c == "Variables IHM") { add(Kind::HmiScripts); add(Kind::HmiVariablesFolder); }
        else if (c == "Types IHM") { add(Kind::HmiScripts); add(Kind::HmiTypesFolder); }
        else if (c == "Scripts") { add(Kind::HmiScripts); add(Kind::HmiScriptsFolder); }
        else if (c == "Ressources") add(Kind::HmiResources);
        else if (c == "Utilisateurs") { add(Kind::HmiConfig); add(Kind::HmiUsers); }
        else if (c == "Alarmes") { add(Kind::HmiConfig); add(Kind::HmiAlarms); }
        else if (c == "Recettes") { add(Kind::HmiConfig); add(Kind::HmiRecipes); }
        else if (c == "Styles") add(Kind::HmiStyles);
        else if (c == "Symboles") add(Kind::HmiSymbolsFolder);
        else if (c.find("ues") != std::string::npos || c == "Popups") add(Kind::HmiViews);   // Vues, Modeles de vues
    }
    treeModel_->setChangedKinds(std::move(kinds), since > 0 ? "modifi\xC3\xA9 depuis V" + std::to_string(since)
                                                            : std::string("modifi\xC3\xA9 depuis la derni\xC3\xA8re version"));
    if (explorer_) explorer_->invalidate();
    refreshTreeChrome();
}

void MainAnalysisScreen::setTreeLibraryUpdates(std::size_t count) {
    if (treeModel_ && treeModel_->setLibraryUpdates(count, 0) && explorer_) explorer_->invalidate();
    refreshTreeChrome();
}

std::string MainAnalysisScreen::treeHealthText() const {
    std::string out;
    if (const auto* foot = dynamic_cast<const TreeFoot*>(treeFoot_))
        for (const auto& p : foot->parts()) out += (out.empty() ? "" : " \xC2\xB7 ") + p.text;
    return out;
}

void MainAnalysisScreen::refreshTreeChrome() {
    auto* rail = dynamic_cast<TreeRail*>(treeRail_);
    auto* foot = dynamic_cast<TreeFoot*>(treeFoot_);
    if (!rail && !foot) return;
    // Un autre projet (un autre modele) repart de "tout" : le rail le suit.
    if (treeModel_) treeScope_ = kTreeScopes[std::clamp(treeModel_->scope(), 0, 5)];
    const bool hmi = treeModel_ && treeModel_->hasHmi();
    // L'etat de la simulation : la pastille du dossier Simulation (Centre de simulation).
    std::string simText;
    ui::Tone simTone = ui::Tone::None;
    if (treeModel_) {
        const auto st = treeModel_->style(ProjectTreeModel::simFolderNode());
        simText = st.badge.empty() ? st.pill : st.badge;
        simTone = st.badge.empty() ? st.pillTone : st.badgeTone;
    }
    const auto& host = app_.simulation();
    if (simText.empty()) {
        using State = SimulationHost::State;
        const auto state = host.attached() ? host.state() : State::Stopped;
        simText = state == State::Running ? "en marche" : state == State::Paused ? "en pause"
                : state == State::Halted ? "halte" : "arr\xC3\xAAt\xC3\xA9" "e";
        simTone = state == State::Running ? ui::Tone::Ok : state == State::Paused ? ui::Tone::Info
                : state == State::Halted ? ui::Tone::Error : ui::Tone::Muted;
    }
    const std::size_t errors = treeModel_ ? treeModel_->hmiExprErrors() : 0;
    const std::size_t updates = treeModel_ ? treeModel_->libraryUpdates() : 0;
    std::string versionText;
    std::size_t versions = 0;
    if (treeModel_ && !treeModel_->versions().empty()) {
        const auto& all = treeModel_->versions();
        versions = all.size() - 1;
        if (all.front().tone == ui::Tone::Warning)
            versionText = "V" + std::to_string(all.size() > 1 ? all[1].number + 1 : 1) + " modifi\xC3\xA9";
    }
    if (rail) {
        std::vector<TreeRail::Item> top;
        const auto item = [&](const char* key, const char* tip, ui::Icon icon, std::uint8_t domain, std::string badge, ui::Tone tone) {
            TreeRail::Item it;
            it.key = key;
            it.tip = tip;
            it.icon = icon;
            it.domain = domain;
            it.badge = std::move(badge);
            it.tone = tone;
            it.on = treeScope_ == key;
            top.push_back(std::move(it));
        };
        item("tout", "Tout le projet", ui::Icon::Project, 0, {}, ui::Tone::None);
        if (hmi) {
            item("epingles", "\xC3\x89pingl\xC3\xA9s et r\xC3\xA9" "cents", ui::Icon::StarFilled, 0,
                 treePins_.empty() ? std::string{} : std::to_string(treePins_.size()), ui::Tone::None);
            item("api", "API seulement", ui::Icon::Cpu, 1, updates ? std::to_string(updates) : std::string{}, ui::Tone::Warning);
            item("ihm", "IHM seulement", ui::Icon::Screen, 2, errors ? std::to_string(errors) : std::string{}, ui::Tone::Error);
            item("simulation", "Simulation seulement", ui::Icon::Play, 3, ".", simTone);
            item("versions", "Versions seulement", ui::Icon::History, 4, versions ? std::to_string(versions) : std::string{}, ui::Tone::None);
        }
        const float h = explorer_ ? explorer_->rowHeightOverride() : 0.f;
        std::vector<TreeRail::Item> bottom;
        TreeRail::Item follow;
        follow.key = "suivre";
        follow.tip = treeFollow_ ? "Suivre l'onglet actif : en marche (l'arbre montre ce que tu regardes). Un clic l'arr\xC3\xAAte."
                                 : "Suivre l'onglet actif : arr\xC3\xAAt\xC3\xA9. Un clic le remet.";
        follow.icon = ui::Icon::Open;
        follow.on = treeFollow_;
        bottom.push_back(follow);
        TreeRail::Item collapse;
        collapse.key = "replier";
        collapse.tip = "Tout replier";
        collapse.icon = ui::Icon::Collapse;
        bottom.push_back(collapse);
        TreeRail::Item density;
        density.key = "densite";
        density.tip = std::string("Densit\xC3\xA9 : ") + (h == 22.f ? "serr\xC3\xA9" "e" : h == 26.f ? "large" : "normale") + ". Un clic la change.";
        density.icon = ui::Icon::Layers;
        bottom.push_back(density);
        // 1.11 (chantier T3, C4) : le « ? » - la legende des icones compilable / generable.
        TreeRail::Item legend;
        legend.key = "legende";
        legend.tip = "Les ic\xC3\xB4nes \xC3\xA0 droite des sections, des DFB et des scripts :";
        // Tranche 11 (decision du chef) : l'infobulle trace les symboles comme l'arbre, au trait et dans
        // la couleur de leur ton (WidgetHost::paintTooltip, ui::trailsym), sans dependre de la police ;
        // le « ⇩ barre » est barre a l'ecran (U+0336 apres le glyphe, kStruck).
        for (const auto& row : hmi::build::legend())
            legend.tip += "\n" + std::string(row.glyph) + (row.struck ? std::string(ui::trailsym::kStruck) : std::string())
                         + "  " + std::string(row.label) + " : " + std::string(row.meaning);
        // Tranche 10 : une unite de programme, une tache et le dossier Scripts resument les leurs.
        legend.tip += "\nSur une unit\xC3\xA9 de programme, une t\xC3\xA2" "che ou le dossier Scripts : le r\xC3\xA9sum\xC3\xA9"
                      " (\xE2\x9C\x93 si tout compile, sinon le nombre de ce qui ne compile pas).";
        legend.icon = ui::Icon::Info;
        bottom.push_back(legend);
        rail->setItems(std::move(top), std::move(bottom));
    }
    if (foot) {
        std::vector<TreeFoot::Part> parts;
        parts.push_back({"Simulation " + simText, "simulation", simTone, true});
        if (errors > 0)
            parts.push_back({std::to_string(errors) + (errors > 1 ? " expressions impossibles" : " expression impossible"), "erreurs", ui::Tone::Error, false});
        if (updates > 0)
            parts.push_back({std::to_string(updates) + " \xC3\xA0 mettre \xC3\xA0 jour", "bibliotheque", ui::Tone::Warning, false});
        if (!versionText.empty()) parts.push_back({versionText, "versions", ui::Tone::Warning, false});
        foot->setParts(std::move(parts));
    }
}

void MainAnalysisScreen::openTreeHealth(const std::string& part) {
    if (part == "simulation") openApiTabFromAction("sim:ensemble");
    else if (part == "erreurs") (void)openTreeTool(true, "Compiler");
    else if (part == "bibliotheque") {
        // Types derives s'ils ont des mises a jour (leur pastille orange), sinon Blocs DFB.
        if (treeModel_) {
            const auto types = ProjectTreeModel::pack(Kind::TypesFolder, 0);
            const auto st = treeModel_->style(types);
            onTreeSelection(!st.pill.empty() && st.pillTone == ui::Tone::Warning ? types : ProjectTreeModel::pack(Kind::DfbFolder, 0));
        }
    } else if (part == "versions") openVersions(-1);
}

void MainAnalysisScreen::treeHoverAction(ui::NodeId node, std::size_t action) {
    if (!explorer_ || node == ui::kInvalidNode) return;
    if (action == 0) {                                   // Epingler / Desepingler
        const auto target = treeModel_ ? treeModel_->shortcutTarget(node) : ui::kInvalidNode;
        const auto n = target != ui::kInvalidNode ? target : node;
        if (treePinnable(n)) (void)pinTreeNode(n, !isTreePinned(n));
        else if (status_) status_->setTransientMessage("Cette ligne ne s'\xC3\xA9pingle pas (un titre, un raccourci).", 4.0);
    } else if (action == 1) {                            // Ouvrir dans une fenetre detachee
        // Seulement l'onglet de CETTE ligne : celui qui vient de s'ouvrir, ou le sien.
        const auto pageNow = [this]() -> ui::Widget* {
            return centre_ && centre_->currentIndex() < centre_->tabCount() ? centre_->page(centre_->currentIndex()) : nullptr;
        };
        const ui::Widget* before = pageNow();
        onTreeSelection(node);
        ui::Widget* after = pageNow();
        if (after && (after != before || pageTreeNode(apiTabs_, hmiTabs_, app_.hmi() != nullptr, after) == node))
            (void)detachTab(centre_->currentIndex());
        else if (status_)
            status_->setTransientMessage("Rien \xC3\xA0 d\xC3\xA9tacher : cette ligne n'ouvre pas d'onglet.", 4.0);
    } else {                                             // "..." : le menu du clic droit, sous la ligne
        gfx::Rect r;
        if (explorer_->rowRect(node, r)) showExplorerMenu(node, {r.right() - 30.f, r.bottom()});
    }
}

void MainAnalysisScreen::refreshTreeState() {
    treeLibRevision_ = ~0ull;
    treeChromeAt_ = -1.0e9;
    tickTree(treeNow_);
}

void MainAnalysisScreen::openTreeHealthPart(const std::string& part) { openTreeHealth(part); }

std::string MainAnalysisScreen::treeCurrentCard() const {
    return explorer_ ? treeCardText(explorer_->currentNode()) : std::string{};
}

bool MainAnalysisScreen::treeActionOnCurrent(std::size_t action) {
    if (!explorer_ || explorer_->currentNode() == ui::kInvalidNode || action > 2) return false;
    if (action == 2) {                                   // sans souris, la ligne peut etre hors de vue
        explorer_->ensureVisible(explorer_->currentNode());
        gfx::Rect r;
        if (!explorer_->rowRect(explorer_->currentNode(), r)) return false;
    }
    treeHoverAction(explorer_->currentNode(), action);
    return true;
}

std::string MainAnalysisScreen::treeCardText(ui::NodeId node) const {
    if (!treeModel_ || !explorer_ || node == ui::kInvalidNode) return {};
    const auto kind = ProjectTreeModel::kindOf(node);
    if (kind == Kind::ToolRow || kind == Kind::Root) return {};
    // Le chemin : les ancetres montres (sans la racine, le nom du projet).
    std::vector<std::string> path;
    for (const auto a : explorer_->visibleAncestors(node))
        if (a != treeModel_->root()) path.push_back(treeModel_->text(a));
    std::string card = treeModel_->text(node);
    const auto st = treeModel_->style(node);
    const auto count = treeModel_->counterOf(node);
    if (!count.empty()) card += " : " + count;
    if (!path.empty()) {
        std::string where;
        for (const auto& p : path) where += (where.empty() ? "" : " \xE2\x80\xBA ") + p;
        card += "\n" + where;
    }
    if (!st.hint.empty()) card += "\n" + st.hint;
    if (const auto bad = kind == Kind::HmiViews ? treeModel_->hmiExprErrorsInViews()
                         : kind == Kind::HmiFolder ? treeModel_->hmiExprErrors() : 0; bad > 0)
        card += "\n" + std::to_string(bad) + (bad > 1 ? " expressions impossibles" : " expression impossible") + " (le dernier Compiler)";
    if (kind == Kind::TypesFolder || kind == Kind::DfbFolder || kind == Kind::ApiFolder)
        if (!st.pill.empty() && st.pillTone == ui::Tone::Warning)
            card += "\n" + st.pill + (kind == Kind::ApiFolder ? "" : " avec une version plus r\xC3\xA9" "cente en biblioth\xC3\xA8que");
    if (st.dotTone != ui::Tone::None) card += "\n" + st.dotTip;
    card += "\nClic : ouvrir \xC2\xB7 clic droit : \xC3\xA9pingler, renommer\xE2\x80\xA6 \xC2\xB7 F2 : renommer";
    return card;
}

} // namespace app
