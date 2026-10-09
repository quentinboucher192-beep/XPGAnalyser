#include "HmiEditor.hpp"
#include "../RenameDialog.hpp"            // lot 7 : requestRename (en ligne : rien de plus a lier)
#include "../../hmi/HmiSymbols.hpp"
#include "../../hmi/HmiRenameRefs.hpp"     // Lot API 8 : renommer partout (IHM)
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "HmiAssist.hpp"
#include "../../hmi/HmiApiVars.hpp"     // 1.11.1 (API-V, R1111-6) : l'arbre de la bibliotheque, sur le modele d'API-M
#include "HmiParamPanes.hpp"             // 1.9 : les parametres des popups
#include "HmiObjectAlarmPanes.hpp"    // 1.9 : les alarmes de l'objet (inspecteur)
#include "HmiOperatorPanes.hpp"       // 1.10 (S2) : les operateurs du symbole
#include "HmiFunctionPanes.hpp"       // 1.11.10 : les fonctions du symbole
#include "HmiSymbolPopupsPane.hpp"    // 1.11.10 : les popups du symbole
#include "HmiActionDialogs.hpp"       // 1.11.10 : la fenetre du script (une redefinition)
#include "HmiActionsPanel.hpp"
#include "HmiPaneKit.hpp"
#include "HmiTreeData.hpp"            // 1.10.3 (Q1103) : les lignes d'un objet deplie (les deux explorateurs)
#include "HmiValueKind.hpp"           // 1.11.3 : le carre de legende, la liste des carres

#include <set>

#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiDuplicate.hpp"    // 1.10.2 (chantier D) : les reperes dans l'inspecteur
#include "../../ui/Layout.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/ExprField.hpp"   // 1.10 (chantier K) : "Revenir a la valeur par defaut"
#include "../../ui/widgets/ExprMarkers.hpp" // 1.10.3 : les reperes d'une case se voient
#include "../../ui/widgets/FilterMemory.hpp" // 1.10.3 : l'etat des filtres de l'inspecteur, retenu

#include <array>
#include <map>

#include <algorithm>

