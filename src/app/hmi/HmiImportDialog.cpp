// =============================================================================
//  app/hmi/HmiImportDialog.cpp - Importer (lot 20 ; 1.11.2 : la scene 5 de la
//  maquette de MQ5, tout paquet) : voir l'en-tete.
// =============================================================================
#include "HmiImportDialog.hpp"

#include "../../menu/MenuManager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace app {

namespace {

const gfx::FontId kSmall{13};
constexpr float kRow = 26.f;        // une ligne du tableau
constexpr float kConflict = 30.f;   // une ligne de conflit (ses choix)
constexpr float kTitle = 28.f;      // un titre de section

using hmi::pkg::Choice;
using hmi::pkg::ItemKind;
using hmi::pkg::State;

bool feminine(const hmi::pkg::Item& it) {
    return it.kind == ItemKind::View || it.kind == ItemKind::Popup || it.kind == ItemKind::Resource || it.kind == ItemKind::Function
        || it.kind == ItemKind::Variable;
}

bool isImage(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "gif" || ext == "svg";
}

// La colonne Sorte : "symbole", "image", "type IHM"...
std::string sortText(const hmi::pkg::Item& it) {
    if (it.kind == ItemKind::Resource) return isImage(it.name) ? "image" : "fichier";
    return std::string(hmi::pkg::itemKindLabel(it.kind));
}

std::string choiceText(const hmi::pkg::Item& it, Choice c) {
    switch (c) {
        case Choice::Rename: return "Renommer en " + (it.newName.empty() ? it.name : it.newName);
        case Choice::Replace: return feminine(it) ? "Remplacer celle du projet" : "Remplacer celui du projet";
        case Choice::KeepOurs: return feminine(it) ? "Garder celle du projet" : "Garder celui du projet";
        case Choice::Add: return "Ajouter";
        case Choice::Skip: return "Ne pas cr\xC3\xA9" "er";
    }
    return {};
}

// La colonne Dans ce projet.
std::string stateText(const hmi::pkg::Item& it) {
    switch (it.state) {
        case State::Missing: return "nouveau";
        case State::Same: return feminine(it) ? "identique : gard\xC3\xA9" "e" : "identique : gard\xC3\xA9";
        case State::Different: return "existe d\xC3\xA9j\xC3\xA0 : \xC3\xA0 trancher plus bas";
    }
    return {};
}

} // namespace

// Le corps : le fichier, le tableau, les conflits, les variables - places a la main.
class ImportBody final : public ui::Widget {
public:
    explicit ImportBody(HmiImportDialog& owner) : ui::Widget("import.body"), d_(owner) {}
    gfx::Rect panel{}, list{};
    float scroll{0.f}, contentH{0.f};
    std::vector<ui::Button*> buttons;
    ui::Button* browse{nullptr};                          // 1.11.2 : Parcourir... (a droite du fichier)
    std::vector<std::vector<ui::RadioButton*>> radios;   // par element du plan

    enum class LineKind { Head, Row, Section, Conflict, Variables };
    struct Line { LineKind kind{LineKind::Row}; int item{-1}; std::string text; float y{0}; };
    std::vector<Line> lines;

