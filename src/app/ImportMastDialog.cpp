// =============================================================================
//  app/ImportMastDialog.cpp - lot 7 : le recapitulatif d'un nouveau MAST
//  (voir l'en-tete).
// =============================================================================
#include "ImportMastDialog.hpp"

#include "../menu/MenuManager.hpp"
#include "../ui/widgets/Containers.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

namespace app {

namespace mast = project::mast;

namespace {

constexpr float kTitle = 36.f;       // la barre de titre du panneau
constexpr float kBanner = 100.f;     // le bandeau rouge : un titre, trois lignes
constexpr float kFooter = 104.f;     // la case, sa note, les boutons
const gfx::FontId kSmall{13};

ui::Icon groupIcon(mast::Group g) {
    switch (g) {
        case mast::Group::Variables: return ui::Icon::Variable;
        case mast::Group::Instances: return ui::Icon::FunctionBlock;
        case mast::Group::Types:     return ui::Icon::DerivedType;
        case mast::Group::Program:   return ui::Icon::Section;
        case mast::Group::Tables:    return ui::Icon::AnimationTable;
        case mast::Group::Hmi:       return ui::Icon::Screen;
        case mast::Group::Hardware:  return ui::Icon::Rack;
    }
    return ui::Icon::None;
}

ui::Tone statusTone(mast::Status s) {
    switch (s) {
        case mast::Status::Kept:     return ui::Tone::Ok;
        case mast::Status::Changed:  return ui::Tone::Warning;
        case mast::Status::Removed:  return ui::Tone::Error;
        case mast::Status::Added:    return ui::Tone::Accent;
        case mast::Status::LinkLost: return ui::Tone::Error;
    }
    return ui::Tone::None;
}

// "Supprime" et "Supprim\xC3\xA9", "LIENS" et "Liens" : le meme onglet (les
// scripts s'ecrivent vite, sans accents).
std::string folded(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const int d = static_cast<unsigned char>(s[i + 1]);
            const auto in = [d](int lo, int hi) { return d >= lo && d <= hi; };
            char base = 0;
            if (in(0xA0, 0xA5) || in(0x80, 0x85)) base = 'a';
            else if (d == 0xA7 || d == 0x87) base = 'c';
            else if (in(0xA8, 0xAB) || in(0x88, 0x8B)) base = 'e';
            else if (in(0xAC, 0xAF) || in(0x8C, 0x8F)) base = 'i';
            else if (in(0xB2, 0xB6) || in(0x92, 0x96)) base = 'o';
            else if (in(0xB9, 0xBC) || in(0x99, 0x9C)) base = 'u';
            if (base != 0) {
                out += base;
                ++i;
                continue;
            }
        }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

// Un texte coupe en lignes a la largeur (les \n comptent).
std::vector<std::string> wrap(const gfx::IRenderer& r, const std::string& text, gfx::FontId font, float width) {
    std::vector<std::string> out;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && r.measure(trial, font).width > width) {
            out.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ') flush();
        else if (c == '\n') {
            flush();
            out.push_back(line);
            line.clear();
        } else {
            word += c;
        }
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

// Une ligne qui ne tient pas finit par "...".
std::string fitted(const gfx::IRenderer& r, const std::string& text, gfx::FontId font, float width) {
    if (r.measure(text, font).width <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    const auto n = r.fitCharacters(text, font, std::max(0.f, width - r.measure(dots, font).width));
    std::size_t cut = std::min(n, text.size());
    // Ne pas couper un caractere UTF-8 en deux.
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0u) == 0x80u) --cut;
    return text.substr(0, cut) + dots;
}

// ------------------------------------------------------------ l'arbre d'un onglet --
//  Les lignes du plan d'UN statut : groupe (Variables globales, Instances...),
//  puis sous-groupe (un genre, un type, une table, une vue), puis les lignes.
//  Petit et fixe : les noeuds sont faits une fois, numerotes a partir de 1.
class PlanTreeModel final : public ui::ITreeModel {
public:
    PlanTreeModel(const mast::Plan& plan, mast::Status status) {
        nodes_.push_back(Node{});                          // la racine (1), cachee
        for (std::size_t g = 0; g < mast::kGroupCount; ++g) {
            const auto group = static_cast<mast::Group>(g);
            ui::NodeId groupNode = ui::kInvalidNode;
            std::map<std::string, ui::NodeId> subs;
            std::size_t count = 0;
            for (const auto& it : plan.items) {
                if (it.status != status || it.group != group) continue;
                if (groupNode == ui::kInvalidNode) {
                    Node n;
                    n.text = mast::groupLabel(group);
                    n.style.bold = true;
                    n.style.icon = groupIcon(group);
                    n.style.iconTone = statusTone(status);
                    groupNode = add(1, std::move(n));
                }
                ui::NodeId parent = groupNode;
                if (!it.sub.empty()) {
                    auto& s = subs[it.sub];
                    if (s == ui::kInvalidNode) {
                        Node n;
                        n.text = it.sub;
                        n.style.icon = ui::Icon::Folder;
                        n.style.iconTone = ui::Tone::Muted;
                        s = add(groupNode, std::move(n));
                    }
                    parent = s;
                }
                Node n;
                n.text = it.name + (it.detail.empty() ? std::string{} : "   \xE2\x80\x94   " + it.detail);
                n.style.icon = groupIcon(group);
                n.style.iconTone = statusTone(status);
                (void)add(parent, std::move(n));
                ++count;
            }
            if (groupNode == ui::kInvalidNode) continue;
            nodes_[groupNode - 1].style.badge = std::to_string(count);
            for (const auto& [name, id] : subs) nodes_[id - 1].style.badge = std::to_string(nodes_[id - 1].kids.size());
        }
        if (nodes_.front().kids.empty()) {
            Node n;
            n.text = status == mast::Status::LinkLost ? "Aucun lien de l'IHM ne se perd."
                                                      : "Rien dans cet onglet.";
            n.style.fgTone = ui::Tone::Muted;
            n.style.icon = ui::Icon::Info;
            n.style.iconTone = ui::Tone::Muted;
            (void)add(1, std::move(n));
        }
    }
    [[nodiscard]] ui::NodeId root() const override { return 1; }
    [[nodiscard]] std::size_t childCount(ui::NodeId n) const override {
        const auto* x = node(n);
        return x ? x->kids.size() : 0;
    }
    [[nodiscard]] ui::NodeId childAt(ui::NodeId parent, std::size_t i) const override {
        const auto* x = node(parent);
        return x && i < x->kids.size() ? x->kids[i] : ui::kInvalidNode;
    }
    [[nodiscard]] bool hasChildren(ui::NodeId n) const override { return childCount(n) > 0; }
    [[nodiscard]] std::string text(ui::NodeId n) const override {
        const auto* x = node(n);
        return x ? x->text : std::string{};
    }
    [[nodiscard]] ui::CellStyle style(ui::NodeId n) const override {
        const auto* x = node(n);
        return x ? x->style : ui::CellStyle{};
    }

private:
    struct Node {
        std::string             text;
        ui::CellStyle           style;
        std::vector<ui::NodeId> kids;
    };
    [[nodiscard]] const Node* node(ui::NodeId n) const {
        return n >= 1 && n <= nodes_.size() ? &nodes_[static_cast<std::size_t>(n - 1)] : nullptr;
    }
    ui::NodeId add(ui::NodeId parent, Node n) {
        nodes_.push_back(std::move(n));
        const auto id = static_cast<ui::NodeId>(nodes_.size());
        nodes_[static_cast<std::size_t>(parent - 1)].kids.push_back(id);
        return id;
    }
    std::vector<Node> nodes_;
};

} // namespace

// ------------------------------------------------------------------- le corps --
//  Le panneau, peint a la main ; les onglets, la case et les boutons sont ses
//  enfants, places ici.
class MastRecapBody final : public ui::Widget {
public:
    explicit MastRecapBody(ImportMastDialog& owner) : ui::Widget("import.mast.body"), d_(owner) {}

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(960.f, r.w - 40.f), h = std::min(640.f, r.h - 40.f);
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float x = panel_.x + 16.f, inner = panel_.w - 32.f;
        float y = panel_.y + kTitle + 10.f;
        bannerY_ = y;
        y += kBanner + 8.f;
        versionY_ = y;
        y += 22.f;
        contentsY_ = y;
        y += 24.f;
        if (d_.tabs_) d_.tabs_->setBounds({x, y, inner, std::max(80.f, panel_.bottom() - kFooter - y)});
        const float fy = panel_.bottom() - kFooter + 10.f;
        if (d_.keepBox_)
            d_.keepBox_->setBounds({x, fy, std::min(inner, ui::measureWidth(d_.keepBox_->label(), gfx::FontId{16}) + 44.f), 26.f});
        noteY_ = fy + 30.f;
        // Les boutons en bas a droite, le principal (qui detruit) au bout.
        float bx = panel_.right() - 16.f;
        for (ui::Button* b : {d_.ok_, d_.cancel_}) {
            if (!b) continue;
            const float bw = std::max(110.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            b->setBounds({bx, panel_.bottom() - 44.f, bw, 30.f});
            bx -= 10.f;
        }
        noteRight_ = bx - 10.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const auto& f = ctx.theme.font;
        ctx.r.fillRect(panel_, c.panelBg);
        ctx.r.strokeRect(panel_, c.borderStrong, 1.f);
        ctx.r.fillRect({panel_.x, panel_.y, panel_.w, kTitle}, c.headerBg);
        ctx.r.drawText({panel_.x + 14.f, panel_.y + (kTitle - ctx.r.lineHeight(f.uiBold)) * 0.5f},
                       fitted(ctx.r, d_.title(), f.uiBold, panel_.w - 28.f), f.uiBold, c.text);

        // LE BANDEAU ROUGE : ce qui est detruit, et ce qui reste.
        const gfx::Rect band{panel_.x + 16.f, bannerY_, panel_.w - 32.f, kBanner};
        ctx.r.fillRect(band, c.error.withAlpha(ctx.theme.isDark() ? 60 : 34));
        ctx.r.fillRect({band.x, band.y, 4.f, band.h}, c.error);
        float ty = band.y + 8.f;
        ctx.r.drawText({band.x + 14.f, ty},
                       fitted(ctx.r, "Le programme c\xC3\xB4t\xC3\xA9 API est remplac\xC3\xA9 par celui de " + d_.spec_.fileName,
                              f.uiBold, band.w - 28.f),
                       f.uiBold, c.error);
        ty += ctx.r.lineHeight(f.uiBold) + 5.f;
        const auto& p = d_.plan();
        const std::size_t lost = p.count(mast::Status::LinkLost);
        // Ce qui est detruit, puis (a la ligne) ce qui reste ; trois lignes au plus.
        std::string said = "Tout le projet c\xC3\xB4t\xC3\xA9 API (t\xC3\xA2" "ches, sections, unit\xC3\xA9s, DDT, DFB, variables) est d\xC3\xA9truit : "
                           "les liens de l'IHM vers les variables qui disparaissent sont perdus ("
                           + std::to_string(lost) + (lost > 1 ? " liens).\n" : " lien).\n");
        said += d_.keep_ ? "Restent : l'IHM, les versions, le mat\xC3\xA9riel, et les variables identiques avec leurs liens."
                         : "Restent : l'IHM, les versions et le mat\xC3\xA9riel ; tout le reste vient du .XPG, tables d'animation comprises.";
        const auto lines = wrap(ctx.r, said, f.ui, band.w - 28.f);
        for (std::size_t i = 0; i < lines.size() && i < 3; ++i) {
            std::string shown = lines[i];
            if (i == 2)
                for (std::size_t k = 3; k < lines.size(); ++k) shown += " " + lines[k];
            ctx.r.drawText({band.x + 14.f, ty}, fitted(ctx.r, shown, f.ui, band.w - 28.f), f.ui, c.text);
            ty += ctx.r.lineHeight(f.ui) + 2.f;
        }

        // La version d'avant, puis le .XPG en chiffres.
        ctx.r.drawText({panel_.x + 16.f, versionY_}, fitted(ctx.r, d_.spec_.versionLine, f.ui, panel_.w - 32.f), f.ui, c.text);
        std::string contents = "Le .XPG : " + std::to_string(p.variablesAfter) + " variables globales (le projet : "
                               + std::to_string(p.variablesBefore) + "), " + std::to_string(p.sectionsAfter) + " sections (le projet : "
                               + std::to_string(p.sectionsBefore) + "), " + std::to_string(p.unitsAfter) + " unit\xC3\xA9s, "
                               + std::to_string(p.ddtAfter) + " DDT, " + std::to_string(p.dfbAfter) + " DFB, "
                               + std::to_string(p.tasksAfter) + " t\xC3\xA2" "ches" + (p.hardwareReplaced ? ", et les racks du .XHW." : ".");
        ctx.r.drawText({panel_.x + 16.f, contentsY_}, fitted(ctx.r, contents, kSmall, panel_.w - 32.f), kSmall, c.textMuted);

        // Ce que veut dire la case.
        const std::string note = d_.keep_
            ? "M\xC3\xAAme nom, m\xC3\xAAme type, m\xC3\xAAme adresse : la variable garde sa d\xC3\xA9" "claration du projet (commentaire, valeur initiale) "
              "et ses lignes dans les tables d'animation. Les liens de l'IHM suivent les noms."
            : "D\xC3\xA9" "coch\xC3\xA9" "e : tout vient du .XPG (d\xC3\xA9" "clarations, commentaires, tables d'animation). L'IHM retrouve encore "
              "les variables dont le nom est dans le .XPG.";
        float ny = noteY_;
        for (const auto& l : wrap(ctx.r, note, kSmall, std::max(120.f, noteRight_ - panel_.x - 16.f))) {
            if (ny + ctx.r.lineHeight(kSmall) > panel_.bottom() - 8.f) break;
            ctx.r.drawText({panel_.x + 16.f, ny}, l, kSmall, c.textMuted);
            ny += ctx.r.lineHeight(kSmall) + 2.f;
        }
    }

private:
    ImportMastDialog& d_;
    gfx::Rect         panel_{};
    float             bannerY_{0.f}, versionY_{0.f}, contentsY_{0.f}, noteY_{0.f}, noteRight_{0.f};
};

// ------------------------------------------------------------------ le dialogue --
ImportMastDialog::ImportMastDialog(Spec spec)
    : menu::WidgetMenu("dialog.importMast"), spec_(std::move(spec)), keep_(spec_.keepIdentical) {}

menu::MenuTraits ImportMastDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    t.closableWithEscape = true;
    return t;
}