namespace app {

std::string hmiVariableLabel(hmi::Kind);   // 1.10.4 (K3) : HmiPanels.cpp ("Variable ecrite"...)

using hmi::Id;
using hmi::kNoId;

namespace {

using Panel = HmiTitledPanel;

// 1.10 (chantier O) : la section "Alarmes de l'objet" de l'inspecteur ouverte
// sur une alarme (son nom de sous-categorie : "chemin.nom" ou "nom"), les autres
// repliees, et la grille qui defile jusqu'a elle. Faux : pas de telle alarme.
bool focusObjectAlarm(ui::PropertyGrid& grid, const std::string& name) {
    auto cats = grid.categories();
    bool found = false;
    for (auto& c : cats) {
        if (c.name != "Alarmes de l'objet") continue;
        c.expanded = true;
        for (auto& sub : c.children) {
            sub.expanded = sub.name == name;
            found = found || sub.expanded;
        }
    }
    if (!found) return false;
    grid.setCategories({});                 // une autre forme : les replis donnes ici comptent
    grid.setCategories(std::move(cats));
    (void)grid.revealValue("\xC3\x89tat");
    return true;
}

// 1.10.3 : le lecteur de reperes de la grille (ui/ ne connait pas le modele) :
// celui de Dupliquer, le meme partout.
std::vector<ui::exprfield::MarkerSpan> gridMarkers(std::string_view text, bool expression) {
    std::vector<ui::exprfield::MarkerSpan> out;
    for (auto& m : hmi::dup::markersIn(text, expression)) out.push_back({m.at, m.length, std::move(m.name)});
    return out;
}

constexpr const char* kFilterKey = "hmi.inspecteur.filtres";
constexpr std::array<const char*, 3> kFilterWords{"fx", "reperes", "nonvides"};

} // namespace

// ============================================ 1.10.3 : les filtres de l'inspecteur ===
// L'onglet Proprietes : une bande de trois boutons bascule (fx, Reperes, Non vides),
// chacun avec son nombre, puis la grille. Un clic allume ou eteint ; l'editeur
// refait la grille (changed).
class HmiPropsFilterBar final : public ui::Widget {
public:
    HmiPropsFilterBar(std::string id, std::unique_ptr<ui::PropertyGrid> grid) : ui::Widget(std::move(id)) {
        addChild(std::move(grid));
        load();
    }
    std::array<bool, 3>        on{};
    std::array<std::size_t, 3> counts{};
    std::function<void()>      changed;
    [[nodiscard]] gfx::Rect chip(int i) const { return chips_[static_cast<std::size_t>(std::clamp(i, 0, 2))]; }
    [[nodiscard]] bool any() const noexcept { return on[0] || on[1] || on[2]; }
    void set(int i, bool v) {
        auto& slot = on[static_cast<std::size_t>(std::clamp(i, 0, 2))];
        if (slot == v) return;
        slot = v;
        save();
        if (changed) changed();
        invalidate();
    }
    // Relire l'etat retenu (un autre editeur a pu le changer) ; sans reglages : rien.
    void load() {
        if (!ui::FilterMemory::installed()) return;
        const std::string stored = ui::FilterMemory::read(kFilterKey);
        for (std::size_t i = 0; i < 3; ++i) on[i] = stored.find(kFilterWords[i]) != std::string::npos;
    }
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override {
        static const char* const what[3] = {
            "fx : les cases pilot\xC3\xA9" "es par une expression",
            "Rep\xC3\xA8res : les cases qui contiennent un rep\xC3\xA8re $Nom$ (Dupliquer\xE2\x80\xA6, Ctrl+D, les remplace)",
            "Non vides : les cases renseign\xC3\xA9" "es - un texte ou une expression non vide, ou une valeur diff\xC3\xA9rente de "
            "celle d'un objet neuf"};
        for (std::size_t i = 0; i < 3; ++i)
            if (chips_[i].contains(mouse))
                return std::string(what[i]) + " (" + std::to_string(counts[i]) + ").\nClic : " + (on[i] ? "\xC3\xA9teindre" : "allumer")
                     + ". Les filtres se cumulent : une case s'affiche si elle r\xC3\xA9pond \xC3\xA0 au moins un filtre allum\xC3\xA9 ; "
                       "tous \xC3\xA9teints, tout s'affiche.";
        return Widget::liveTooltip(mouse);
    }
    [[nodiscard]] bool hasTooltip() const override { return true; }
protected:
    void onLayout() override {
        const auto b = bounds();
        if (!children().empty()) children().front()->setBounds({b.x, b.y + kBarH, b.w, std::max(0.f, b.h - kBarH)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        const gfx::Rect bar{b.x, b.y, b.w, kBarH};
        ctx.r.fillRect(bar, c.panelBg);
        ctx.r.line({bar.x, bar.bottom() - 1.f}, {bar.right(), bar.bottom() - 1.f}, c.border, 1.f);
        const auto font = ctx.theme.font.smallUi;
        const float lh = ctx.r.lineHeight(font);
        static const char* const labels[3] = {"fx", "$ Rep\xC3\xA8res", "Non vides"};
        float x = bar.x + 6.f;
        const std::string lead = any() ? "Filtr\xC3\xA9 :" : "Montrer :";
        ctx.r.drawText({x, bar.y + (kBarH - lh) * 0.5f}, lead, font, c.textMuted);
        // la meme place pour "Montrer :" et "Filtre :" : les boutons ne bougent pas sous la souris
        x += std::max(ctx.r.measure("Montrer :", font).width, ctx.r.measure("Filtr\xC3\xA9 :", font).width) + 8.f;
        for (std::size_t i = 0; i < 3; ++i) {
            const std::string label = labels[i];
            const std::string count = std::to_string(counts[i]);
            const float lw = ctx.r.measure(label, font).width, cw = ctx.r.measure(count, font).width;
            const gfx::Rect r{x, bar.y + 5.f, lw + cw + 26.f, kBarH - 10.f};
            chips_[i] = r;
            const gfx::Color tone = i == 1 ? c.warning : c.accent;
            if (on[i]) {
                ctx.r.fillRoundedRect(r, tone, r.h * 0.5f);
            } else {
                ctx.r.fillRoundedRect(r, c.border, r.h * 0.5f);
                ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, c.panelBg, r.h * 0.5f - 1.f);
            }
            const gfx::Color ink = on[i] ? (i == 1 ? gfx::Color{40, 28, 0, 255} : c.selectionText) : (counts[i] ? c.text : c.textDisabled);
            const float ty = r.y + (r.h - lh) * 0.5f;
            ctx.r.drawText({r.x + 10.f, ty}, label, font, ink);
            ctx.r.drawText({r.x + 16.f + lw, ty}, count, font, on[i] ? ink : c.textMuted);
            x = r.right() + 6.f;
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left)
            for (std::size_t i = 0; i < 3; ++i)
                if (chips_[i].contains(d->pos)) {
                    set(static_cast<int>(i), !on[i]);
                    return ui::EventResult::Consumed;
                }
        return ui::EventResult::Ignored;
    }
private:
    void save() const {
        std::string v;
        for (std::size_t i = 0; i < 3; ++i)
            if (on[i]) v += (v.empty() ? "" : ",") + std::string(kFilterWords[i]);
        if (ui::FilterMemory::installed()) ui::FilterMemory::write(kFilterKey, v);
    }
    static constexpr float kBarH = 32.f;
    mutable std::array<gfx::Rect, 3> chips_{};
};

void HmiEditor::setPropertyFilter(PropFilter f, bool on) {
    if (propFilters_) propFilters_->set(static_cast<int>(f), on);
}
bool HmiEditor::propertyFilter(PropFilter f) const noexcept {
    return propFilters_ && propFilters_->on[static_cast<std::size_t>(f)];
}
std::size_t HmiEditor::propertyFilterCount(PropFilter f) const noexcept {
    return propFilters_ ? propFilters_->counts[static_cast<std::size_t>(f)] : 0u;
}
gfx::Rect HmiEditor::propertyFilterRect(PropFilter f) const {
    return propFilters_ ? propFilters_->chip(static_cast<int>(f)) : gfx::Rect{};
}

HmiEditor::HmiEditor(std::string widgetId, hmi::DocumentPtr doc, Id view, Apply apply,
                     std::shared_ptr<const domain::Project> plc)
    : ui::Widget(std::move(widgetId)), doc_(std::move(doc)), viewId_(view), apply_(std::move(apply)), plc_(std::move(plc)) {
    using ui::Orientation;
    const std::string base = this->id();
    if (plc_) hmiPublishPlcNames(plc_.get());   // 1.11.3 : un argument qui n'est pas une variable est une constante
    // Une modification qui passe : l'avertissement d'une saisie refusee avant
    // elle n'a plus lieu d'etre (les avertissements suivent toujours un echec).
    apply_ = [inner = std::move(apply_), this](core::CommandPtr c) {
        if (statusBar_) statusBar_->dismissTransient();
        inner(std::move(c));
    };

    // ---- la barre d'outils ---------------------------------------------------
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ActUndo, HmiGlyph::Undo, "Annuler (Ctrl+Z) : la m\xC3\xAAme pile que le reste du projet");
    tools->add(ActRedo, HmiGlyph::Redo, "R\xC3\xA9tablir (Ctrl+Y)");
    tools->separator();
    tools->add(ActCopy, HmiGlyph::Copy, "Copier (Ctrl+C)");
    tools->add(ActPaste, HmiGlyph::Paste, "Coller (Ctrl+V) : les animations et les liaisons API suivent");
    tools->add(ActDuplicate, HmiGlyph::Duplicate, "Dupliquer\xE2\x80\xA6 (Ctrl+D : rep\xC3\xA8res, indices, pose) ; tel quel : Ctrl+Maj+D ou Alt + glisser");
    tools->add(ActDelete, HmiGlyph::Delete, "Supprimer (Suppr)");
    tools->separator();
    tools->add(ActGroup, HmiGlyph::Group, "Grouper");
    tools->add(ActUngroup, HmiGlyph::Ungroup, "D\xC3\xA9grouper");
    tools->add(ActMakeSymbol, HmiGlyph::Symbol,
               "Cr\xC3\xA9" "er un symbole : la s\xC3\xA9lection devient un symbole r\xC3\xA9utilisable et param\xC3\xA9tr\xC3\xA9 "
               "(IHM > Symboles) ; une instance la remplace");
    tools->separator();
    tools->add(ActFront, HmiGlyph::Front, "Premier plan");
    tools->add(ActForward, HmiGlyph::Forward, "Avancer d'un rang");
    tools->add(ActBackward, HmiGlyph::Backward, "Reculer d'un rang");
    tools->add(ActBack, HmiGlyph::Back, "Arri\xC3\xA8re-plan");
    tools->separator();
    tools->add(ActAlignLeft, HmiGlyph::AlignLeft, "Aligner \xC3\xA0 gauche (un objet seul : sur la vue)");
    tools->add(ActAlignCenterH, HmiGlyph::AlignCenterH, "Centrer horizontalement");
    tools->add(ActAlignRight, HmiGlyph::AlignRight, "Aligner \xC3\xA0 droite");
    tools->add(ActAlignTop, HmiGlyph::AlignTop, "Aligner en haut");
    tools->add(ActAlignCenterV, HmiGlyph::AlignCenterV, "Centrer verticalement");
    tools->add(ActAlignBottom, HmiGlyph::AlignBottom, "Aligner en bas");
    tools->add(ActDistributeH, HmiGlyph::DistributeH, "Distribuer horizontalement (3 objets ou plus)");
    tools->add(ActDistributeV, HmiGlyph::DistributeV, "Distribuer verticalement");
    tools->separator();
    tools->add(ActRotateLeft, HmiGlyph::RotateLeft, "Tourner de -90\xC2\xB0");
    tools->add(ActRotateRight, HmiGlyph::RotateRight, "Tourner de +90\xC2\xB0");
    tools->add(ActRotate45, HmiGlyph::RotateRight, "Tourner de +45\xC2\xB0", "45\xC2\xB0");
    tools->add(ActRotate180, HmiGlyph::RotateRight, "Tourner de 180\xC2\xB0", "180\xC2\xB0");
    tools->add(ActMirrorH, HmiGlyph::MirrorH, "Miroir horizontal");
    tools->add(ActMirrorV, HmiGlyph::MirrorV, "Miroir vertical");
    tools->separator();
    tools->add(ActLock, HmiGlyph::Lock, "Verrouiller");
    tools->add(ActUnlock, HmiGlyph::Unlock, "D\xC3\xA9verrouiller");
    tools->add(ActStyleCopy, HmiGlyph::StyleCopy, "Copier le style (Ctrl+Maj+C)");
    tools->add(ActStylePaste, HmiGlyph::StylePaste, "Coller le style (Ctrl+Maj+V)");
    tools->add(ActMakeStyle, HmiGlyph::Style,
               "Cr\xC3\xA9" "er un style nomm\xC3\xA9 : l'apparence de l'objet choisi (couleurs, police, taille, rayon) sous un nom "
               "(IHM > Styles) ; la s\xC3\xA9lection le cite, et suit ses changements");
    // Lot 20 : la vue entiere, en modele (ma bibliotheque ou ce projet).
    tools->add(ActSaveTemplate, HmiGlyph::StarFilled,
               "Enregistrer la vue comme mod\xC3\xA8le : elle et ce dont elle a besoin (symboles, images, styles, variables), "
               "pour la refaire d'un clic (Nouvelle vue)", "Mod\xC3\xA8le");
    tools->separator();
    tools->add(ActGrid, HmiGlyph::Grid, "Afficher la grille");
    tools->add(ActSnap, HmiGlyph::Magnet, "Magn\xC3\xA9tisme (grille, objets, guides)");
    tools->add(ActGuideV, HmiGlyph::Guide, "Ajouter un guide vertical (le tirer hors de la vue le retire)", "V");
    tools->add(ActGuideH, HmiGlyph::Guide, "Ajouter un guide horizontal", "H");
    tools->add(ActShowHidden, HmiGlyph::Eye, "Montrer les objets cach\xC3\xA9s (estomp\xC3\xA9s)");
    tools->separator();
    tools->add(ActZoomOut, HmiGlyph::ZoomOut, "Zoom arri\xC3\xA8re (Ctrl + molette)");
    tools->add(ActZoomIn, HmiGlyph::ZoomIn, "Zoom avant");
    tools->add(ActZoomFit, HmiGlyph::ZoomFit, "Toute la vue");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));

    // ---- le centre, et ses deux colonnes -------------------------------------
    auto split = std::make_unique<ui::Splitter>(Orientation::Horizontal, base + ".split");

    auto left = std::make_unique<ui::Splitter>(Orientation::Vertical, base + ".left");
    {
        auto panel = std::make_unique<Panel>(base + ".objectsPanel", "EXPLORATEUR D'OBJETS");
        auto row = std::make_unique<ui::BoxLayout>(Orientation::Horizontal, base + ".objectsFilter");
        row->setSpacing(4.f);
        auto search = std::make_unique<ui::InputText>(base + ".objectsSearch");
        search->setPlaceholder("Rechercher un objet, une variable...");
        search_ = &static_cast<ui::InputText&>(row->addChild(std::move(search)));
        auto type = std::make_unique<ui::DropDown>(base + ".objectsType");
        std::vector<ui::DropDown::Item> items{{"Tous les types", ""}};
        for (auto k : hmi::kPlaceableKinds) items.push_back({std::string(hmi::kindLabel(k)), std::string(hmi::kindKey(k))});
        items.push_back({std::string(hmi::kindLabel(hmi::Kind::Group)), std::string(hmi::kindKey(hmi::Kind::Group))});
        // Lot 10 : les instances de symboles (elles ne sont pas dans la palette des genres).
        items.push_back({std::string(hmi::kindLabel(hmi::Kind::SymbolInstance)), std::string(hmi::kindKey(hmi::Kind::SymbolInstance))});
        type->setItems(std::move(items));
        type->setSelectedIndex(0);
        typeFilter_ = &static_cast<ui::DropDown&>(row->addChild(std::move(type)));
        panel->addRow(std::move(row), 32.f);
        objects_ = &static_cast<HmiObjectList&>(panel->setBody(std::make_unique<HmiObjectList>(base + ".objects")));
        left->addPane(std::move(panel), 0.66f, 120.f);
    }
    {
        auto panel = std::make_unique<Panel>(base + ".layersPanel", "CALQUES");
        auto lt = std::make_unique<HmiToolStrip>(base + ".layerTools");
        lt->add(ActLayerAdd, HmiGlyph::LayerAdd, "Nouveau calque (devient le calque actif)");
        lt->add(ActLayerRemove, HmiGlyph::LayerRemove, "Supprimer le calque actif : ses objets passent au calque du dessous");
        lt->add(ActLayerUp, HmiGlyph::Up, "Monter le calque");
        lt->add(ActLayerDown, HmiGlyph::Down, "Descendre le calque");
        lt->add(ActLayerSelect, HmiGlyph::Select, "S\xC3\xA9lectionner tous les objets du calque actif");
        lt->add(ActLayerMoveHere, HmiGlyph::Layer, "Mettre la s\xC3\xA9lection dans le calque actif");
        layerTools_ = &static_cast<HmiToolStrip&>(panel->addRow(std::move(lt), 36.f));
        layers_ = &static_cast<HmiLayerList&>(panel->setBody(std::make_unique<HmiLayerList>(base + ".layers")));
        left->addPane(std::move(panel), 0.34f, 90.f);
    }
    split->addPane(std::move(left), 0.19f, 180.f);

    auto canvas = std::make_unique<HmiCanvas>(base + ".canvas", doc_, viewId_,
                                              [this](core::CommandPtr c) { apply_(std::move(c)); });
    canvas_ = &static_cast<HmiCanvas&>(split->addPane(std::move(canvas), 0.55f, 240.f));

    auto right = std::make_unique<ui::Splitter>(Orientation::Vertical, base + ".right");
    {
        // L'inspecteur, comme WinCC Unified : Proprietes | Actions (evenements).
        auto tabs = std::make_unique<ui::TabControl>(base + ".inspector");
        auto grid = std::make_unique<ui::PropertyGrid>(base + ".props");
        grid->setShowDescriptionPane(true);
        grid->setFieldAssist(assist::gridAssist(assist::sourcesFor(doc_)));
        grid->setPaletteColors([doc = doc_] { return doc ? assist::projectColors(doc->project) : std::vector<std::string>{}; });
        grid->setNameColumnRatio(0.52f);   // "Visibilite (expression)" se lit en entier
        props_ = grid.get();
        // 1.10.3 : les reperes ($Vanne$) des cases se voient (le lecteur de Dupliquer) ;
        // les trois filtres en haut de l'onglet.
        ui::exprfield::setMarkerFinder(&gridMarkers);
        auto filters = std::make_unique<HmiPropsFilterBar>(base + ".propsFilters", std::move(grid));
        propFilters_ = filters.get();
        propFilters_->changed = [this] { rebuildProperties(); };
        tabs->addTab(ui::TabControl::Tab{"Propri\xC3\xA9t\xC3\xA9s", ui::Icon::Settings, false, false}, std::move(filters));
        auto acts = std::make_unique<HmiActionsPanel>(base + ".actions", doc_, viewId_,
                                                      [this](core::CommandPtr c) { apply_(std::move(c)); });
        actions_ = acts.get();
        tabs->addTab(ui::TabControl::Tab{"Actions", ui::Icon::Play, false, false}, std::move(acts));
        auto content = std::make_unique<HmiContentPanel>(base + ".content", doc_, viewId_,
                                                         [this](core::CommandPtr c) { apply_(std::move(c)); });
        content_ = content.get();
        tabs->addTab(ui::TabControl::Tab{"Contenu", ui::Icon::AnimationTable, false, false}, std::move(content));
        tabs->setCurrentIndex(0);
        inspector_ = &static_cast<ui::TabControl&>(right->addPane(std::move(tabs), 0.58f, 140.f));
    }
    {
        auto panel = std::make_unique<Panel>(base + ".libraryPanel", "BIBLIOTH\xC3\x88QUE");
        auto search = std::make_unique<ui::InputText>(base + ".librarySearch");
        search->setPlaceholder("Rechercher un composant...");
        paletteSearch_ = &static_cast<ui::InputText&>(panel->addRow(std::move(search), 32.f));
        palette_ = &static_cast<HmiPalette&>(panel->setBody(std::make_unique<HmiPalette>(base + ".palette")));
        right->addPane(std::move(panel), 0.42f, 120.f);
    }
    split->addPane(std::move(right), 0.26f, 200.f);
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));

    statusBar_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    // ---- les liens -------------------------------------------------------------
    auto hasSel = [this] { return !canvas_->selection().empty(); };
    for (int a : {ActCopy, ActDuplicate, ActDelete, ActFront, ActBack, ActForward, ActBackward, ActAlignLeft,
                  ActAlignCenterH, ActAlignRight, ActAlignTop, ActAlignCenterV, ActAlignBottom, ActRotateLeft,
                  ActRotateRight, ActRotate45, ActRotate180, ActMirrorH, ActMirrorV, ActLock, ActUnlock, ActStyleCopy})
        tools_->setEnabledWhen(a, hasSel);
    tools_->setEnabledWhen(ActGroup, [this] { return canvas_->selection().size() >= 2; });
    tools_->setEnabledWhen(ActMakeSymbol, hasSel);
    tools_->setEnabledWhen(ActMakeStyle, hasSel);
    tools_->setEnabledWhen(ActDistributeH, [this] { return canvas_->selection().size() >= 3; });
    tools_->setEnabledWhen(ActDistributeV, [this] { return canvas_->selection().size() >= 3; });
    tools_->setEnabledWhen(ActUngroup, [this] {
        const auto* v = canvas_->view();
        if (!v) return false;
        for (Id id : canvas_->selection())
            if (const auto* o = v->object(id); o && o->kind == hmi::Kind::Group) return true;
        return false;
    });
    tools_->setEnabledWhen(ActStylePaste, hasSel);
    tools_->setCheckedWhen(ActGrid, [this] { const auto* v = canvas_->view(); return v && v->grid.visible; });
    tools_->setCheckedWhen(ActSnap, [this] {
        const auto* v = canvas_->view();
        return v && (v->grid.snapGrid || v->grid.snapObjects || v->grid.snapGuides);
    });
    tools_->setCheckedWhen(ActShowHidden, [this] { return canvas_->showHidden(); });
    tools_->setEnabledWhen(ActUndo, [this] { return history_ && (!historyPossible_ || historyPossible_(false)); });
    tools_->setEnabledWhen(ActRedo, [this] { return history_ && (!historyPossible_ || historyPossible_(true)); });
    links_ += canvas_->historyRequested->connect([this](bool redo) { run(redo ? ActRedo : ActUndo); });
    // 1.10.2 (chantier D) : Ctrl+D ouvre "Dupliquer..." (Ctrl+Maj+D : la copie decalee, dans le canevas).
    links_ += canvas_->duplicateRequested->connect([this] { run(ActDuplicate); });
    // 1.10.2 (chantier D) : le clic droit sur la vue - Dupliquer..., tel quel, Copier, Supprimer.
    context_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(id() + ".context")));
    links_ += context_->itemChosen->connect([this](int a) {
        if (a == 1) run(ActDuplicate);
        else if (a == 2) canvas_->duplicateSelection();
        else if (a == 3) run(ActCopy);
        else if (a == 4) run(ActDelete);
    });
    links_ += canvas_->contextRequested->connect([this](gfx::Point at) {
        const bool any = !canvas_->selection().empty();
        const std::string none = any ? std::string{} : std::string("choisis d'abord un objet");
        std::vector<ui::PopupMenu::Item> items;
        items.push_back({"Dupliquer\xE2\x80\xA6", "Ctrl+D", none, ui::Icon::None, any, false, 1});
        items.push_back({"Dupliquer tel quel", "Ctrl+Maj+D", none, ui::Icon::None, any, false, 2});
        items.push_back({{}, {}, {}, ui::Icon::None, true, true, -1});
        items.push_back({"Copier", "Ctrl+C", none, ui::Icon::None, any, false, 3});
        items.push_back({"Supprimer", "Suppr", none, ui::Icon::None, any, false, 4});
        context_->setItems(std::move(items));
        gfx::Size surface = ui::surfaceSize();
        if (surface.w <= 0.f) surface = {bounds().right() + 400.f, bounds().bottom() + 400.f};
        context_->openAt(at, surface);
    });
    links_ += tools_->triggered->connect([this](int a) { run(a); });
    links_ += layerTools_->triggered->connect([this](int a) { run(a); });
    // 1.11.3 : le carre de legende - la liste des carres, puis le selecteur (l'hote).
    legendMenu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(id() + ".carres")));
    links_ += legendMenu_->itemChosen->connect([this](int a) {
        if (!askValue_) return;
        ValueRequest req = legendRequest_;
        const auto& kinds = valuekind::kinds();
        if (a >= 300 && static_cast<std::size_t>(a - 300) < legendUnknown_.size()) {
            req.create = legendUnknown_[static_cast<std::size_t>(a - 300)];
        } else if (a >= 100 && static_cast<std::size_t>(a - 100) < kinds.size()) {
            const auto s = kinds[static_cast<std::size_t>(a - 100)].style;
            using S = ui::PropertyGrid::LegendStyle;
            req.source = (s == S::Formula || s == S::Error) ? S::Empty : s;
        } else if (a != 200) {
            return;
        }
        const auto ask = askValue_;   // l'hote peut refaire l'editeur
        ask(req);
    });
    links_ += props_->legendClicked->connect([this](const std::string& cat, const std::string& name, gfx::Rect at) {
        (void)openLegendMenu(cat, name, {at.x, at.bottom() + 4.f});
    });

    links_ += canvas_->selectionChanged->connect([this] {
        if (syncing_) return;
        // On est passe a autre chose : l'avertissement d'avant s'en va.
        statusBar_->dismissTransient();
        syncing_ = true;
        objects_->setSelection(canvas_->selection());
        objects_->revealSelection();
        syncing_ = false;
        invalidateLayout();   // les proprietes suivent, a la prochaine mise en page
    });
    links_ += objects_->selectionChanged->connect([this](const std::vector<Id>& ids) {
        if (syncing_) return;
        statusBar_->dismissTransient();
        syncing_ = true;
        canvas_->setSelection(ids, false);
        syncing_ = false;
        invalidateLayout();
    });
    links_ += objects_->toggleHidden->connect([this](Id id) {
        const auto* v = canvas_->view();
        const auto* o = v ? v->object(id) : nullptr;
        if (!o) return;
        const bool on = !o->hidden;
        canvas_->edit(on ? "Cacher" : "Montrer", [&](hmi::Project&, hmi::View& vv) { hmi::edit::setHidden(vv, {id}, on); });
    });
    links_ += objects_->toggleLocked->connect([this](Id id) {
        const auto* v = canvas_->view();
        const auto* o = v ? v->object(id) : nullptr;
        if (!o) return;
        const bool on = !o->locked;
        canvas_->edit(on ? "Verrouiller" : "D\xC3\xA9verrouiller",
                      [&](hmi::Project&, hmi::View& vv) { hmi::edit::setLocked(vv, {id}, on); });
    });
    links_ += objects_->activated->connect([this](Id id) { objects_->beginRename(id); });
    // 1.10 (chantier O) : une alarme sous un objet (l'explorateur) - l'objet est
    // deja choisi ; l'inspecteur s'ouvre sur elle (section Alarmes de l'objet).
    links_ += objects_->alarmActivated->connect([this](Id, const std::string& alarm, const std::string& path) {
        layout();                                                    // les proprietes de l'objet choisi
        const std::string name = path.empty() ? alarm : path + "." + alarm;
        if (focusObjectAlarm(*props_, name)) statusBar_->setMessage("Alarme " + name + " : la section Alarmes de l'objet s'ouvre sur elle.");
    });
    // 1.10.3 (Q1103) : une famille ou une ligne d'un objet deplie - comme dans l'arbre.
    links_ += objects_->lineActivated->connect([this](Id object, int family, int line) {
        const auto* v = doc_->project.view(viewId_);
        const auto* o = v ? v->object(object) : nullptr;
        if (!o || family < 0 || family >= static_cast<int>(hmitree::Family::Count)) return;
        const auto f = static_cast<hmitree::Family>(family);
        hmitree::Line target;
        if (line < 0) target = hmitree::familyTarget(doc_->project, *o, f);
        else if (const auto lines = hmitree::familyLines(doc_->project, *o, f); static_cast<std::size_t>(line) < lines.size())
            target = lines[static_cast<std::size_t>(line)];
        showLine(object, target);
    });
    links_ += objects_->keyPressed->connect([this](const ui::KeyDown& k) {
        // Dans l'explorateur, les memes touches que sur la vue.
        switch (k.key) {
            case ui::Key::Delete:
            case ui::Key::Backspace: canvas_->deleteSelection(); break;
            case ui::Key::F2:
                if (!canvas_->selection().empty()) objects_->beginRename(canvas_->selection().front());
                break;
            case ui::Key::Z: if (k.mods.ctrl) run(k.mods.shift ? ActRedo : ActUndo); break;
            case ui::Key::Y: if (k.mods.ctrl) run(ActRedo); break;
            case ui::Key::C: if (k.mods.ctrl) run(ActCopy); break;
            case ui::Key::V: if (k.mods.ctrl) run(ActPaste); break;
            default: break;
        }
    });
    // Lot API 8 : renommer partout (IHM) - ce qui cite l'objet suit (renameObject).
    links_ += objects_->renamed->connect([this](Id id, const std::string& name) { (void)renameObject(id, name); });
    links_ += layers_->activate->connect([this](Id layer) {
        canvas_->edit("Calque actif", [&](hmi::Project&, hmi::View& vv) { vv.activeLayer = layer; });
    });
    links_ += layers_->toggleVisible->connect([this](Id layer) {
        canvas_->edit("Calque visible", [&](hmi::Project&, hmi::View& vv) {
            if (auto* l = vv.layer(layer)) l->visible = !l->visible;
        });
    });
    links_ += layers_->toggleLocked->connect([this](Id layer) {
        canvas_->edit("Calque verrouill\xC3\xA9", [&](hmi::Project&, hmi::View& vv) {
            if (auto* l = vv.layer(layer)) l->locked = !l->locked;
        });
    });
    links_ += layers_->rename->connect([this](Id layer) { layers_->beginRename(layer); });
    links_ += layers_->renamed->connect([this](Id layer, const std::string& name) {
        std::string why;
        bool ok = true;
        canvas_->edit("Renommer le calque", [&](hmi::Project&, hmi::View& vv) { ok = hmi::edit::renameLayer(vv, layer, name, &why); });
        if (!ok) warn(why);
    });
    links_ += palette_->chosen->connect([this](hmi::Kind k) {
        canvas_->setPlaceKind(k);
        statusBar_->setMessage("Clique dans la vue pour poser : " + std::string(hmi::kindLabel(k))
                               + " (Maj : en poser plusieurs, \xC3\x89" "chap : annuler)");
    });
    links_ += canvas_->placed->connect([this] {
        palette_->setCurrent(canvas_->placeKind());
        palette_->setCurrentSymbol(canvas_->placeSymbol());
        palette_->setCurrentVariable(canvas_->placeVariable() ? canvas_->placeVariable()->name : std::string{});
    });
    // Lot 12 : l'onglet Variables de la bibliotheque - lues a l'ouverture de
    // l'onglet (le programme a pu changer) ; une variable choisie se pose au
    // clic suivant, ou la ou on la lache.
    links_ += palette_->variablesWanted->connect([this] {
        palette_->setVariables(assist::designVariables(doc_->project, plc_.get()));
        // 1.11.1 (API-V, R1111-6) : l'arbre des variables de l'automate (API.…), comme IHM > Configuration.
        palette_->setApiModel(plc_ ? std::make_shared<const hmi::apivars::Model>(hmi::apivars::Model::build(plc_, &doc_->project))
                                   : nullptr);
    });
    // Lot 20 : une colonne de noms collee depuis Excel : quel objet pour chacun.
    canvas_->setVariableSource([this] { return assist::designVariables(doc_->project, plc_.get()); });
    links_ += palette_->chosenVariable->connect([this](const hmi::design::VarInfo& v) {
        canvas_->setPlaceVariable(v);
        const bool structure = hmi::design::shapeOf(v) == hmi::design::VarShape::Structure;
        statusBar_->setMessage(v.name + " (" + v.type + ") : clique dans la vue, ou glisse-la - "
                               + (structure ? "un bouton qui ouvre " + hmi::design::equipmentPopupName(v.type)
                                                  + (hmi::design::existingEquipmentPopup(doc_->project, v.type) ? std::string{} : std::string(" (g\xC3\xA9n\xC3\xA9r\xC3\xA9" "e)"))
                                            : std::string(hmi::kindLabel(hmi::design::kindForVariable(v))))
                               + ". \xC3\x89" "chap : annuler.");
    });
    // Lot 10 : les symboles du projet, dans la bibliotheque ; poser une instance ;
    // double-clic sur une instance : ouvrir son symbole.
    palette_->setProject(&doc_->project);
    links_ += palette_->chosenSymbol->connect([this](const std::string& name) {
        canvas_->setPlaceSymbol(name);
        // 1.10.2 (D) : un modele d'objets - ses objets copies, puis "Dupliquer...".
        if (const auto* mv = doc_->project.viewByName(name); mv && !hmi::isSymbolView(*mv) && hmi::dup::isTemplatesFolder(mv->folder)) {
            statusBar_->setMessage("Clique dans la vue (ou l\xC3\xA2" "che-le) pour poser le mod\xC3\xA8le " + name
                                   + " : ses objets y sont copi\xC3\xA9s, puis Dupliquer... remplit leurs rep\xC3\xA8res (\xC3\x89" "chap : annuler).");
            return;
        }
        statusBar_->setMessage("Clique dans la vue pour poser une instance de " + name
                               + " (Maj : en poser plusieurs, \xC3\x89" "chap : annuler). Ses arguments : propri\xC3\xA9t\xC3\xA9 Arguments.");
    });
    links_ += canvas_->symbolOpened->connect([this](const std::string& name) {
        const auto* sv = doc_->project.viewByName(name);
        if (!sv || !hmi::isSymbolView(*sv)) { warn("Symbole introuvable : " + name); return; }
        openView->emit(sv->id);
    });
    links_ += paletteSearch_->textChanged->connect([this](const std::string& t) { palette_->setFilter(t); });
    links_ += search_->textChanged->connect([this](const std::string&) { setObjectFilter(search_->text(), std::nullopt); });
    links_ += typeFilter_->selectionChanged->connect([this](int) { setObjectFilter(search_->text(), std::nullopt); });
    links_ += canvas_->status->connect([this](const std::string& s) { statusBar_->setMessage(s); });
    links_ += doc_->changed->connect([this](Id touched) {
        if (touched == viewId_ || touched == kNoId) refresh();
    });
    // La pastille de l'onglet Actions : combien en a l'objet (ou la vue).
    links_ += actions_->countChanged->connect([this](std::size_t n) {
        inspector_->setTabBadge(1, n ? std::to_string(n) : std::string{}, ui::Tone::Accent);
    });
    links_ += content_->countChanged->connect([this](std::size_t n) {
        // 1.10.4 (K3) : l'onglet peut etre cache (indexOf : -1).
        if (const int at = inspector_->indexOf(content_); at >= 0)
            inspector_->setTabBadge(static_cast<std::size_t>(at), n ? std::to_string(n) : std::string{}, ui::Tone::Accent);
    });
    refresh();
    // 1.9 : un symbole - les sous-onglets Dessin | Alarmes | Instances (HmiObjectAlarmPanes.hpp).
    if (const auto* sv = doc_->project.view(viewId_); sv && hmi::isSymbolView(*sv)) {
        symbolTabs_ = &static_cast<HmiSymbolTabs&>(addChild(std::make_unique<HmiSymbolTabs>(this->id() + ".symtabs", doc_, viewId_)));
        symbolAlarms_ = &static_cast<HmiSymbolAlarmsPane&>(
            addChild(std::make_unique<HmiSymbolAlarmsPane>(this->id() + ".symalarms", doc_, viewId_, apply_)));
        symbolAlarms_->plc = [this] { return plc_.get(); };   // decision 7 : la pastille fx juge les globales
        symbolAlarms_->setVisibility(ui::Visibility::Collapsed);
        // 1.10 (S2) : le sous-onglet Operateurs - la liste, le script, ses diagnostics.
        symbolOperators_ = &static_cast<HmiOperatorsPane&>(addChild(std::make_unique<HmiOperatorsPane>(
            this->id() + ".symops", doc_, apply_, hmi::ownerOfView(*sv))));
        symbolOperators_->setAssist([this] { return plc_; });
        symbolOperators_->setVisibility(ui::Visibility::Collapsed);
        // 1.11.10 : les sous-onglets Fonctions et Popups.
        symbolFunctions_ = &static_cast<HmiFunctionsPane&>(addChild(std::make_unique<HmiFunctionsPane>(this->id() + ".symfns", doc_, apply_)));
        symbolFunctions_->setSymbol(viewId_);
        symbolFunctions_->setAssist([this] { return plc_; });
        symbolFunctions_->setVisibility(ui::Visibility::Collapsed);
        symbolPopups_ = &static_cast<HmiSymbolPopupsPane&>(
            addChild(std::make_unique<HmiSymbolPopupsPane>(this->id() + ".sympops", doc_, viewId_, apply_)));
        symbolPopups_->openView = [this](Id v) { openView->emit(v); };
        symbolPopups_->setVisibility(ui::Visibility::Collapsed);
        links_ += symbolTabs_->changed->connect([this](int) { invalidateLayout(); invalidate(); });
    }
}