    void build() {
        lines.clear();
        const auto& items = d_.spec_->plan.items;
        lines.push_back({LineKind::Head, -1, {}, 0});
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].kind != ItemKind::Variable) lines.push_back({LineKind::Row, static_cast<int>(i), items[i].name, 0});
        std::size_t conflicts = 0;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (d_.groups_[i]) ++conflicts;
        if (conflicts > 0) {
            lines.push_back({LineKind::Section, -1,
                             std::to_string(conflicts) + (conflicts == 1 ? " NOM EN CONFLIT" : " NOMS EN CONFLIT"), 0});
            for (std::size_t i = 0; i < items.size(); ++i)
                if (d_.groups_[i]) lines.push_back({LineKind::Conflict, static_cast<int>(i), items[i].name, 0});
        }
        std::size_t missing = 0;
        std::string names;
        for (const auto& it : items)
            if (it.kind == ItemKind::Variable && it.state == State::Missing) {
                if (missing < 6) names += (missing ? ", " : "") + it.name;
                ++missing;
            }
        if (missing > 0) {
            lines.push_back({LineKind::Section, -1, "VARIABLES ABSENTES : " + std::to_string(missing), 0});
            lines.push_back({LineKind::Variables, -2, names + (missing > 6 ? ", \xE2\x80\xA6" : ""), 0});
        }
    }

    static float heightOf(LineKind k) {
        switch (k) {
            case LineKind::Head: return 24.f;
            case LineKind::Row: return kRow;
            case LineKind::Section: return kTitle;
            case LineKind::Conflict: return kConflict;
            case LineKind::Variables: return kConflict;
        }
        return kRow;
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(900.f, r.w - 60.f), h = std::min(680.f, r.h - 60.f);
        panel = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        list = {panel.x + 14.f, panel.y + 36.f + 84.f, panel.w - 28.f, panel.h - 36.f - 84.f - 56.f};
        float y = list.y - scroll;
        for (auto& l : lines) {
            l.y = y;
            y += heightOf(l.kind);
        }
        contentH = y + scroll - list.y;
        const auto shownAt = [&](float at, float hh) { return at >= list.y - 1.f && at + hh <= list.bottom() + 1.f; };
        // Les choix d'un conflit : apres le nom (150) et la sorte (90), de gauche a droite.
        for (std::size_t i = 0; i < radios.size(); ++i) {
            float at = -1.f;
            for (const auto& l : lines)
                if (l.kind == LineKind::Conflict && l.item == static_cast<int>(i)) at = l.y;
            float x = list.x + 10.f + 160.f + 100.f;
            for (auto* rb : radios[i]) {
                const float bw = ui::measureWidth(rb->label(), gfx::FontId{14}) + 30.f;
                rb->setVisibility(shownAt(at, kConflict) ? ui::Visibility::Visible : ui::Visibility::Collapsed);
                rb->setBounds({x, at + 4.f, bw, kConflict - 8.f});
                x += bw + 14.f;
            }
        }
        if (d_.variables_) {
            float at = -1.f;
            for (const auto& l : lines)
                if (l.kind == LineKind::Variables) at = l.y;
            const float cx = list.x + list.w * 0.52f;
            d_.variables_->setVisibility(shownAt(at, kConflict) ? ui::Visibility::Visible : ui::Visibility::Collapsed);
            d_.variables_->setBounds({cx, at + 2.f, list.right() - cx, kConflict - 4.f});
        }
        if (browse) {
            const float bw = ui::measureWidth(browse->text(), gfx::FontId{16}) + 28.f;
            browse->setBounds({panel.right() - 14.f - bw, panel.y + 36.f + 12.f, bw, 24.f});
        }
        float bx = panel.right() - 14.f;
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
            const float bw = std::max(96.f, ui::measureWidth((*it)->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            (*it)->setBounds({bx, panel.bottom() - 42.f, bw, 28.f});
            bx -= 8.f;
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel, c.panelBg);
        ctx.r.strokeRect(panel, c.borderStrong, 1.f);
        const float titleH = 36.f;
        ctx.r.fillRect({panel.x, panel.y, panel.w, titleH}, c.headerBg);
        ctx.r.drawText({panel.x + 14.f, panel.y + (titleH - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, d_.title(), ctx.theme.font.uiBold, c.text);
        const auto& m = d_.spec_->manifest;
        // Fichier : son nom.
        float y = panel.y + titleH + 12.f;
        ctx.r.drawText({panel.x + 14.f, y + 3.f}, "Fichier", ctx.theme.font.ui, c.textMuted);
        const float browseW = browse ? browse->bounds().w + 8.f : 0.f;
        const gfx::Rect field{panel.x + 80.f, y, panel.w - 94.f - browseW, 24.f};
        ctx.r.fillRect(field, c.inputBg);
        ctx.r.strokeRect(field, c.border, 1.f);
        const bool noFile = d_.spec_->fileName.empty();
        ctx.r.drawText({field.x + 6.f, y + (24.f - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f},
                       noFile ? std::string("aucun fichier choisi") : d_.spec_->fileName, ctx.theme.font.ui, noFile ? c.textMuted : c.text);
        // Ce qu'il emporte, et d'ou il vient : "fait par la 1.11.2 (format 2) . exporte depuis Armoire_Gaz".
        y += 34.f;
        const gfx::Rect band{panel.x + 14.f, y, panel.w - 28.f, 30.f};
        ctx.r.fillRect(band, c.rowAltBg);
        ctx.r.fillRect({band.x, band.y, 3.f, band.h}, c.accent);
        const float ty = y + (30.f - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f;
        std::string from;
        if (!m.writer.empty()) from = "fait par la " + m.writer + " (format " + std::to_string(m.format) + ")";
        if (!m.fromProject.empty()) from += (from.empty() ? "" : " \xC2\xB7 ") + std::string("export\xC3\xA9 depuis ") + m.fromProject;
        const float fromW = from.empty() ? 0.f : ctx.r.measure(from, kSmall).width;
        std::string what = d_.spec_->contents;
        // 1.11.2 (decision 188) : rien a importer - pourquoi (illisible, plus recent, un modele), ou quoi faire.
        if (!d_.spec_->error.empty()) {
            from.clear();
            what = d_.spec_->error;
        } else if (!d_.ready()) {
            what = "Choisis un fichier : Parcourir\xE2\x80\xA6, ou l\xC3\xA2" "che un paquet sur cette fen\xC3\xAAtre.";
        }
        const float fromW2 = from.empty() ? 0.f : fromW;
        while (what.size() > 4 && ctx.r.measure(what, ctx.theme.font.uiBold).width > band.w - fromW2 - 40.f)
            what = what.substr(0, what.size() - 4) + "\xE2\x80\xA6";
        ctx.r.drawText({band.x + 12.f, ty}, what, ctx.theme.font.uiBold,
                       !d_.spec_->error.empty() ? c.warning : d_.ready() ? c.text : c.textMuted);
        if (!from.empty()) ctx.r.drawText({band.right() - 10.f - fromW, ty + 1.f}, from, kSmall, c.textMuted);
        // Le tableau, les conflits, les variables.
        ctx.r.pushClip(list);
        const auto& p = d_.spec_->plan;
        const float xName = list.x + 30.f, xSort = list.x + 30.f + 220.f, xState = xSort + 120.f;
        std::size_t row = 0;
        for (const auto& l : lines) {
            const float hh = heightOf(l.kind);
            if (l.y + hh < list.y || l.y > list.bottom()) continue;
            const float ly = l.y + (hh - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f;
            switch (l.kind) {
                case LineKind::Head:
                    ctx.r.fillRect({list.x, l.y, list.w, hh}, c.headerBg);
                    ctx.r.drawText({xName, ly}, "Nom", kSmall, c.textMuted);
                    ctx.r.drawText({xSort, ly}, "Sorte", kSmall, c.textMuted);
                    ctx.r.drawText({xState, ly}, "Dans ce projet", kSmall, c.textMuted);
                    break;
                case LineKind::Row: {
                    const auto& it = p.items[static_cast<std::size_t>(l.item)];
                    if (row++ % 2 == 1) ctx.r.fillRect({list.x, l.y, list.w, hh}, c.rowAltBg);
                    std::string label = l.text;
                    while (label.size() > 4 && ctx.r.measure(label, ctx.theme.font.ui).width > xSort - xName - 10.f)
                        label = label.substr(0, label.size() - 4) + "\xE2\x80\xA6";
                    ctx.r.drawText({xName, ly}, label, ctx.theme.font.ui, c.text);
                    ctx.r.drawText({xSort, ly}, sortText(it), ctx.theme.font.ui, c.textMuted);
                    const gfx::Color col = it.state == State::Missing ? c.ok : it.state == State::Same ? c.textMuted : c.warning;
                    ctx.r.drawText({xState, ly}, stateText(it), ctx.theme.font.ui, col);
                    break;
                }
                case LineKind::Section:
                    ctx.r.drawText({list.x + 4.f, l.y + 9.f}, l.text, ctx.theme.font.uiBold, c.text);
                    if (l.text.find("CONFLIT") != std::string::npos)
                        ctx.r.drawText({list.x + 14.f + ctx.r.measure(l.text, ctx.theme.font.uiBold).width, l.y + 10.f},
                                       "\xC2\xB7 choisis pour chacun", kSmall, c.textMuted);
                    break;
                case LineKind::Conflict: {
                    const auto& it = p.items[static_cast<std::size_t>(l.item)];
                    std::string label = l.text;
                    while (label.size() > 4 && ctx.r.measure(label, ctx.theme.font.uiBold).width > 150.f)
                        label = label.substr(0, label.size() - 4) + "\xE2\x80\xA6";
                    ctx.r.drawText({list.x + 10.f, ly}, label, ctx.theme.font.uiBold, c.text);
                    ctx.r.drawText({list.x + 10.f + 160.f, ly}, sortText(it), ctx.theme.font.ui, c.textMuted);
                    break;
                }
                case LineKind::Variables:
                    ctx.r.drawText({list.x + 10.f, ly}, l.text, ctx.theme.font.ui, c.text);
                    break;
            }
        }
        ctx.r.popClip();
        ctx.r.drawText({panel.x + 14.f, panel.bottom() - 36.f},
                       "Ctrl+Z annule l'import entier. Un fichier d'une version plus r\xC3\xA9" "cente est refus\xC3\xA9, en le disant.", kSmall,
                       c.textMuted);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* wv = std::get_if<ui::MouseWheel>(&ev); wv && list.contains(wv->pos)) {
            scroll = std::clamp(scroll - wv->dy * 60.f, 0.f, std::max(0.f, contentH - list.h));
            invalidateLayout();
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && panel.contains(d->pos)) return ui::EventResult::Consumed;
        return ui::EventResult::Ignored;
    }

private:
    HmiImportDialog& d_;
};

HmiImportDialog::HmiImportDialog(Spec spec, Loader loader)
    : menu::WidgetMenu("dialog.hmiImportViews"), spec_(std::make_shared<Spec>(std::move(spec))), loader_(std::move(loader)) {
    if (!spec_->browse.active())
        spec_->browse = ui::openFile("Paquets XpgAnalyzer|*.xpgvues;*.xpgsymboles;*.xpgtypes;*.xpgfonctions;*.xpgscripts", {}, "Importer");
}

menu::MenuTraits HmiImportDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status HmiImportDialog::buildUi() {
    setRoot(makeBody());
    return core::ok();
}

std::unique_ptr<ui::Widget> HmiImportDialog::makeBody() {
    links_.clear();
    variables_ = nullptr;
    ok_ = nullptr;
    auto body = std::make_unique<ImportBody>(*this);
    auto* b = body.get();
    const auto& items = spec_->plan.items;
    groups_.assign(items.size(), nullptr);
    b->radios.assign(items.size(), {});
    bool anyMissingVar = false;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        if (it.kind == ItemKind::Variable) {
            anyMissingVar = anyMissingVar || it.state == State::Missing;
            continue;
        }
        if (it.state != State::Different) continue;
        const auto options = it.choices();
        if (options.size() < 2) continue;
        auto group = std::make_shared<ui::RadioGroup>();
        for (const auto c : options) {
            auto rb = std::make_unique<ui::RadioButton>(choiceText(it, c), group, static_cast<int>(c),
                                                        "import.item" + std::to_string(i) + "." + std::to_string(static_cast<int>(c)));
            b->radios[i].push_back(&static_cast<ui::RadioButton&>(b->addChild(std::move(rb))));
        }
        group->setValue(static_cast<int>(it.choice));
        links_ += group->valueChanged->connect([this](int) {
            if (ok_) ok_->setText(confirmText());
        });
        groups_[i] = std::move(group);
    }
    if (anyMissingVar) {
        auto dd = std::make_unique<ui::DropDown>("import.variables");
        dd->setItems({{"les cr\xC3\xA9" "er (avec leur type)", "1", {}, true}, {"ne pas les cr\xC3\xA9" "er (G\xC3\xA9n\xC3\xA9rer les signalera)", "0", {}, true}});
        dd->setSelectedIndex(0);
        variables_ = &static_cast<ui::DropDown&>(b->addChild(std::move(dd)));
    }
    // 1.11.2 (decision 188 ; la scene 5) : Parcourir... dans la fenetre meme : un autre fichier la refait.
    if (loader_) {
        b->browse = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Parcourir\xE2\x80\xA6", "import.browse")));
        links_ += b->browse->clicked->connect([this] { browse(); });
    }
    auto* cancel = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Annuler", "import.cancel")));
    links_ += cancel->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>(confirmText(), "import.ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += ok_->clicked->connect([this] { finish(true); });
    ok_->setEnabled(ready());
    b->buttons = {cancel, ok_};
    b->build();
    return body;
}

// 1.11.2 : de quoi importer - un paquet lu (ou, lot 20, un plan donne par l'ecran) et rien qui l'empeche.
bool HmiImportDialog::ready() const { return spec_->error.empty() && (spec_->package || !spec_->plan.items.empty()); }

void HmiImportDialog::browse() {
    if (!loader_) return;
    std::weak_ptr<char> life = alive_;
    (void)ui::browsePath(spec_->browse, spec_->path, [this, life](std::string chosen) {
        if (life.expired() || done_ || chosen.empty()) return;
        pending_ = std::move(chosen);   // lu a l'image suivante (Update) : pas sous les pieds d'un bouton
    });
}

void HmiImportDialog::loadFile(const std::string& path) {
    if (!loader_ || done_) return;
    auto next = loader_(path);
    if (!next.browse.active()) next.browse = spec_->browse;
    *spec_ = std::move(next);            // le meme objet : state() le suit
    if (!widgetRoot()) return;           // pas encore construite : Initialize le fera
    setRoot(makeBody());
}

void HmiImportDialog::Update(const menu::FrameContext& fc) {
    if (!pending_.empty()) {
        const std::string path = std::move(pending_);
        pending_.clear();
        loadFile(path);
    }
    menu::WidgetMenu::Update(fc);
}

HmiImportDialog::Spec HmiImportDialog::load(const hmi::Project& target, const std::string& path) {
    Spec s;
    s.title = "Importer";
    s.path = path;
    if (path.empty()) return s;
    s.fileName = std::filesystem::path(path).filename().string();
    auto pkg = hmi::pkg::readFile(path);
    if (!pkg) {
        // Refuse, et dit pourquoi : un fichier d'une version plus recente (la version qui l'a ecrit), illisible...
        s.error = s.fileName + " : " + (hmi::pkg::tooNew(pkg.error()) && !pkg.error().context.empty() ? pkg.error().context : pkg.error().message());
        return s;
    }
    if (pkg->manifest.kind == "modele") {
        s.error = s.fileName + " est un mod\xC3\xA8le : Nouvelle vue \xE2\x80\xBA Mes mod\xC3\xA8les \xE2\x80\xBA Importer un mod\xC3\xA8le";
        return s;
    }
    s.manifest = pkg->manifest;
    // Les mots de la maquette de MQ5 (scene 5) : "Le fichier emporte : ... · fait par la 1.11.2".
    s.contents = "Le fichier emporte : " + hmi::pkg::contentsText(*pkg, true)
               + (pkg->manifest.writer.empty() ? std::string{} : " \xC2\xB7 fait par la " + pkg->manifest.writer);
    s.plan = hmi::pkg::plan(target, *pkg);
    s.title = hmi::pkg::importTitle(*pkg);
    s.package = std::make_shared<const hmi::pkg::Package>(std::move(*pkg));
    return s;
}

bool HmiImportDialog::isPackageFile(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (const auto e : hmi::pkg::kAllExtensions)
        if (ext == e) return true;
    return false;
}

std::vector<std::string> HmiImportDialog::takePackages(std::vector<std::string>& paths) {
    std::vector<std::string> packages, rest;
    for (auto& p : paths) (isPackageFile(p) ? packages : rest).push_back(std::move(p));
    paths = std::move(rest);
    return packages;
}

Choice HmiImportDialog::effective(std::size_t i) const {
    const auto& it = spec_->plan.items[i];
    Choice c = it.choice;
    if (i < groups_.size() && groups_[i] && groups_[i]->value() >= 0) c = static_cast<Choice>(groups_[i]->value());
    if (it.kind == ItemKind::Variable && it.state == State::Missing && variables_)
        c = variables_->selectedIndex() == 1 ? Choice::Skip : Choice::Add;
    return c;
}

// "Importer 8 elements" : ce qui sera ajoute, renomme ou remplace (un element
// identique, ou garde, ne compte pas).
std::string HmiImportDialog::confirmText() const {
    std::size_t n = 0;
    for (std::size_t i = 0; i < spec_->plan.items.size(); ++i) {
        const auto& it = spec_->plan.items[i];
        if (it.state == State::Same) continue;
        const Choice c = effective(i);
        if (c == Choice::Add || c == Choice::Rename || c == Choice::Replace) ++n;
    }
    if (n == 0) return spec_->confirm;
    return "Importer " + std::to_string(n) + (n == 1 ? " \xC3\xA9l\xC3\xA9ment" : " \xC3\xA9l\xC3\xA9ments");
}

bool HmiImportDialog::setChoice(const std::string& name, Choice choice) {
    for (std::size_t i = 0; i < spec_->plan.items.size(); ++i) {
        const auto& it = spec_->plan.items[i];
        if (it.name != name || !groups_[i]) continue;
        const auto options = it.choices();
        if (std::find(options.begin(), options.end(), choice) == options.end()) continue;
        groups_[i]->setValue(static_cast<int>(choice));
        if (ok_) ok_->setText(confirmText());
        return true;
    }
    return false;
}

void HmiImportDialog::setCreateVariables(bool on) {
    if (variables_) variables_->setSelectedIndex(on ? 0 : 1);
    if (ok_) ok_->setText(confirmText());
}

std::string HmiImportDialog::payload() const {
    std::string out;
    for (std::size_t i = 0; i < spec_->plan.items.size(); ++i)
        out += std::to_string(static_cast<int>(effective(i))) + (i + 1 < spec_->plan.items.size() ? "," : "");
    return out;
}

std::vector<Choice> HmiImportDialog::parse(const std::string& payload) {
    std::vector<Choice> out;
    std::stringstream in(payload);
    std::string part;
    while (std::getline(in, part, ',')) {
        const int v = std::atoi(part.c_str());
        out.push_back(static_cast<Choice>(std::clamp(v, 0, static_cast<int>(Choice::Skip))));
    }
    return out;
}

void HmiImportDialog::applyChoices(hmi::pkg::Plan& plan, const std::vector<Choice>& choices) {
    for (std::size_t i = 0; i < plan.items.size() && i < choices.size(); ++i) plan.items[i].choice = choices[i];
}

void HmiImportDialog::confirm() { finish(true); }

void HmiImportDialog::finish(bool ok) {
    if (done_ || (ok && !ready())) return;   // 1.11.2 : rien a importer - Importer est grise
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, payload()});
}

ui::EventResult HmiImportDialog::HandleEvent(const ui::InputEvent& ev) {
    // 1.11.2 (decision 188) : un paquet lache sur la fenetre ouverte : elle le lit (comme Parcourir...).
    if (const auto* drop = std::get_if<ui::FileDropped>(&ev); drop && loader_ && isPackageFile(drop->path)) {
        pending_ = drop->path;
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        const bool open = variables_ && variables_->isOpen();
        if (k->key == ui::Key::Escape && !open) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return && !open) {
            finish(true);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app