std::string ImportMastDialog::title() const { return "Importer " + spec_.fileName + " (nouveau MAST)"; }

core::Status ImportMastDialog::buildUi() {
    auto body = std::make_unique<MastRecapBody>(*this);
    auto* b = body.get();
    auto tabs = std::make_unique<ui::TabControl>("import.mast.tabs");
    for (std::size_t i = 0; i < mast::kStatusCount; ++i) {
        auto tree = std::make_unique<ui::TreeView>("import.mast.tree" + std::to_string(i));
        tree->setShowRootNode(false);
        tree->setSelectionMode(ui::SelectionMode::Single);
        trees_[i] = tree.get();
        ui::TabControl::Tab meta;
        meta.title = mast::statusLabel(static_cast<mast::Status>(i));
        (void)tabs->addTab(std::move(meta), std::move(tree));
    }
    tabs_ = &static_cast<ui::TabControl&>(b->addChild(std::move(tabs)));
    auto box = std::make_unique<ui::Checkbox>("Garder les variables identiques et leurs liens (recommand\xC3\xA9)", "import.mast.keep");
    box->setState(keep_ ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    keepBox_ = &static_cast<ui::Checkbox&>(b->addChild(std::move(box)));
    links_ += keepBox_->stateChanged->connect([this](ui::Checkbox::State s) {
        const bool on = s == ui::Checkbox::State::Checked;
        if (on == keep_) return;
        keep_ = on;
        refreshTrees();
    });
    cancel_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Annuler", "import.mast.cancel")));
    links_ += cancel_->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Importer et remplacer", "import.mast.ok")));
    ok_->setStyle(ui::Button::Style::Danger);
    links_ += ok_->clicked->connect([this] { finish(true); });
    setRoot(std::move(body));
    refreshTrees();
    // L'onglet qui compte le plus s'ouvre d'abord : les liens perdus, puis ce
    // qui part, ce qui change, ce qui arrive ; tout garde sinon.
    const auto& p = plan();
    for (const auto s : {mast::Status::LinkLost, mast::Status::Removed, mast::Status::Changed, mast::Status::Added})
        if (p.count(s) > 0) {
            tabs_->setCurrentIndex(static_cast<std::size_t>(s));
            break;
        }
    return core::ok();
}

void ImportMastDialog::refreshTrees() {
    const auto& p = plan();
    for (std::size_t i = 0; i < mast::kStatusCount; ++i) {
        const auto s = static_cast<mast::Status>(i);
        if (auto* tree = trees_[i]) {
            tree->setModel(std::make_shared<PlanTreeModel>(p, s));
            // Garde : les groupes ouverts, leurs sous-groupes fermes (il y en a
            // des centaines) ; les autres onglets : tout ouvert.
            tree->expandToDepth(s == mast::Status::Kept ? 1 : 2);
        }
        if (tabs_) {
            const auto n = p.count(s);
            tabs_->setTabBadge(i, std::to_string(n), n == 0 ? ui::Tone::Muted : statusTone(s));
        }
    }
    if (tabs_) root().invalidate();       // le bandeau et la note suivent la case
}

bool ImportMastDialog::keepFrom(const std::string& payload) { return payload.find("garder=0") == std::string::npos; }

bool ImportMastDialog::selectTab(const std::string& titlePrefix) {
    if (!tabs_) return false;
    const auto want = folded(titlePrefix);
    for (std::size_t i = 0; i < tabs_->tabCount(); ++i) {
        const auto* t = tabs_->tab(i);
        if (t && folded(t->title).rfind(want, 0) == 0) {
            tabs_->setCurrentIndex(i);
            return true;
        }
    }
    return false;
}

std::size_t ImportMastDialog::currentTab() const { return tabs_ ? tabs_->currentIndex() : 0; }

std::string ImportMastDialog::currentTabTitle() const {
    const auto* t = tabs_ ? tabs_->tab(tabs_->currentIndex()) : nullptr;
    return t ? t->title : std::string{};
}

void ImportMastDialog::setKeepIdentical(bool on) {
    if (keepBox_) keepBox_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    else keep_ = on;
}

void ImportMastDialog::confirm() { finish(true); }
void ImportMastDialog::cancel() { finish(false); }

void ImportMastDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             keep_ ? "garder=1" : "garder=0"});
}

ui::EventResult ImportMastDialog::HandleEvent(const ui::InputEvent& ev) {
    // Echap : Annuler, quel que soit ce qui a le focus. Entree ne valide pas :
    // "Importer et remplacer" detruit le programme, il se clique.
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        finish(false);
        return ui::EventResult::Consumed;
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app