HmiEditor::~HmiEditor() = default;

// ---- 1.11.3 : le carre de legende ----
namespace {
const ui::PropertyGrid::Property* findProperty(const std::vector<ui::PropertyGrid::Category>& cats, std::string_view category,
                                               std::string_view name) {
    for (const auto& c : cats) {
        if (c.name == category)
            for (const auto& p : c.properties)
                if (p.name == name) return &p;
        if (const auto* in = findProperty(c.children, category, name)) return in;
    }
    return nullptr;
}
} // namespace

void HmiEditor::setValueAsker(std::function<void(const ValueRequest&)> ask) {
    askValue_ = std::move(ask);
    props_->setLegendClickable(static_cast<bool>(askValue_));
}

bool HmiEditor::openLegendMenu(const std::string& category, const std::string& property, gfx::Point at) {
    using S = ui::PropertyGrid::LegendStyle;
    const auto* p = findProperty(props_->categories(), category, property);
    if (!p || !p->legend || !askValue_) return false;
    const bool fx = !p->expression.empty();
    ValueRequest req;
    req.category = category;
    req.property = property;
    req.field = std::string(ui::exprfield::shownLabel(*p));
    if (const auto at2 = req.field.find(" \xC2\xB7 "); at2 != std::string::npos) req.field = req.field.substr(0, at2);   // « Name · STRING »
    if (const auto f = req.field.find("  \xC6\x92"); f != std::string::npos) req.field = req.field.substr(0, f);
    req.text = fx ? p->expression : p->value;
    req.fx = fx || p->value.empty();
    req.expected = p->legend->expected;
    legendRequest_ = req;
    const valuekind::Env env{&doc_->project, doc_->project.view(viewId_), plc_.get()};
    const auto res = valuekind::classify(env, req.text, fx, req.expected);
    legendUnknown_ = res.unknown;
    std::vector<ui::PopupMenu::Item> items;
    items.push_back({"Le carr\xC3\xA9 dit d'o\xC3\xB9 vient la valeur", {}, {}, ui::Icon::None, true, false, -1, true});
    const std::string now = valuekind::summary(res);
    items.push_back({now.size() > 90 ? now.substr(0, 87) + "\xE2\x80\xA6" : now, {}, {}, ui::Icon::None, true, false, -1, true});
    for (std::size_t i = 0; i < legendUnknown_.size() && i < 3; ++i) {
        ui::PopupMenu::Item it{"Cr\xC3\xA9" "er \xC2\xAB " + legendUnknown_[i] + " \xC2\xBB\xE2\x80\xA6", "le type et la zone (API ou IHM)", {},
                               ui::Icon::None, true, false, 300 + static_cast<int>(i)};
        it.paintIcon = [](const ui::PaintContext& ctx, gfx::Rect r) { ui::paintLegend(ctx, r, valuekind::legendOf(S::Error)); };
        items.push_back(std::move(it));
    }
    items.push_back({{}, {}, {}, ui::Icon::None, true, true, -1});
    const auto& kinds = valuekind::kinds();
    const std::string expectedText = valuekind::expectedLabel(req.expected);
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const auto& k = kinds[i];
        std::string meaning(k.meaning);
        if (k.style == S::Constant) meaning = "Une valeur fixe, convertie en " + expectedText;
        if (!res.empty && k.style == res.style) meaning = "\xE2\x9C\x93 ici \xC2\xB7 " + meaning;
        ui::PopupMenu::Item it{std::string(k.name), meaning, {}, ui::Icon::None, true, false, 100 + static_cast<int>(i)};
        const S style = k.style;
        it.paintIcon = [style](const ui::PaintContext& ctx, gfx::Rect r) { ui::paintLegend(ctx, r, valuekind::legendOf(style)); };
        items.push_back(std::move(it));
    }
    items.push_back({{}, {}, {}, ui::Icon::None, true, true, -1});
    ui::PopupMenu::Item open{"Ouvrir le s\xC3\xA9lecteur\xE2\x80\xA6", "tout ce qui convient \xC3\xA0 " + expectedText, {}, ui::Icon::None, true, false, 200};
    open.paintIcon = [](const ui::PaintContext& ctx, gfx::Rect r) {
        ui::PropertyGrid::Legend l = valuekind::legendOf(S::Formula);
        l.text = "\xE2\x80\xA6";
        ui::paintLegend(ctx, r, l);
    };
    items.push_back(std::move(open));
    legendMenu_->setItems(std::move(items));
    gfx::Size surface = ui::surfaceSize();
    if (surface.w <= 0.f) surface = {bounds().right() + 400.f, bounds().bottom() + 400.f};
    legendMenu_->openAt(at, surface);
    return true;
}

