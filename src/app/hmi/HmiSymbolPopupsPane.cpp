#include "HmiSymbolPopupsPane.hpp"

#include "HmiAssetPanes.hpp"     // hmiSelectModelRow
#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"
#include "../../hmi/HmiSymbols.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>

namespace app {

using hmi::Id;
using hmi::kNoId;
using hmikit::Rows;

namespace {
enum ToolAction : int { TNew = 1, TOpen, TDelete };

// Les actions du symbole (ses objets, la vue) qui ouvrent la popup `name`.
std::size_t openersOf(const hmi::View& sym, const std::string& name) {
    std::size_t n = 0;
    const auto count = [&](const std::vector<hmi::Action>& list) {
        for (const auto& a : list)
            if ((a.operation == hmi::Operation::Popup || a.operation == hmi::Operation::ChangePopup) && hmikit::same(a.target, name)) ++n;
    };
    count(sym.actions);
    for (const auto& o : sym.objects) count(o.actions);
    return n;
}
} // namespace

HmiSymbolPopupsPane::HmiSymbolPopupsPane(std::string id, hmi::DocumentPtr doc, Id symbol, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), symbol_(symbol), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(TNew, HmiGlyph::Plus, "Nouvelle popup du symbole : elle conna\xC3\xAEt ses param\xC3\xA8tres et ses fonctions", "Nouvelle popup");
    tools->add(TOpen, HmiGlyph::Popup, "Ouvrir le dessin de la popup (un onglet, comme une vue)", "Ouvrir le dessin");
    tools->add(TDelete, HmiGlyph::Delete, "Supprimer la popup (Ctrl+Z la rend)", "Supprimer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(TOpen, [this] { return selectedPopup() != kNoId; });
    tools_->setEnabledWhen(TDelete, [this] { return selectedPopup() != kNoId; });

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    {
        const auto* sym = doc_->project.view(symbol_);
        auto panel = std::make_unique<HmiTitledPanel>(base + ".listPanel", "POPUPS DE " + (sym ? sym->name : std::string("?")));
        auto table = std::make_unique<ui::TableView>(base + ".popups");
        table->setColumns({{"Nom", 190.f}, {"Taille", 100.f}, {"Param\xC3\xA8tres propres", 170.f}, {"Ouverte par", 150.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        table_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        split->addPane(std::move(panel), 0.55f, 260.f);
    }
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".infoPanel", "CE QU'ELLE CONNA\xC3\x8ET");
        auto grid = std::make_unique<ui::PropertyGrid>(base + ".info");
        grid->setShowDescriptionPane(false);
        grid->setNameColumnRatio(0.42f);
        info_ = &static_cast<ui::PropertyGrid&>(panel->setBody(std::move(grid)));
        split->addPane(std::move(panel), 0.45f, 220.f);
    }
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedPopup();
        switch (a) {
            case TNew: (void)addPopup(); break;
            case TOpen: if (sel && openView) openView(sel); break;
            case TDelete: if (sel) (void)deletePopup(sel); break;
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        selectedRow_ = rows.empty() ? -1 : static_cast<int>(rows.front());
        rebuildInfo();
    });
    links_ += table_->activated->connect([this](ui::RowIndex) {
        if (const Id sel = selectedPopup(); sel && openView) openView(sel);
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

std::vector<Id> HmiSymbolPopupsPane::popups() const {
    std::vector<Id> out;
    for (const auto& v : doc_->project.views)
        if (v.ownerSymbol == symbol_ && symbol_ != kNoId) out.push_back(v.id);
    return out;
}

Id HmiSymbolPopupsPane::selectedPopup() const {
    if (selectedRow_ < 0 || static_cast<std::size_t>(selectedRow_) >= order_.size()) return kNoId;
    return order_[static_cast<std::size_t>(selectedRow_)];
}

void HmiSymbolPopupsPane::selectPopup(Id id) {
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == id && id) {
            hmiSelectModelRow(*table_, i);
            selectedRow_ = static_cast<int>(i);
            rebuildInfo();
            return;
        }
}

void HmiSymbolPopupsPane::refresh() {
    const Id keep = selectedPopup();
    const auto* sym = doc_->project.view(symbol_);
    order_ = popups();
    std::vector<std::vector<std::string>> rows;
    for (const Id id : order_) {
        const auto* v = doc_->project.view(id);
        if (!v) continue;
        std::string own;
        for (const auto& prm : v->params) own += (own.empty() ? "" : ", ") + prm.name;
        const std::size_t n = sym ? openersOf(*sym, v->name) : 0;
        rows.push_back({v->name, std::to_string(v->width) + " \xC3\x97 " + std::to_string(v->height), own.empty() ? std::string("-") : own,
                        n ? std::to_string(n) + " action(s) du symbole" : std::string("-")});
    }
    model_ = std::make_shared<Rows>(std::vector<std::string>{"Nom", "Taille", "Param\xC3\xA8tres propres", "Ouverte par"}, std::move(rows),
                                    [](ui::RowIndex, std::size_t c) {
                                        ui::CellStyle s;
                                        if (c == 0) { s.icon = ui::Icon::Screen; s.iconTone = ui::Tone::Family2; }
                                        return s;
                                    });
    table_->setModel(model_);
    selectedRow_ = -1;
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == keep && keep) { hmiSelectModelRow(*table_, i); selectedRow_ = static_cast<int>(i); }
    if (selectedRow_ < 0 && !order_.empty()) { hmiSelectModelRow(*table_, 0); selectedRow_ = 0; }
    rebuildInfo();
    if (sym)
        status_->setMessage(std::to_string(order_.size()) + " popup(s) de " + sym->name + "  \xC2\xB7  dans le symbole : Ouvrir une popup, "
                            "cible Pop_...  \xC2\xB7  dans la vue : Instance.Pop_...  \xC2\xB7  depuis un script : IHM_POPUP('Vue.Instance.Pop_...')");
    invalidate();
}

void HmiSymbolPopupsPane::rebuildInfo() {
    using PG = ui::PropertyGrid;
    const auto* sym = doc_->project.view(symbol_);
    const auto* pop = doc_->project.view(selectedPopup());
    if (!sym) { info_->setCategories({}); return; }
    PG::Category params;
    params.name = "Param\xC3\xA8tres du symbole (" + std::to_string(sym->params.size()) + ")";
    for (const auto& prm : sym->params)
        params.properties.push_back(hmikit::prop(prm.name, (prm.type.empty() ? std::string("ANY") : prm.type) + "  \xC2\xB7  li\xC3\xA9 \xC3\xA0 l'instance qui l'ouvre",
                                                 PG::ValueType::ReadOnly));
    if (sym->params.empty()) params.properties.push_back(hmikit::prop("Aucun", "le symbole ne d\xC3\xA9" "clare rien", PG::ValueType::ReadOnly));
    PG::Category fns;
    fns.name = "Fonctions de l'instance (" + std::to_string(sym->functions.size()) + ")";
    for (const auto& f : sym->functions)
        fns.properties.push_back(hmikit::prop(f.name + "()", (f.returnType.empty() ? std::string("sans retour") : f.returnType)
                                                                 + (f.isVirtual ? "  \xC2\xB7  virtuelle" : ""),
                                              PG::ValueType::ReadOnly));
    std::vector<PG::Category> cats{std::move(params), std::move(fns)};
    if (pop) {
        PG::Category own;
        own.name = "Ses param\xC3\xA8tres \xC3\xA0 elle (" + std::to_string(pop->params.size()) + ")";
        for (const auto& prm : pop->params) own.properties.push_back(hmikit::prop(prm.name, prm.type.empty() ? std::string("ANY") : prm.type, PG::ValueType::ReadOnly));
        if (pop->params.empty()) own.properties.push_back(hmikit::prop("Aucun", "ceux du symbole suffisent", PG::ValueType::ReadOnly));
        cats.push_back(std::move(own));
    }
    info_->setCategories(std::move(cats));
}

Id HmiSymbolPopupsPane::addPopup(std::string name, std::string* why) {
    const auto* sym = doc_->project.view(symbol_);
    if (!sym || !hmi::isSymbolView(*sym)) {
        if (why) *why = "symbole introuvable";
        return kNoId;
    }
    const std::string symName = sym->name;   // copie : la commande remplace les vues
    if (name.empty()) name = "Pop_" + symName;
    if (!hmi::isIdentifier(name)) {
        if (why) *why = "nom invalide : " + name;
        say("Popup refus\xC3\xA9" "e : nom invalide " + name, true);
        return kNoId;
    }
    Id made = kNoId;
    const Id owner = symbol_;
    auto cmd = hmi::changeProject(doc_, "Nouvelle popup de " + symName, [&](hmi::Project& p) {
        auto v = hmi::makeView(p, hmi::uniqueViewName(p, name));
        v.role = "popup";
        v.width = 420;
        v.height = 260;
        v.ownerSymbol = owner;
        v.popup.title = v.name;
        made = v.id;
        p.views.push_back(std::move(v));
    });
    if (cmd) apply_(std::move(cmd));
    if (made) {
        refresh();
        selectPopup(made);
        say("Popup " + doc_->project.view(made)->name + " cr\xC3\xA9\xC3\xA9" "e dans " + symName
            + " : ses objets lisent les param\xC3\xA8tres du symbole. Ctrl+Z la retire.");
    }
    return made;
}

bool HmiSymbolPopupsPane::deletePopup(Id id) {
    const auto* v = doc_->project.view(id);
    if (!v || v->ownerSymbol != symbol_) return false;
    const std::string name = v->name;
    auto cmd = hmi::changeProject(doc_, "Supprimer la popup " + name, [&](hmi::Project& p) {
        std::erase_if(p.views, [&](const hmi::View& x) { return x.id == id; });
    });
    if (cmd) apply_(std::move(cmd));
    say("Popup " + name + " supprim\xC3\xA9" "e. Ctrl+Z la rend.");
    return true;
}

void HmiSymbolPopupsPane::say(std::string text, bool warning) {
    message_ = text;
    status_->setTransientMessage(std::move(text), 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

void HmiSymbolPopupsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    split_->setBounds({b.x, b.y + 38, b.w, std::max(0.f, b.h - 62)});
}

void HmiSymbolPopupsPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