bool HmiEditor::commitValue(const std::string& category, const std::string& property, const std::string& text, bool fx) {
    const auto* p = findProperty(props_->categories(), category, property);
    if (!p || !p->commit) return false;
    const auto commit = p->commit;   // la grille est refaite pendant l'appel
    std::string sent;
    if (text.empty()) sent = fx && !p->expression.empty() ? std::string("=") : std::string(ui::exprfield::kToDefault);
    else sent = fx && ui::exprfield::accepts(*p) ? "=" + text : text;
    const bool ok = commit(sent);
    if (ok) props_->propertyChanged->emit(property, sent);
    invalidateLayout();
    return ok;
}

void HmiEditor::setPlcProject(std::shared_ptr<const domain::Project> plc) {
    plc_ = std::move(plc);
    hmiPublishPlcNames(plc_.get());   // 1.11.3 : un argument qui n'est pas une variable est une constante
    invalidateLayout();
}

void HmiEditor::setObjectFilter(std::string text, std::optional<hmi::Kind> kind) {
    if (!kind) {
        if (const auto* item = typeFilter_->selectedItem(); item && !item->value.empty()) kind = hmi::kindFromKey(item->value);
    }
    objects_->setFilter(std::move(text), kind);
    objects_->setSelection(canvas_->selection());
}

void HmiEditor::refresh() {
    const auto* v = doc_->project.view(viewId_);
    canvas_->documentChanged();
    objects_->setExpressionContext(&doc_->project, plc_.get());   // ---- Lot API 8 : les expressions impossibles (le badge fx) ----
    objects_->setView(v);
    objects_->setSelection(canvas_->selection());
    layers_->setView(v);
    if (actions_) actions_->refresh();
    if (content_) content_->refresh();
    invalidateLayout();
    invalidate();
}

void HmiEditor::syncFromCanvas() {
    objects_->setSelection(canvas_->selection());
}

// ---- 1.11.10 : les fonctions et les popups du symbole d'une instance -------------------
std::vector<ui::PropertyGrid::Category> HmiEditor::instanceFunctionCategories(const hmi::View& v, const hmi::Object& inst) {
    using PG = ui::PropertyGrid;
    std::vector<PG::Category> out;
    const hmi::View* sym = hmi::symbolOf(doc_->project, inst);
    if (!sym) return out;
    const Id instId = inst.id;
    if (!sym->functions.empty()) {
        PG::Category c;
        c.name = "Fonctions du symbole (" + std::to_string(sym->functions.size()) + ")";
        for (const auto& f : sym->functions) {
            PG::Property p;
            const bool over = hmi::functionOverride(inst, f.name) != nullptr;
            p.name = "\xC6\x92 " + hmi::functionSignature(f);
            const std::string call = v.name + "." + inst.name + "." + f.name + "()";
            if (!f.isVirtual) {
                p.type = PG::ValueType::ReadOnly;
                p.value = "du symbole (non virtuelle)";
                p.description = f.description + (f.description.empty() ? "" : "\n") + "Appel : " + call
                              + ". Non virtuelle : le corps du symbole, pour toutes les instances.";
            } else {
                p.type = PG::ValueType::Enum;
                p.value = over ? "red\xC3\xA9" "finie ici" : "du symbole";
                p.enumValues = {"du symbole", "red\xC3\xA9" "finie ici"};
                const std::string name = f.name;
                p.commit = [this, instId, name](std::string_view val) {
                    return val.rfind("red", 0) == 0 ? overrideFunction(instId, name, std::nullopt) : revertFunction(instId, name);
                };
                p.open = [this, instId, name] { editOverride(instId, name); };
                p.openTip = over ? "Modifier la red\xC3\xA9" "finition de " + inst.name + "\xE2\x80\xA6" : "Red\xC3\xA9" "finir " + name + " pour " + inst.name + "\xE2\x80\xA6";
                p.description = f.description + (f.description.empty() ? "" : "\n") + "Appel : " + call
                              + ". Virtuelle : \xC2\xAB red\xC3\xA9" "finie ici \xC2\xBB donne \xC3\xA0 " + inst.name
                              + " son propre corps (le bouton \xE2\x80\xA6 l'\xC3\xA9" "dite ; SUPER." + f.name
                              + "(...) y rappelle celui du symbole) ; \xC2\xAB du symbole \xC2\xBB l'efface.";
            }
            c.properties.push_back(std::move(p));
        }
        out.push_back(std::move(c));
    }
    std::vector<const hmi::View*> pops;
    for (const auto& w : doc_->project.views)
        if (w.ownerSymbol == sym->id) pops.push_back(&w);
    if (!pops.empty()) {
        PG::Category c;
        c.name = "Popups du symbole (" + std::to_string(pops.size()) + ")";
        const auto args = hmi::symbolArguments(*sym, inst, &doc_->project);
        std::string given;
        for (const auto& [n, t] : args) given += (given.empty() ? "" : ", ") + n + " := " + t;
        for (const auto* w : pops) {
            PG::Property p;
            p.name = w->name;
            p.type = PG::ValueType::ReadOnly;
            p.value = given.empty() ? std::string("les param\xC3\xA8tres par d\xC3\xA9" "faut") : "s'ouvre avec " + given;
            const Id wid = w->id;
            p.open = [this, wid] { openView->emit(wid); };
            p.openTip = "Ouvrir le dessin de " + w->name;
            p.description = "Dans la vue : action Ouvrir une popup, cible " + inst.name + "." + w->name + " ; depuis un script : IHM_POPUP('"
                          + v.name + "." + inst.name + "." + w->name + "').";
            c.properties.push_back(std::move(p));
        }
        out.push_back(std::move(c));
    }
    return out;
}

bool HmiEditor::overrideFunction(Id instance, const std::string& function, std::optional<std::string> body) {
    const auto* v = doc_->project.view(viewId_);
    const auto* inst = v ? v->object(instance) : nullptr;
    const auto* sym = inst ? hmi::symbolOf(doc_->project, *inst) : nullptr;
    const auto* f = sym ? hmi::symbolFunction(*sym, function) : nullptr;
    if (!f || !f->isVirtual) return false;
    // Les noms copies : la commande remplace la vue (les pointeurs ne valent plus apres).
    const std::string text = body ? *body : f->body, fname = f->name, iname = inst->name;
    // 1.11.18 (refonte, lot 5) : une redefinition neuve part du corps du symbole - avec ses
    // locales et ses constantes du modele (le corps les lit), copiees ; ses parametres restent
    // ceux de la fonction. La commande leur donne des identifiants neufs.
    std::vector<hmi::Declaration> locals;
    for (const auto& d : f->decls)
        if (d.kind != hmi::DeclKind::Parameter) locals.push_back(d);
    auto cmd = hmi::changeView(doc_, viewId_, "Red\xC3\xA9" "finir " + fname + " dans " + iname, [&](hmi::Project&, hmi::View& vv) {
        auto* o = vv.object(instance);
        if (!o) return;
        for (auto& fo : o->functionOverrides)
            if (hmikit::same(fo.function, fname)) { fo.body = text; return; }
        o->functionOverrides.push_back({fname, text, locals});
    });
    if (!cmd) return true;
    apply_(std::move(cmd));
    statusBar_->setMessage(iname + " : " + fname + " red\xC3\xA9" "finie (Ctrl+Z la reprend)");
    return true;
}

bool HmiEditor::revertFunction(Id instance, const std::string& function) {
    auto cmd = hmi::changeView(doc_, viewId_, "Revenir au corps du symbole : " + function, [&](hmi::Project&, hmi::View& vv) {
        if (auto* o = vv.object(instance))
            std::erase_if(o->functionOverrides, [&](const hmi::FunctionOverride& fo) { return hmikit::same(fo.function, function); });
    });
    if (cmd) apply_(std::move(cmd));
    return true;
}

void HmiEditor::editOverride(Id instance, const std::string& function) {
    const auto* v = doc_->project.view(viewId_);
    const auto* inst = v ? v->object(instance) : nullptr;
    const auto* sym = inst ? hmi::symbolOf(doc_->project, *inst) : nullptr;
    const auto* f = sym ? hmi::symbolFunction(*sym, function) : nullptr;
    if (!f || !f->isVirtual) return;
    const auto& host = actions_ ? actions_->dialogHost() : HmiActionsPanel::DialogHost{};
    if (!host) return;
    HmiActionScriptDialog::Spec spec;
    spec.doc = doc_;
    spec.view = sym->id;                      // les parametres du symbole, ses fonctions
    spec.plc = plc_;
    spec.function = *f;
    const auto* fo = hmi::functionOverride(*inst, f->name);
    spec.code = fo ? fo->body : f->body;
    // 1.11.18 (lot 5) : le controle lit les parametres de la fonction et les declarations de la
    // redefinition (une neuve : celles du symbole, qu'elle copiera).
    if (fo) {
        std::vector<hmi::Declaration> decls;
        for (const auto& d : f->decls)
            if (d.kind == hmi::DeclKind::Parameter) decls.push_back(d);
        decls.insert(decls.end(), fo->decls.begin(), fo->decls.end());
        spec.function->decls = std::move(decls);
    }
    spec.where = fo ? std::string("sa red\xC3\xA9" "finition") : std::string("le corps du symbole pour d\xC3\xA9part");
    spec.title = "Red\xC3\xA9" "finir " + f->name + " dans " + inst->name;
    const std::string name = f->name;
    std::weak_ptr<bool> alive = alive_;
    host(std::make_unique<HmiActionScriptDialog>(std::move(spec)), [this, alive, instance, name](const menu::DialogResult& r) {
        if (alive.expired() || r.button != menu::DialogResult::Button::Ok) return;
        (void)overrideFunction(instance, name, r.payload);
    });
}

void HmiEditor::rebuildProperties() {
    const auto* v = doc_->project.view(viewId_);
    if (!v) { props_->clearProperties(); return; }
    hmiparams::setAssistView(viewId_);   // 1.9 : l'aide a la saisie propose ses parametres
    HmiPropertyCommits c;
    c.prop = [this](const std::string& k, const std::string& val, bool expr) { return commitProp(k, val, expr); };
    c.meta = [this](const std::string& f, const std::string& val) { return commitMeta(f, val); };
    c.view = [this](const std::string& f, const std::string& val) { return commitView(f, val); };
    auto cats = hmiPropertyCategories(*v, canvas_->selection(), plc_.get(), c, &doc_->project.assets, &doc_->project);
    // 1.9 : les alarmes de l'objet pose et ses variables publiques, apres la categorie Objet (maquette A2, A5).
    if (const auto sel = canvas_->selection(); sel.size() == 1) {
        auto extra = objalarms::inspectorCategories(doc_, apply_, viewId_, sel.front(), plc_.get());
        // 1.10.4 (K3) : apres les sections communes (Objet, Position et taille, Apparence,
        // Securite), avant celle du genre ("Vanne", "Instance de symbole").
        std::size_t at = std::min<std::size_t>(1, cats.size());
        for (std::size_t k = 0; k < cats.size(); ++k)
            if (cats[k].name == "S\xC3\xA9" "curit\xC3\xA9") at = k + 1;
        cats.insert(cats.begin() + static_cast<long>(at), extra.begin(), extra.end());
    }
    // 1.11.10 : une instance - les fonctions de son symbole (une virtuelle se redefinit ici)
    // et ses popups, apres la section Parametres du symbole.
    if (const auto sel = canvas_->selection(); sel.size() == 1)
        if (const auto* inst = v->object(sel.front()); inst && inst->kind == hmi::Kind::SymbolInstance) {
            auto extra = instanceFunctionCategories(*v, *inst);
            std::size_t at = cats.size();
            for (std::size_t k = 0; k < cats.size(); ++k)
                if (cats[k].name.rfind("Param\xC3\xA8tres du symbole", 0) == 0) at = k + 1;
            cats.insert(cats.begin() + static_cast<long>(at), extra.begin(), extra.end());
        }
    // 1.10.2 (chantier D) : les REPERES de l'objet ($Vanne$ x 3), en tete de
    // l'inspecteur - "Dupliquer..." (Ctrl+D, le clic droit) les remplace.
    if (const auto sel = canvas_->selection(); sel.size() == 1 && !cats.empty()) {
        const auto marks = hmi::dup::markersOf(*v, sel.front());
        if (!marks.empty()) {
            std::string list;
            for (const auto& m : marks) list += (list.empty() ? "" : ", ") + ("$" + m.name + "$ \xC3\x97 " + std::to_string(m.uses));
            ui::PropertyGrid::Property p;
            p.name = "Rep\xC3\xA8res";
            p.value = list;
            p.type = ui::PropertyGrid::ValueType::ReadOnly;
            p.description = "Les rep\xC3\xA8res de l'objet et de ses \xC3\xA9l\xC3\xA9ments. Ctrl+D (ou le clic droit) : "
                            "\xC2\xAB Dupliquer\xE2\x80\xA6 \xC2\xBB fait les copies en rempla\xC3\xA7" "ant chaque rep\xC3\xA8re. "
                            "Compiler signale un rep\xC3\xA8re rest\xC3\xA9 dans un objet pos\xC3\xA9 (sauf dans \xC2\xAB Mod\xC3\xA8les d'objets \xC2\xBB).";
            cats.front().properties.insert(cats.front().properties.begin(), std::move(p));
        }
    }
    applyPropertyFilters(*v, cats);
    props_->setCategories(std::move(cats));
}

// 1.10.3 : les filtres de l'onglet Proprietes - les nombres, puis ce qui reste montre.
void HmiEditor::applyPropertyFilters(const hmi::View& v, std::vector<ui::PropertyGrid::Category>& cats) {
    if (!propFilters_) return;
    propFilters_->load();
    using PG = ui::PropertyGrid;
    const auto plainName = [](std::string_view name) {
        if (const auto at = name.find("  \xC6\x92"); at != std::string_view::npos) name = name.substr(0, at);
        return std::string(name);
    };
    // Les valeurs d'un objet neuf de ce genre (Non vides) : la meme grille, faite pour lui.
    std::map<std::string, std::string> defaults;
    const auto sel = canvas_->selection();
    if (sel.size() == 1)
        if (const auto* o = v.object(sel.front())) {
            hmi::View tmp;
            tmp.id = v.id;
            tmp.name = v.name;
            tmp.role = v.role;
            tmp.layers = v.layers;
            tmp.objects.push_back(hmi::makeObject(o->kind, o->id, {}, 0, 0, o->layer));
            const std::function<void(const PG::Category&)> walk = [&](const PG::Category& c) {
                for (const auto& p : c.properties) defaults.emplace(c.name + '\x1F' + plainName(p.name), p.value);
                for (const auto& ch : c.children) walk(ch);
            };
            for (const auto& c : hmiPropertyCategories(tmp, {o->id}, plc_.get(), HmiPropertyCommits{}, &doc_->project.assets, &doc_->project))
                walk(c);
        }
    const auto isSummary = [](const PG::Property& p) {   // la ligne "Reperes : $Vanne$ x 3" (1.10.2)
        return p.type == PG::ValueType::ReadOnly && p.name == "Rep\xC3\xA8res" && !p.commit;
    };
    const auto flagsOf = [&](const std::string& cat, const PG::Property& p) {
        std::array<bool, 3> f{};
        f[0] = !p.expression.empty();
        f[1] = ui::exprfield::hasMarker(p);
        // Non vides : ce que l'on a renseigne - pas une ligne de lecture seule (un resume),
        // ni "(aucun)" (rien de choisi).
        if (f[0]) f[2] = true;
        else if (p.type == PG::ValueType::ReadOnly || p.value == "(aucun)") f[2] = false;
        else if (const auto it = defaults.find(cat + '\x1F' + plainName(p.name)); it != defaults.end()) f[2] = p.value != it->second;
        else if (p.type == PG::ValueType::Boolean) f[2] = p.value == "TRUE" || p.value == "true" || p.value == "1";
        else f[2] = !p.value.empty();
        return f;
    };
    std::array<std::size_t, 3> counts{};
    const std::function<void(const PG::Category&)> count = [&](const PG::Category& c) {
        for (const auto& p : c.properties) {
            if (isSummary(p)) continue;
            const auto f = flagsOf(c.name, p);
            for (std::size_t i = 0; i < 3; ++i) counts[i] += f[i] ? 1u : 0u;
        }
        for (const auto& ch : c.children) count(ch);
    };
    for (const auto& c : cats) count(c);
    propFilters_->counts = counts;
    propFilters_->invalidate();
    if (!propFilters_->any()) return;
    const auto& on = propFilters_->on;
    // Une case reste si elle repond a au moins un filtre allume ; une section vide part.
    const std::function<bool(PG::Category&)> keep = [&](PG::Category& c) {
        std::vector<PG::Property> kept;
        for (auto& p : c.properties) {
            const bool show = isSummary(p) ? on[1] : [&] {
                const auto f = flagsOf(c.name, p);
                return (on[0] && f[0]) || (on[1] && f[1]) || (on[2] && f[2]);
            }();
            if (show) kept.push_back(std::move(p));
        }
        c.properties = std::move(kept);
        c.children.erase(std::remove_if(c.children.begin(), c.children.end(), [&](PG::Category& ch) { return !keep(ch); }),
                         c.children.end());
        return !c.properties.empty() || !c.children.empty();
    };
    cats.erase(std::remove_if(cats.begin(), cats.end(), [&](PG::Category& c) { return !keep(c); }), cats.end());
}

bool HmiEditor::commitProp(const std::string& key, const std::string& value, bool expr) {
    const auto sel = canvas_->selection();
    if (sel.empty()) return false;
    // Lot 12 : un style nomme choisi - ses valeurs s'appliquent aux objets choisis,
    // qui le citent ; vide : ils ne le citent plus (et gardent leurs valeurs).
    if (key == "namedStyle" && !expr) {
        const hmi::Style* found = value.empty() ? nullptr : doc_->project.styleByName(value);
        if (!value.empty() && !found) { warn("Style inconnu : " + value + " (IHM > Styles)"); return false; }
        const hmi::Style style = found ? *found : hmi::Style{};
        canvas_->edit(value.empty() ? std::string("Sans style") : "Style " + style.name, [&](hmi::Project&, hmi::View& vv) {
            for (Id id : sel) {
                auto* o = vv.object(id);
                if (!o) continue;
                if (value.empty()) { if (auto* cite = o->find("namedStyle")) cite->value.clear(); }
                else hmi::design::applyStyle(*o, style);
            }
        });
        statusBar_->setMessage(value.empty()
                                   ? std::to_string(sel.size()) + " objet(s) ne citent plus de style : ils gardent leurs valeurs."
                                   : std::to_string(sel.size()) + " objet(s) citent le style " + style.name
                                         + " : ses valeurs s'appliquent, ses changements suivront (IHM > Styles). Ctrl+Z le reprend.");
        return true;
    }
    // 1.10 (chantier K) : "Revenir a la valeur par defaut" (clic droit d'un champ a
    // expression) : l'expression part et la valeur devient celle d'un objet neuf
    // de ce genre - une seule commande (Ctrl+Z).
    if (!expr && value == ui::exprfield::kToDefault) {
        std::string label, help, shown;
        if (!hmiPropertyInfo(key, label, help) || label.empty()) label = key;
        canvas_->edit("Valeur par d\xC3\xA9" "faut " + key, [&](hmi::Project&, hmi::View& vv) {
            for (Id id : sel) {
                auto* o = vv.object(id);
                if (!o) continue;
                const auto fresh = hmi::makeObject(o->kind, hmi::kNoId, {}, 0, 0, hmi::kNoId);
                const auto* d = fresh.find(key);
                auto* pp = o->find(key);
                if (pp) pp->expr.clear();
                if (d) o->set(key, d->value);
                else if (pp && (key == "rot" || key == "access")) pp->value = "0";
                if (shown.empty()) shown = o->text(key);
            }
            hmi::edit::refreshGroupBounds(vv);
        });
        statusBar_->setTransientMessage(label + " revient \xC3\xA0 sa valeur par d\xC3\xA9" "faut ("
                                            + (shown.empty() ? std::string("vide") : shown) + "). Ctrl+Z pour annuler.",
                                        6.0, ui::StatusBar::Severity::Info);
        return true;
    }
    std::string error;
    if (expr && !value.empty()) {
        std::string why;
        if (!hmi::isReadOnly(value, &why)) { warn(why); return false; }
    }
    // Un nombre attendu, et autre chose de tape : le dire, au lieu de ne
    // rien faire en silence.
    static const std::set<std::string, std::less<>> numeric = {
        "x", "y", "w", "h", "rot", "pivotX", "pivotY", "strokeWidth", "radius", "opacity", "fontSize",
        "min", "max", "rows", "duration", "ymin", "ymax", "refresh", "access"};
    if (!expr && numeric.count(key)) {
        double n = 0;
        // 1.10 (chantier K) : vider un nombre le ramene a sa valeur par defaut
        // (celle d'un objet neuf de ce genre) - on pouvait ne plus le vider.
        if (value.find_first_not_of(" \t") == std::string::npos) {
            const auto* v0 = canvas_->view();
            const auto* o0 = v0 ? v0->object(sel.front()) : nullptr;
            const auto fresh = o0 ? hmi::makeObject(o0->kind, hmi::kNoId, {}, 0, 0, hmi::kNoId) : hmi::Object{};
            if (const auto* d = fresh.find(key); d && hmi::parseNumber(d->value, n)) return commitProp(key, d->value, false);
            if (key == "rot" || key == "access") return commitProp(key, "0", false);
        }
        if (!hmi::parseNumber(value, n)) {
            warn("\xC2\xAB " + value + " \xC2\xBB n'est pas un nombre (" + key
                 + ") : rien n'a chang\xC3\xA9. Pour une expression, commencer par =.");
            return false;
        }
    }
    // Lot 10 : le symbole d'une instance - un symbole du projet, qui ne contient
    // pas la vue ou on le pose ; l'instance prend sa taille.
    const hmi::View* chosenSymbol = nullptr;
    if (key == "symbol" && !expr) {
        const auto* v = canvas_->view();
        chosenSymbol = doc_->project.viewByName(value);
        if (!chosenSymbol || !hmi::isSymbolView(*chosenSymbol)) { warn("'" + value + "' n'est pas un symbole du projet"); return false; }
        if (v && hmi::isSymbolView(*v) && (chosenSymbol->id == v->id || hmi::symbolContains(doc_->project, *chosenSymbol, v->name))) {
            warn("Refus\xC3\xA9 : " + value + " contient " + v->name + " (un symbole ne se contient pas lui-m\xC3\xAAme)");
            return false;
        }
    }
    const int symbolW = chosenSymbol ? chosenSymbol->width : 0, symbolH = chosenSymbol ? chosenSymbol->height : 0;
    const bool geometric = key == "x" || key == "y" || key == "w" || key == "h" || key == "rot";
    // 1.10 (chantier K) : l'expression retiree se dit (maquette : "Expression retiree :
    // X revient a sa valeur fixe (...) - Ctrl+Z pour annuler").
    bool removedExpr = false;
    std::string fixedBack;
    canvas_->edit(expr ? "Expression " + key : "Propri\xC3\xA9t\xC3\xA9 " + key, [&](hmi::Project&, hmi::View& vv) {
        for (Id id : sel) {
            auto* o = vv.object(id);
            if (!o) continue;
            if (expr) {
                if (value.empty() && !o->expr(key).empty()) removedExpr = true;
                o->setExpr(key, value);
                // 1.10 (chantier K) : l'expression retiree, la propriete revient a sa
                // valeur fixe ; elle n'en avait pas : celle d'un objet neuf de ce genre.
                if (value.empty())
                    if (auto* pp = o->find(key); pp && pp->value.empty())
                        if (const auto* d = hmi::makeObject(o->kind, hmi::kNoId, {}, 0, 0, hmi::kNoId).find(key)) pp->value = d->value;
                if (removedExpr && fixedBack.empty()) fixedBack = o->text(key);
                continue;
            }
            // 1.10 (chantier K) : une valeur fixe tapee sur une propriete pilotee (le
            // champ montrait "=expression") remplace l'expression.
            if (auto* pp = o->find(key); pp && !pp->expr.empty()) pp->expr.clear();
            if (geometric && !o->find(key)) continue;
            if (key == "rot") {
                double a = 0;
                if (hmi::parseNumber(value, a)) hmi::edit::setRotation(vv, {id}, a);
                continue;
            }
            if (key == "w" || key == "h") {
                double n = 0;
                if (!hmi::parseNumber(value, n) || n < 0) continue;
                hmi::Box b = o->box();
                (key == "w" ? b.w : b.h) = n;
                hmi::edit::setBox(vv, id, b);
                continue;
            }
            // Une propriete que l'objet n'a pas : posee sur l'objet seul choisi,
            // et sur tous pour la securite (niveau d'acces, autorisation).
            if (!o->find(key) && sel.size() > 1 && key != "access" && key != "auth") continue;
            if (key == "symbol" && o->kind != hmi::Kind::SymbolInstance) continue;
            o->set(key, value);
            if (key == "symbol" && symbolW > 0 && symbolH > 0) {
                o->setNumber("w", symbolW);
                o->setNumber("h", symbolH);
            }
        }
        hmi::edit::refreshGroupBounds(vv);
    });
    if (removedExpr) {
        std::string label, help;
        if (!hmiPropertyInfo(key, label, help) || label.empty()) label = key;
        statusBar_->setTransientMessage("Expression retir\xC3\xA9" "e : " + label + " revient \xC3\xA0 sa valeur fixe ("
                                            + (fixedBack.empty() ? std::string("vide") : fixedBack) + "). Ctrl+Z pour annuler.",
                                        6.0, ui::StatusBar::Severity::Info);
    }
    return true;
}

bool HmiEditor::makeStyle(const std::string& name, std::string* why) {
    const auto sel = canvas_->selection();
    const auto* v = canvas_->view();
    const auto* first = v && !sel.empty() ? v->object(sel.front()) : nullptr;
    if (!first) {
        if (why) *why = "choisis d'abord l'objet dont le style est fait";
        return false;
    }
    const Id vid = viewId_;
    const Id from = first->id;
    std::string made;
    auto cmd = hmi::changeProject(doc_, "Cr\xC3\xA9" "er le style " + name, [&](hmi::Project& p) {
        auto* vv = p.view(vid);
        const auto* o = vv ? vv->object(from) : nullptr;
        if (!o) return;
        hmi::Style style = hmi::design::styleFromObject(p, *o, name);
        made = style.name;
        for (Id id : sel)
            if (auto* target = vv->object(id)) hmi::design::applyStyle(*target, style);
        p.styles.push_back(std::move(style));
    });
    if (!cmd || made.empty()) {
        if (why) *why = "rien \xC3\xA0 faire un style (l'objet n'a pas de couleurs ni de police)";
        return false;
    }
    apply_(std::move(cmd));
    refresh();
    statusBar_->setMessage("Style " + made + " cr\xC3\xA9\xC3\xA9 (IHM > Styles) : " + std::to_string(sel.size())
                           + " objet(s) le citent et suivront ses changements. Ctrl+Z le retire.");
    return true;
}

bool HmiEditor::commitMeta(const std::string& field, const std::string& value) {
    const auto sel = canvas_->selection();
    if (sel.empty()) return false;
    // Lot API 8 : renommer partout (IHM) - ce qui cite l'objet suit (renameObject).
    if (field == "nom") return renameObject(sel.front(), value);
    std::string why;
    bool ok = true;
    canvas_->edit("Objet", [&](hmi::Project&, hmi::View& vv) {
        if (field == "calque") {
            for (const auto& l : vv.layers) if (l.name == value) { hmi::edit::moveToLayer(vv, sel, l.id); return; }
            ok = false;
            why = "calque inconnu";
            return;
        }
        if (field == "verrou") { hmi::edit::setLocked(vv, sel, hmi::parseBool(value, false)); return; }
        if (field == "cache") { hmi::edit::setHidden(vv, sel, hmi::parseBool(value, false)); return; }
    });
    if (!ok) warn(why);
    return ok;
}

bool HmiEditor::commitView(const std::string& field, const std::string& value) {
    bool ok = true;
    std::string why;
    // 1.9 : la sous-section Parametres | Infos d'une popup (HmiParamPanes) ; une
    // commande annulable, sauf la partie montree et le clic des infos.
    if (hmiparams::isParamField(field)) {
        if (field == "param19:partie") {
            hmiparams::showPart(viewId_, value.rfind("Infos", 0) == 0 ? hmiparams::Part::Infos : hmiparams::Part::List);
            rebuildProperties();
            return true;
        }
        if (field.rfind("param19:aller:", 0) == 0) {
            const auto t = hmiparams::parseTarget(field.substr(14));
            const auto* cur = doc_->project.view(viewId_);
            if (cur && cur->name == t.view) {
                for (const auto& o : cur->objects)
                    if (o.name == t.object) canvas_->setSelection({o.id});
            } else {
                hmiparams::openObject(t.view, t.object);
            }
            return true;
        }
        hmiparams::FieldResult res;
        auto cmd = hmi::changeProject(doc_, hmiparams::fieldLabel(field, value),
                                      [&](hmi::Project& p) { res = hmiparams::applyParamField(p, viewId_, field, value); });
        if (res.ok && cmd) apply_(std::move(cmd));
        if (!res.ok) warn(res.why);
        return res.ok;
    }
    if (field == "nom") {
        // Lot 7 : le dialogue qui montre ce qui la cite (navigations, Vue.Objet,
        // scripts, essais) et le reecrit en une commande ; sans lui (un essai
        // sans l'ecran d'analyse), comme avant.
        if (const auto* cur = doc_->project.view(viewId_); cur && !value.empty() && value != cur->name && requestRename("ihm-vue", cur->name, value))
            return false;
        // Le nom d'une vue est une affaire de projet (unicite, arbre).
        auto cmd = hmi::changeProject(doc_, "Renommer la vue", [&](hmi::Project& p) {
            if (value.empty()) { ok = false; why = "une vue a un nom"; return; }
            for (const auto& other : p.views)
                if (other.id != viewId_ && other.name == value) { ok = false; why = "'" + value + "' existe d\xC3\xA9j\xC3\xA0"; return; }
            auto* v = p.view(viewId_);
            if (!v) return;
            // Lot 10 : un symbole renomme - ses instances suivent.
            if (hmi::isSymbolView(*v)) {
                if (!hmi::isIdentifier(value)) { ok = false; why = "un symbole se nomme comme une variable (lettres, chiffres, _)"; return; }
                (void)hmi::renameSymbol(p, v->name, value);
                v = p.view(viewId_);
            }
            v->name = value;
        });
        if (ok && cmd) apply_(std::move(cmd));
        if (!ok) warn(why);
        return ok;
    }
    canvas_->edit("Vue", [&](hmi::Project& p, hmi::View& vv) {
        double n = 0;
        if (field == "description") vv.description = value;
        else if (field == "largeur" && hmi::parseNumber(value, n) && n >= 16) vv.width = static_cast<int>(n);
        else if (field == "hauteur" && hmi::parseNumber(value, n) && n >= 16) vv.height = static_cast<int>(n);
        else if (field == "fond") vv.background = value;
        else if (field == "grille") vv.grid.visible = hmi::parseBool(value, true);
        else if (field == "pas" && hmi::parseNumber(value, n) && n >= 1) vv.grid.step = static_cast<int>(n);
        else if (field == "mag_grille") vv.grid.snapGrid = hmi::parseBool(value, true);
        else if (field == "mag_objets") vv.grid.snapObjects = hmi::parseBool(value, true);
        else if (field == "mag_guides") vv.grid.snapGuides = hmi::parseBool(value, true);
        else if (field == "calque_actif") {
            for (const auto& l : vv.layers) if (l.name == value) vv.activeLayer = l.id;
        }
        // ---- lot 6 : role, ecran modele, en-tete, pied
        else if (field == "role") {
            const std::string role(hmi::viewRoleFromLabel(value));
            if (role == vv.role) return;
            // Une vue dont d'autres heritent ne change pas de role en silence.
            if (hmi::isTemplateRole(vv.role) && !hmi::viewsUsing(p, vv.id).empty() && role != vv.role) {
                ok = false;
                why = vv.name + " est utilis\xC3\xA9" "e par " + std::to_string(hmi::viewsUsing(p, vv.id).size())
                    + " vue(s) : retire-la d'abord de leurs propri\xC3\xA9t\xC3\xA9s";
                return;
            }
            // Lot 10 : un symbole pose quelque part le reste.
            if (hmi::isSymbolView(vv) && !hmi::instancesOf(p, vv.name).empty()) {
                ok = false;
                why = vv.name + " a " + std::to_string(hmi::instancesOf(p, vv.name).size())
                    + " instance(s) : supprime-les d'abord, ou garde le r\xC3\xB4le Symbole";
                return;
            }
            vv.role = role;
            // Une bande d'en-tete ou de pied : de la largeur de l'IHM, 80 de haut.
            if ((role == "entete" || role == "pied") && vv.height > 200) vv.height = 80;
            if (role == "entete" || role == "pied") { vv.templateView = hmi::kNoId; vv.showHeader = vv.showFooter = false; }
        } else if (field == "modele") {
            if (value.empty() || value == "(aucun)") { vv.templateView = hmi::kNoId; return; }
            const auto* t = p.viewByName(value);
            if (!t || t->role != "modele") { ok = false; why = "'" + value + "' n'est pas un \xC3\xA9" "cran mod\xC3\xA8le"; return; }
            // Pas de boucle : le modele ne doit pas heriter (de proche en proche) de cette vue.
            for (const auto* up : hmi::templateChain(p, *t))
                if (up->id == vv.id) { ok = false; why = "h\xC3\xA9ritage circulaire : " + t->name + " h\xC3\xA9rite d\xC3\xA9j\xC3\xA0 de " + vv.name; return; }
            if (t->id == vv.id) { ok = false; why = "une vue n'h\xC3\xA9rite pas d'elle-m\xC3\xAAme"; return; }
            vv.templateView = t->id;
        } else if (field == "entete") vv.showHeader = hmi::parseBool(value, false);
        else if (field == "pied") vv.showFooter = hmi::parseBool(value, false);
        else if (field == "entete_modele" || field == "pied_modele") {
            const bool head = field == "entete_modele";
            if (value.empty() || value == "(le premier)") { (head ? vv.header : vv.footer) = hmi::kNoId; return; }
            const auto* t = p.viewByName(value);
            if (!t || t->role != (head ? "entete" : "pied")) {
                ok = false;
                why = "'" + value + "' n'est pas un mod\xC3\xA8le " + (head ? std::string("d'en-t\xC3\xAAte") : std::string("de pied de page"));
                return;
            }
            (head ? vv.header : vv.footer) = t->id;
        }
        // ---- lot 8 : la popup (sa barre, son comportement, sa place) et les parametres
        else if (field == "popup_titre") vv.popup.titleBar = hmi::parseBool(value, true);
        else if (field == "popup_libelle") vv.popup.title = value;
        else if (field == "popup_modale") vv.popup.modal = hmi::parseBool(value, true);
        else if (field == "popup_deplacable") vv.popup.movable = hmi::parseBool(value, true);
        else if (field == "popup_fermer") vv.popup.closeButton = hmi::parseBool(value, true);
        else if (field == "popup_dehors") vv.popup.closeOutside = hmi::parseBool(value, false);
        else if (field == "popup_position") vv.popup.placement = hmi::popupPlacementFromLabel(value);
        else if (field == "parametres") {
            // "Moteur := Pompes[0]; Titre := 'Pompe 1'" : les noms et leurs valeurs par
            // defaut ; la description d'un parametre qui reste est gardee.
            std::vector<hmi::ViewParam> next;
            for (const auto& [name, def] : hmi::parseArguments(value)) {
                if (!hmi::isIdentifier(name)) { ok = false; why = "param\xC3\xA8tre : nom invalide '" + name + "'"; return; }
                hmi::ViewParam prm;
                prm.name = name;
                prm.defaultValue = def;
                if (const auto* old = vv.param(name)) {
                    prm.description = old->description;
                    prm.type = old->type;   // 1.9 : le type et le mode restent
                    prm.mode = old->mode;
                }
                next.push_back(std::move(prm));
            }
            vv.params = std::move(next);
        }
        // ---- lot 12 : la vue parente (fil d'Ariane), le zoom en marche
        else if (field == "vue_parente") {
            if (value.empty() || value == "(aucune)") { vv.upView = hmi::kNoId; return; }
            const auto* up = p.viewByName(value);
            if (!up) { ok = false; why = "'" + value + "' : vue introuvable"; return; }
            // Pas de boucle : la parente ne doit pas descendre (de proche en proche) de cette vue.
            for (const auto* x = up; x; x = x->upView != hmi::kNoId ? p.view(x->upView) : nullptr) {
                if (x->id == vv.id) { ok = false; why = "boucle : " + up->name + " a d\xC3\xA9j\xC3\xA0 " + vv.name + " pour parente"; return; }
                if (x->upView == x->id) break;
            }
            vv.upView = up->id;
        } else if (field == "zoom") vv.zoomable = hmi::parseBool(value, false);
        else ok = false;
    });
    if (!ok && !why.empty()) warn(why);
    return ok;
}

// Un refus se lit : il reste huit secondes, par-dessus ce que la vue dit du
// survol de la souris, qui l'effacait a la premiere image.
void HmiEditor::warn(const std::string& why) {
    statusBar_->setTransientMessage(why, 8.0, ui::StatusBar::Severity::Warning);
}

// ---- Lot API 8 : renommer partout (IHM) ----
bool HmiEditor::renameObject(Id id, const std::string& name, std::string* why) {
    std::string reason;
    bool ok = true;
    std::size_t followed = 0;
    std::string old;
    auto cmd = hmi::changeProject(doc_, "Renommer", [&](hmi::Project& p) {
        auto* v = p.view(viewId_);
        const auto* o = v ? v->object(id) : nullptr;
        if (!o) { ok = false; reason = "objet introuvable"; return; }
        old = o->name;
        ok = hmi::edit::rename(*v, id, name, &reason);
        if (ok && !old.empty() && old != name) followed = hmi::renameObjectReferences(p, v->name, old, name);
    });
    if (!ok) {
        if (why) *why = reason;
        warn(reason);
        return false;
    }
    if (cmd) apply_(std::move(cmd));
    canvas_->documentChanged();
    // Rien ne le citait (un objet qu'on vient de poser) : pas de message, comme avant.
    if (followed > 0)
        statusBar_->setTransientMessage("Renomm\xC3\xA9 : " + old + " \xE2\x86\x92 " + name + " - " + std::to_string(followed)
                                            + " endroit(s) qui le citaient ont suivi (Ctrl+Z : tout revient)",
                                        6.0);
    return true;
}

void HmiEditor::setHistory(History history, std::function<bool(bool redo)> possible) {
    history_ = std::move(history);
    historyPossible_ = std::move(possible);
}

bool HmiEditor::makeSymbol(const std::string& name, const std::string& params, std::string* why) {
    const auto sel = canvas_->selection();
    std::string reason;
    bool ok = false;
    hmi::Id made = hmi::kNoId;
    auto cmd = hmi::changeProject(doc_, "Cr\xC3\xA9" "er le symbole " + name, [&](hmi::Project& p) {
        ok = hmi::createSymbol(p, viewId_, sel, name, params, &made, &reason);
    });
    if (!ok) {
        if (why) *why = reason;
        return false;
    }
    if (cmd) apply_(std::move(cmd));
    canvas_->setSelection({made});
    statusBar_->setTransientMessage("Symbole " + name + " cr\xC3\xA9\xC3\xA9 (IHM > Symboles, et la biblioth\xC3\xA8que) : une instance "
                                    "remplace la s\xC3\xA9lection. Double-clic dessus : ouvrir le symbole.", 10.0,
                                    ui::StatusBar::Severity::Success);
    return true;
}

void HmiEditor::run(int action) {
    using hmi::edit::Align;
    using hmi::edit::ZMove;
    auto& c = *canvas_;
    switch (action) {
        case ActUndo:
        case ActRedo:
            if (history_) history_(action == ActRedo);
            break;
        case ActSaveTemplate:
            if (askTemplate_) askTemplate_();
            break;
        case ActMakeStyle: {
            // Lot 12 : un style nomme fait de l'objet choisi (le premier), applique
            // a toute la selection. L'hote demande le nom ; sans lui : "Style".
            if (c.selection().empty()) break;
            if (askStyle_) { askStyle_(); break; }
            std::string why;
            if (!makeStyle("Style", &why)) warn(why);
            break;
        }
        case ActMakeSymbol: {
            if (c.selection().empty()) break;
            if (askSymbol_) { askSymbol_(); break; }
            const auto* v = c.view();
            if (!v) break;
            std::string why;
            if (!makeSymbol(hmi::uniqueViewName(doc_->project, "Symbole"), hmi::suggestSymbolParams(*v, c.selection()), &why))
                warn(why);
            break;
        }
        case ActCopy:         c.copySelection(); break;
        case ActPaste:        c.paste(); break;
        case ActDuplicate:    if (askDuplicate_) askDuplicate_(); else c.duplicateSelection(); break;   // 1.10.2 : "Dupliquer..."
        case ActDelete:       c.deleteSelection(); break;
        case ActGroup:        c.groupSelection(); break;
        case ActUngroup:      c.ungroupSelection(); break;
        case ActFront:        c.zorder(ZMove::Front); break;
        case ActBack:         c.zorder(ZMove::Back); break;
        case ActForward:      c.zorder(ZMove::Forward); break;
        case ActBackward:     c.zorder(ZMove::Backward); break;
        case ActAlignLeft:    c.align(Align::Left); break;
        case ActAlignCenterH: c.align(Align::CenterH); break;
        case ActAlignRight:   c.align(Align::Right); break;
        case ActAlignTop:     c.align(Align::Top); break;
        case ActAlignCenterV: c.align(Align::CenterV); break;
        case ActAlignBottom:  c.align(Align::Bottom); break;
        case ActDistributeH:  c.distribute(true); break;
        case ActDistributeV:  c.distribute(false); break;
        case ActRotateLeft:   c.rotateSelection(-90); break;
        case ActRotateRight:  c.rotateSelection(90); break;
        case ActRotate45:     c.rotateSelection(45); break;
        case ActRotate180:    c.rotateSelection(180); break;
        case ActMirrorH:      c.mirrorSelection(true); break;
        case ActMirrorV:      c.mirrorSelection(false); break;
        case ActLock:         c.lockSelection(true); break;
        case ActUnlock:       c.lockSelection(false); break;
        case ActStyleCopy:    c.copyStyle(); break;
        case ActStylePaste:   c.pasteStyle(); break;
        case ActGrid:         c.toggleGrid(); break;
        case ActSnap:         c.toggleSnap(); break;
        case ActGuideV:       c.addGuide(true); break;
        case ActGuideH:       c.addGuide(false); break;
        case ActShowHidden:   c.setShowHidden(!c.showHidden()); break;
        case ActZoomOut:      c.zoomBy(1.f / 1.25f); break;
        case ActZoomIn:       c.zoomBy(1.25f); break;
        case ActZoomFit:      c.zoomToFit(); break;
        case ActLayerAdd:
            c.edit("Nouveau calque", [](hmi::Project& p, hmi::View& vv) { (void)hmi::edit::addLayer(p, vv); });
            break;
        case ActLayerRemove: {
            std::string why;
            bool ok = true;
            c.edit("Supprimer le calque", [&](hmi::Project&, hmi::View& vv) { ok = hmi::edit::removeLayer(vv, vv.activeLayer, &why); });
            if (!ok) warn(why);
            break;
        }
        case ActLayerUp:
            c.edit("Monter le calque", [](hmi::Project&, hmi::View& vv) { hmi::edit::moveLayer(vv, vv.activeLayer, +1); });
            break;
        case ActLayerDown:
            c.edit("Descendre le calque", [](hmi::Project&, hmi::View& vv) { hmi::edit::moveLayer(vv, vv.activeLayer, -1); });
            break;
        case ActLayerSelect:
            if (const auto* v = c.view()) c.setSelection(hmi::edit::objectsInLayer(*v, v->activeLayer));
            break;
        case ActLayerMoveHere: {
            const auto sel = c.selection();
            c.edit("Changer de calque", [&](hmi::Project&, hmi::View& vv) { hmi::edit::moveToLayer(vv, sel, vv.activeLayer); });
            break;
        }
        default: break;
    }
    invalidate();
}

void HmiEditor::showAction(Id object, int index) {
    const auto* v = doc_->project.view(viewId_);
    if (!v) return;
    if (object != kNoId && !v->object(object)) object = kNoId;
    canvas_->setSelection(object != kNoId ? std::vector<Id>{object} : std::vector<Id>{});
    if (object != kNoId) objects_->revealSelection();
    actions_->setOwner(object);
    inspector_->setCurrentIndex(1);
    if (index >= 0) actions_->selectIndex(index);
}

// ---- 1.10.3 (Q1103) : le clic d'une ligne d'un objet deplie (les deux explorateurs) ----
void HmiEditor::showLine(Id object, const hmitree::Line& line) {
    const auto* v = doc_->project.view(viewId_);
    if (!v || !v->object(object)) return;
    if (line.action >= 0) { showAction(object, line.action); return; }
    if (canvas_->selection() != std::vector<Id>{object}) {
        canvas_->setSelection({object});
        objects_->setSelection({object});
    }
    if (!line.property.empty()) (void)revealProperty(object, line.property);
}

bool HmiEditor::revealProperty(Id object, std::string_view key) {
    const auto* v = doc_->project.view(viewId_);
    if (!v || !v->object(object) || key.empty()) return false;
    inspector_->setCurrentIndex(0);
    layout();                                        // les proprietes de l'objet choisi
    // (1.10.2, venu de l'arbre) l'expression d'une animation : "Valeur (expression)".
    std::string label;
    for (const auto& cat : props_->categories())
        for (const auto& pr : cat.properties)
            if (label.empty() && pr.name.find(std::string(hmitree::animatedLabel(key))) == 0
                && pr.name.find("(expression)") != std::string::npos)
                label = pr.name;
    if (label.empty() && key == "variable")                               // 1.10.4 (K3) : plus de "Variable API"
        if (const auto* o = v->object(object)) label = hmiVariableLabel(o->kind);
    if (label.empty() && key == "auth") label = "Autorisation (expression)";
    if (label.empty() && key == "access") label = "Niveau d'acc\xC3\xA8s";
    // 1.11.2 (SYM) : une instance seule dont le symbole a des parametres - la premiere ligne de la
    // section « Parametres du symbole » ; sinon la ligne Arguments (1.10.2, chantier A).
    if (label.empty() && key == "params")
        for (const auto& cat : props_->categories())
            if (label.empty() && cat.name == "Param\xC3\xA8tres du symbole" && !cat.properties.empty())
                label = cat.properties.front().name;
    if (label.empty() && key == "params") label = "Arguments";
    // 1.10.3 : sinon, son libelle dans l'inspecteur ("Libelle", "Ouverture (%)"), suivi
    // de "  f" quand une expression la pilote ; a defaut, le premier nom qui commence par lui.
    if (label.empty()) {
        std::string info, help;
        if (!hmiPropertyInfo(key, info, help)) info = std::string(hmitree::animatedLabel(key));
        const std::string driven = info + "  \xC6\x92";
        std::string prefix;
        for (const auto& cat : props_->categories())
            for (const auto& pr : cat.properties) {
                if (label.empty() && (pr.name == info || pr.name == driven)) label = pr.name;
                if (prefix.empty() && !info.empty() && pr.name.rfind(info, 0) == 0) prefix = pr.name;
            }
        if (label.empty()) label = prefix;
    }
    return !label.empty() && props_->revealValue(label);
}

void HmiEditor::onLayout() {
    const auto b = bounds();
    const float toolH = 38.f, statusH = 24.f;
    tools_->setBounds({b.x, b.y, b.w, toolH});
    statusBar_->setBounds({b.x, b.y + b.h - statusH, b.w, statusH});
    split_->setBounds({b.x, b.y + toolH, b.w, std::max(0.f, b.h - toolH - statusH)});
    // 1.11.3 : un menu sans bornes ne se dessine pas (il est ouvert, rien ne se voit).
    context_->setBounds(b);
    legendMenu_->setBounds(b);
    if (symbolTabs_) {
        // 1.9 : les sous-onglets du symbole ; Alarmes et Instances : le volet Alarmes a la place du dessin.
        const float tabH = 30.f;
        const gfx::Rect rest{b.x, b.y + toolH + tabH, b.w, std::max(0.f, b.h - toolH - tabH - statusH)};
        symbolTabs_->setBounds({b.x, b.y + toolH, b.w, tabH});
        const bool drawing = symbolTabs_->current() == HmiSymbolTabs::Drawing;
        const bool operators = symbolTabs_->current() == HmiSymbolTabs::Operators;   // 1.10 (S2)
        const bool functions = symbolTabs_->current() == HmiSymbolTabs::Functions;   // 1.11.10
        const bool popups = symbolTabs_->current() == HmiSymbolTabs::Popups;         // 1.11.10
        const auto shown = [](bool on) { return on ? ui::Visibility::Visible : ui::Visibility::Collapsed; };
        split_->setVisibility(shown(drawing));
        symbolAlarms_->setVisibility(shown(!drawing && !operators && !functions && !popups));
        symbolOperators_->setVisibility(shown(operators));
        symbolFunctions_->setVisibility(shown(functions));
        symbolPopups_->setVisibility(shown(popups));
        split_->setBounds(rest);
        symbolAlarms_->setBounds(rest);
        symbolOperators_->setBounds(rest);
        symbolFunctions_->setBounds(rest);
        symbolPopups_->setBounds(rest);
    }
    rebuildProperties();
    // Les actions suivent la selection : un seul objet, les siennes ; rien, celles
    // de la vue ; plusieurs, celles de la vue aussi (une action est a un objet).
    const auto sel = canvas_->selection();
    const Id owner = sel.size() == 1 ? sel.front() : kNoId;
    if (owner != actions_->owner()) actions_->setOwner(owner);
    if (owner != content_->object()) content_->setObject(owner);
    // 1.10.4 (K3) : l'onglet Contenu, seulement pour un objet qui peut avoir un contenu
    // (un tableau, une courbe, un groupe...) ; une vanne, un bouton, la vue : pas d'onglet.
    const bool wantContent = content_->mode() != HmiContentPanel::Mode::None;
    const int contentTab = inspector_->indexOf(content_);
    if (wantContent && contentTab < 0 && contentPage_) {
        const std::size_t at = inspector_->addTab(ui::TabControl::Tab{"Contenu", ui::Icon::AnimationTable, false, false},
                                                  std::move(contentPage_));
        const std::size_t n = content_->count();
        inspector_->setTabBadge(at, n ? std::to_string(n) : std::string{}, ui::Tone::Accent);
    } else if (!wantContent && contentTab >= 0) {
        contentPage_ = inspector_->takeTab(static_cast<std::size_t>(contentTab));
    }
}

void HmiEditor::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.windowBg);
}

} // namespace app
