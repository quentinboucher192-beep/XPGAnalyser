// =============================================================================
//  app/hmi/HmiTemplateGallery.cpp - Nouvelle vue : choisir un modele (lot 20).
// =============================================================================
#include "HmiTemplateGallery.hpp"

#include "HmiPainter.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/Icons.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace app {

namespace {

const gfx::FontId kSmall{13};
constexpr float kCardW = 176.f, kCardH = 132.f, kThumbH = 100.f, kGap = 14.f;
constexpr float kLeftW = 250.f, kRightW = 300.f;

const char* kSources[3] = {"Propos\xC3\xA9s par l'application", "Mes mod\xC3\xA8les", "Mod\xC3\xA8les du projet"};

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Un texte coupe a la largeur.
std::vector<std::string> wrap(const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(trial, f) > w) {
            out.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    };
    for (const char c : s) {
        if (c == ' ') flush();
        else if (c == '\n') { flush(); out.push_back(line); line.clear(); }
        else word += c;
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

} // namespace

// Le corps : tout est place et dessine a la main (les vignettes, les categories).
class GalleryBody final : public ui::Widget {
public:
    explicit GalleryBody(HmiTemplateGallery& owner) : ui::Widget("gallery.body"), g_(owner) {}
    gfx::Rect panel{}, left{}, centre{}, right{}, preview{};
    std::vector<std::pair<const HmiTemplateGallery::Entry*, gfx::Rect>> cards;
    std::vector<std::pair<float, const char*>> titles;   // les petits titres des sections
    float scroll{0.f}, contentH{0.f};
    std::array<gfx::Rect, 3> sources{};
    gfx::Rect importLink{}, manageLink{};

    void place() {
        const auto r = bounds();
        const float w = std::min(1180.f, r.w - 60.f), h = std::min(640.f, r.h - 60.f);
        panel = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float top = panel.y + 36.f;
        left = {panel.x, top, kLeftW, panel.h - 36.f};
        right = {panel.right() - kRightW, top, kRightW, panel.h - 36.f};
        centre = {left.right(), top, right.x - left.right(), panel.h - 36.f};
        for (int i = 0; i < 3; ++i) sources[static_cast<std::size_t>(i)] = {left.x + 8.f, top + 12.f + 38.f * static_cast<float>(i), kLeftW - 16.f, 34.f};
        importLink = {left.x + 12.f, left.bottom() - 64.f, kLeftW - 24.f, 24.f};
        manageLink = {left.x + 12.f, left.bottom() - 36.f, kLeftW - 24.f, 24.f};
        preview = {right.x + 16.f, top + 12.f, kRightW - 32.f, 150.f};
        // Les vignettes : la categorie choisie, puis (si ce n'est pas elle) l'application.
        cards.clear();
        const auto shown = g_.shown();
        const float x0 = centre.x + 16.f, y0 = centre.y + 56.f;
        const int perRow = std::max(1, static_cast<int>((centre.w - 32.f + kGap) / (kCardW + kGap)));
        float y = y0 - scroll;
        int col = 0;
        int last = 0;
        titles.clear();
        for (const auto* e : shown) {
            // Un petit titre a la section PROPOSES PAR L'APPLICATION (sous mes
            // modeles). 1.10.3 : plus d'AUTRES MODELES - seuls ceux du role.
            const int sec = g_.section(*e);
            if (sec != last && sec != 0) {
                if (col != 0) { y += kCardH + kGap; col = 0; }
                titles.push_back({y + 4.f, "PROPOS\xC3\x89S PAR L'APPLICATION"});
                y += 28.f;
            }
            last = sec;
            cards.push_back({e, {x0 + static_cast<float>(col) * (kCardW + kGap), y, kCardW, kCardH}});
            if (++col >= perRow) { col = 0; y += kCardH + kGap; }
        }
        if (col != 0) y += kCardH + kGap;
        contentH = y + scroll - y0;
    }

protected:
    // Les champs, sous l'apercu : Nom, Role, Modele, Largeur x Hauteur, Description.
    [[nodiscard]] float fieldsTop() const { return right.bottom() - 246.f; }
    void onLayout() override {
        place();
        const float fx = right.x + 16.f + 80.f, fw = right.w - 32.f - 80.f;
        float fy = fieldsTop();
        if (g_.nameBox_) g_.nameBox_->setBounds({fx, fy, fw, 28.f});
        fy += 34.f;
        if (g_.roleBox_) g_.roleBox_->setBounds({fx, fy, fw, 28.f});
        fy += 34.f;
        if (g_.templateBox_) g_.templateBox_->setBounds({fx, fy, fw, 28.f});
        fy += 34.f;
        if (g_.widthBox_) g_.widthBox_->setBounds({fx, fy, (fw - 24.f) * 0.5f, 28.f});
        if (g_.heightBox_) g_.heightBox_->setBounds({fx + (fw + 24.f) * 0.5f, fy, (fw - 24.f) * 0.5f, 28.f});
        fy += 34.f;
        if (g_.descBox_) g_.descBox_->setBounds({fx, fy, fw, 28.f});
        if (g_.searchBox_) g_.searchBox_->setBounds({centre.x + 16.f, centre.y + 12.f, std::min(380.f, centre.w - 32.f), 30.f});
        // Les boutons, en bas a droite.
        float bx = panel.right() - 16.f;
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
            const float bw = std::max(96.f, ui::measureWidth((*it)->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            (*it)->setBounds({bx, panel.bottom() - 44.f, bw, 30.f});
            bx -= 10.f;
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        place();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel, c.panelBg);
        ctx.r.strokeRect(panel, c.borderStrong, 1.f);
        const float titleH = 36.f;
        ctx.r.fillRect({panel.x, panel.y, panel.w, titleH}, c.headerBg);
        ctx.r.drawText({panel.x + 14.f, panel.y + (titleH - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, g_.title(),
                       ctx.theme.font.uiBold, c.text);
        // A gauche : les trois sources.
        ctx.r.fillRect(left, c.windowBg);
        for (int i = 0; i < 3; ++i) {
            const auto& r = sources[static_cast<std::size_t>(i)];
            const bool on = g_.spec_.source == i;
            if (on) ctx.r.fillRect(r, c.selectionBg);
            std::size_t n = 0;
            // 1.10.3 : les modeles du role choisi seulement (Vide compris).
            for (const auto& e : g_.spec_.entries) n += e.source == i && e.fits(g_.spec_.role) ? 1u : 0u;
            ctx.r.drawText({r.x + 10.f, r.y + (r.h - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f}, kSources[i], ctx.theme.font.ui,
                           on ? c.text : c.textMuted);
            const std::string count = std::to_string(n);
            const float cw = ctx.r.measure(count, ctx.theme.font.ui).width;
            ctx.r.drawText({r.right() - 10.f - cw, r.y + (r.h - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f}, count, ctx.theme.font.ui,
                           c.textMuted);
        }
        // 1.10.2 : l'icone dessinee, puis le texte (l'engrenage U+2699 n'est pas dans la police).
        const auto link = [&](const gfx::Rect& r, ui::Icon icon, const char* text) {
            ui::drawIcon(ctx.r, icon, {r.x, r.y + (r.h - 16.f) * 0.5f, 16.f, 16.f}, c.accent);
            ctx.r.drawText({r.x + 22.f, r.y + (r.h - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f}, text, ctx.theme.font.ui, c.accent);
        };
        link(importLink, ui::Icon::Open, "Importer un mod\xC3\xA8le\xE2\x80\xA6");
        link(manageLink, ui::Icon::Settings, "G\xC3\xA9rer mes mod\xC3\xA8les\xE2\x80\xA6");
        ctx.r.line({left.right(), left.y}, {left.right(), left.bottom()}, c.border, 1.f);
        ctx.r.line({right.x, right.y}, {right.x, right.bottom()}, c.border, 1.f);
        // Au centre : les vignettes.
        ctx.r.pushClip({centre.x, centre.y + 50.f, centre.w, centre.h - 100.f});
        for (const auto& [ty, text] : titles) ctx.r.drawText({centre.x + 16.f, ty}, text, kSmall, c.textMuted);
        for (const auto& [e, r] : cards) {
            const bool on = e->key == g_.spec_.selected;
            ctx.r.fillRoundedRect(r, on ? c.selectionBg : c.inputBg, 4.f);
            const gfx::Rect thumb{r.x + 6.f, r.y + 6.f, r.w - 12.f, kThumbH - 6.f};
            ctx.r.fillRect(thumb, c.windowBg);
            if (e->preview)
                if (const auto* v = e->preview->view(e->previewView)) paintHmiViewPreview(ctx.r, *e->preview, *v, thumb, ctx.theme);
            const auto& f = ctx.theme.font.ui;
            std::string label = e->name;
            while (label.size() > 3 && ctx.r.measure(label, f).width > r.w - 12.f) label = label.substr(0, label.size() - 4) + "\xE2\x80\xA6";
            const float lw = ctx.r.measure(label, f).width;
            ctx.r.drawText({r.x + (r.w - lw) * 0.5f, r.y + kThumbH + (r.h - kThumbH - ctx.r.lineHeight(f)) * 0.5f}, label, f, c.text);
            ctx.r.strokeRect(r, on ? c.accent : c.border, on ? 2.f : 1.f);
        }
        if (cards.empty())
            ctx.r.drawText({centre.x + 16.f, centre.y + 64.f},
                           g_.search_.empty() ? std::string("Aucun mod\xC3\xA8le de ce r\xC3\xB4le ici pour l'instant : \xC2\xAB Enregistrer comme mod\xC3\xA8le \xC2\xBB depuis une vue.")
                                              : std::string("Aucun mod\xC3\xA8le de ce r\xC3\xB4le ne contient \xC2\xAB " + g_.search_ + " \xC2\xBB."),
                           ctx.theme.font.ui, c.textMuted);
        ctx.r.popClip();
        // A droite : l'apercu du modele choisi.
        const auto* e = g_.find(g_.spec_.selected);
        ctx.r.fillRect(preview, c.windowBg);
        ctx.r.strokeRect(preview, c.border, 1.f);
        if (e && e->preview)
            if (const auto* v = e->preview->view(e->previewView)) paintHmiViewPreview(ctx.r, *e->preview, *v, preview, ctx.theme);
        float y = preview.bottom() + 12.f;
        const float x = right.x + 16.f, w = right.w - 32.f;
        if (e) {
            ctx.r.drawText({x, y}, e->name, ctx.theme.font.uiBold, c.text);
            const std::string badge = kSources[std::clamp(e->source, 0, 2)];
            y += ctx.r.lineHeight(ctx.theme.font.uiBold) + 4.f;
            ctx.r.drawText({x, y}, (e->category.empty() ? std::string{} : e->category + " \xC2\xB7 ") + badge, kSmall, c.accent);
            y += 20.f;
            for (const auto& l : wrap(e->description, kSmall, w)) {
                if (y > fieldsTop() - 60.f) break;
                ctx.r.drawText({x, y}, l, kSmall, c.textMuted);
                y += 18.f;
            }
            if (!e->origin.empty()) {
                ctx.r.drawText({x, y}, e->origin, kSmall, c.textMuted);
                y += 18.f;
            }
            y += 6.f;
            for (const auto& l : wrap(e->contents, kSmall, w)) {
                ctx.r.drawText({x, y}, l, kSmall, c.text);
                y += 18.f;
            }
            if (!e->variables.empty()) {
                for (const auto& l : wrap(e->variables, kSmall, w)) {
                    ctx.r.drawText({x, y}, l, kSmall, e->variablesOk ? c.ok : c.warning);
                    y += 18.f;
                }
            }
        }
        // Les etiquettes des champs.
        const float fy = fieldsTop();
        const char* labels[] = {"Nom", "R\xC3\xB4le", "Mod\xC3\xA8le", "Taille", "Description"};
        for (int i = 0; i < 5; ++i)
            ctx.r.drawText({x, fy + 6.f + 34.f * static_cast<float>(i)}, labels[i], ctx.theme.font.ui, c.textMuted);
        ctx.r.drawText({x + 80.f + (right.w - 32.f - 80.f) * 0.5f - 6.f, fy + 6.f + 34.f * 3.f}, "\xC3\x97", ctx.theme.font.ui, c.textMuted);
        if (!hint.empty()) ctx.r.drawText({x, fy + 34.f * 5.f + 2.f}, hint, kSmall, c.warning);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* wv = std::get_if<ui::MouseWheel>(&ev); wv && centre.contains(wv->pos)) {
            const float maxScroll = std::max(0.f, contentH - (centre.h - 110.f));
            scroll = std::clamp(scroll - wv->dy * 60.f, 0.f, maxScroll);
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
            for (int i = 0; i < 3; ++i)
                if (sources[static_cast<std::size_t>(i)].contains(d->pos)) {
                    g_.chooseSource(i);
                    return ui::EventResult::Consumed;
                }
            if (importLink.contains(d->pos)) { g_.finish(false, "importer"); return ui::EventResult::Consumed; }
            if (manageLink.contains(d->pos)) { g_.finish(false, "gerer"); return ui::EventResult::Consumed; }
            for (const auto& [e, r] : cards)
                if (r.contains(d->pos) && d->pos.y > centre.y + 50.f && d->pos.y < centre.bottom() - 50.f) {
                    (void)g_.choose(e->key);
                    if (d->clickCount >= 2) g_.confirm();
                    return ui::EventResult::Consumed;
                }
            if (panel.contains(d->pos)) return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

public:
    std::vector<ui::Button*> buttons;
    std::string              hint;

private:
    HmiTemplateGallery& g_;
};

HmiTemplateGallery::HmiTemplateGallery(Spec spec) : menu::WidgetMenu("dialog.hmiTemplateGallery"), spec_(std::move(spec)) {
    autoName_ = spec_.name;
    if (spec_.selected.empty() || !find(spec_.selected)) {
        for (const auto& e : spec_.entries)
            if (e.source == spec_.source) { spec_.selected = e.key; break; }
        if (spec_.selected.empty() && !spec_.entries.empty()) spec_.selected = spec_.entries.front().key;
    }
}

menu::MenuTraits HmiTemplateGallery::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string HmiTemplateGallery::title() const {
    // Le titre suit la liste Role (1.10.2) ; les mots de la liste des vues (HmiPanes).
    const std::string& r = spec_.role;
    const char* what = r == "popup" ? "Nouvelle popup" : r == "symbole" ? "Nouveau symbole" : r == "entete" ? "Nouvel en-t\xC3\xAAte"
                     : r == "pied" ? "Nouveau pied de page" : r == "modele" ? "Nouvel \xC3\xA9" "cran mod\xC3\xA8le" : "Nouvelle vue";
    return std::string(what) + " \xE2\x80\x94 choisir un mod\xC3\xA8le";
}

core::Status HmiTemplateGallery::buildUi() {
    auto body = std::make_unique<GalleryBody>(*this);
    auto* b = body.get();
    auto name = std::make_unique<ui::InputText>("gallery.name");
    name->setText(spec_.name);
    nameBox_ = &static_cast<ui::InputText&>(b->addChild(std::move(name)));
    links_ += nameBox_->textChanged->connect([this](const std::string&) { sync(); });
    auto role = std::make_unique<ui::DropDown>("gallery.role");
    std::vector<ui::DropDown::Item> roles;
    int chosen = 0;
    for (const auto r : hmi::kViewRoles) {
        if (r == spec_.role) chosen = static_cast<int>(roles.size());
        roles.push_back({std::string(hmi::viewRoleLabel(r)), std::string(r), {}, true});
    }
    role->setItems(std::move(roles));
    role->setSelectedIndex(chosen);
    roleBox_ = &static_cast<ui::DropDown&>(b->addChild(std::move(role)));
    // 1.10.2 : le role met tout a jour (le titre, le nom, les modeles, la taille).
    links_ += roleBox_->selectionChanged->connect([this](int i) {
        if (syncing_ || i < 0) return;
        int k = 0;
        for (const auto r : hmi::kViewRoles)
            if (k++ == i) { chooseRole(std::string(r)); break; }
    });
    // La liste Modele : les vignettes, au clavier (et pour les sessions d'avant).
    templateBox_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>("gallery.template")));
    rebuildTemplateList();
    links_ += templateBox_->selectionChanged->connect([this](int i) {
        if (!syncing_ && i >= 0 && static_cast<std::size_t>(i) < templateKeys_.size() && !templateKeys_[static_cast<std::size_t>(i)].empty())
            (void)choose(templateKeys_[static_cast<std::size_t>(i)]);
    });
    auto width = std::make_unique<ui::InputText>("gallery.width");
    widthBox_ = &static_cast<ui::InputText&>(b->addChild(std::move(width)));
    auto height = std::make_unique<ui::InputText>("gallery.height");
    heightBox_ = &static_cast<ui::InputText&>(b->addChild(std::move(height)));
    auto desc = std::make_unique<ui::InputText>("gallery.description");
    desc->setPlaceholder("ce que montre la vue");
    descBox_ = &static_cast<ui::InputText&>(b->addChild(std::move(desc)));
    auto* cancel = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Annuler", "gallery.cancel")));
    links_ += cancel->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Cr\xC3\xA9" "er", "gallery.ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += ok_->clicked->connect([this] { confirm(); });
    b->buttons = {cancel, ok_};
    // Chercher : le dernier champ (les sessions d'avant comptent les autres).
    auto search = std::make_unique<ui::InputText>("gallery.search");
    search->setPlaceholder("Chercher un mod\xC3\xA8le");
    searchBox_ = &static_cast<ui::InputText&>(b->addChild(std::move(search)));
    links_ += searchBox_->textChanged->connect([this](const std::string& t) { setSearch(t); });
    body_ = b;
    setRoot(std::move(body));
    (void)choose(spec_.selected);
    sync();
    return core::ok();
}

const HmiTemplateGallery::Entry* HmiTemplateGallery::find(const std::string& nameOrKey) const {
    for (const auto& e : spec_.entries)
        if (e.key == nameOrKey) return &e;
    for (const auto& e : spec_.entries)
        if (e.name == nameOrKey) return &e;
    return nullptr;
}

// 1.10.3 (demande du client) : SEULS LES MODELES DU ROLE CHOISI, plus Vide qui
// va avec tous - dans les trois sources, et la recherche cherche dans le role.
// Plus de section AUTRES MODELES : une popup ne propose plus Synoptique.
std::vector<const HmiTemplateGallery::Entry*> HmiTemplateGallery::shown() const {
    std::vector<const Entry*> out;
    if (!search_.empty()) {
        const std::string term = lower(search_);
        for (const auto& e : spec_.entries)
            if (e.fits(spec_.role)
                && (lower(e.name).find(term) != std::string::npos || lower(e.category).find(term) != std::string::npos
                    || lower(e.description).find(term) != std::string::npos))
                out.push_back(&e);
        return out;
    }
    for (const auto& e : spec_.entries)
        if (e.source == spec_.source && e.fits(spec_.role)) out.push_back(&e);
    if (spec_.source != 0)
        for (const auto& e : spec_.entries)
            if (e.source == 0 && e.fits(spec_.role)) out.push_back(&e);
    return out;
}

int HmiTemplateGallery::section(const Entry& e) const {
    if (!search_.empty()) return 0;
    return e.source == spec_.source ? 0 : 1;
}

void HmiTemplateGallery::rebuildTemplateList() {
    if (!templateBox_) return;
    // Les modeles de l'application qui vont avec le role, puis mes modeles et
    // ceux du projet de ce role (1.10.3 : plus d'AUTRES MODELES).
    std::vector<const Entry*> order;
    for (const auto& e : spec_.entries)
        if (e.source == 0 && e.fits(spec_.role)) order.push_back(&e);
    for (const auto& e : spec_.entries)
        if (e.source != 0 && e.fits(spec_.role)) order.push_back(&e);
    std::vector<ui::DropDown::Item> items;
    templateKeys_.clear();
    for (std::size_t i = 0; i < order.size(); ++i) {
        items.push_back({order[i]->name, order[i]->key, {}, true});
        templateKeys_.push_back(order[i]->key);
    }
    syncing_ = true;
    templateBox_->setItems(std::move(items));
    int index = -1;
    for (std::size_t i = 0; i < templateKeys_.size(); ++i)
        if (!templateKeys_[i].empty() && templateKeys_[i] == spec_.selected) index = static_cast<int>(i);
    templateBox_->setSelectedIndex(index);
    syncing_ = false;
}

void HmiTemplateGallery::chooseRole(const std::string& role) { applyRole(role, false); }

void HmiTemplateGallery::applyRole(const std::string& role, bool fromModel) {
    if (role.empty()) return;
    spec_.role = role;
    syncing_ = true;
    if (roleBox_) {
        int i = 0;
        for (const auto r : hmi::kViewRoles) {
            if (r == role && roleBox_->selectedIndex() != i) roleBox_->setSelectedIndex(i);
            ++i;
        }
    }
    syncing_ = false;
    // Le nom propose (Vue_3 -> Popup_2), s'il n'a pas ete tape.
    if (nameBox_ && spec_.nameFor && nameBox_->text() == autoName_) {
        autoName_ = spec_.nameFor(role);
        nameBox_->setText(autoName_);
    }
    // Les modeles de l'application, refaits pour ce role (taille, vignette).
    if (spec_.appEntriesFor) {
        std::vector<Entry> all = spec_.appEntriesFor(role);
        for (auto& e : spec_.entries)
            if (e.source != 0) all.push_back(std::move(e));
        spec_.entries = std::move(all);
    }
    rebuildTemplateList();
    if (!fromModel) {
        // Le modele choisi reste s'il va avec le role ; sinon Popup
        // d'equipement pour une popup, Vide sinon, ou le premier qui va.
        const auto* cur = find(spec_.selected);
        if (!cur || !cur->fits(role)) {
            const std::string preferred = role == "popup" ? "app:equipement" : "app:vide";
            const Entry* next = find(preferred);
            if (!next || !next->fits(role)) {
                next = nullptr;
                for (const auto& e : spec_.entries)
                    if (e.fits(role) && (!next || (e.source == 0 && next->source != 0))) next = &e;
            }
            if (next) spec_.selected = next->key;
        }
        applyModel(spec_.selected, false);
    }
    sync();
    if (body_) { body_->invalidateLayout(); body_->invalidate(); }
}

void HmiTemplateGallery::applyModel(const std::string& key, bool forceSize) {
    const auto* e = find(key);
    if (!e) return;
    spec_.selected = e->key;
    // La liste Modele suit (sans boucle : syncing_).
    syncing_ = true;
    if (templateBox_)
        for (std::size_t i = 0; i < templateKeys_.size(); ++i)
            if (templateKeys_[i] == e->key && templateBox_->selectedIndex() != static_cast<int>(i)) templateBox_->setSelectedIndex(static_cast<int>(i));
    syncing_ = false;
    // La taille du modele (0 : celle du projet pour ce role, celle de Vide).
    int w = e->width, h = e->height;
    if (w <= 0 || h <= 0)
        if (const auto* blank = find("app:vide")) {
            if (w <= 0) w = blank->width;
            if (h <= 0) h = blank->height;
        }
    if (widthBox_ && w > 0 && (forceSize || widthBox_->text() == autoWidth_)) {
        autoWidth_ = std::to_string(w);
        widthBox_->setText(autoWidth_);
    }
    if (heightBox_ && h > 0 && (forceSize || heightBox_->text() == autoHeight_)) {
        autoHeight_ = std::to_string(h);
        heightBox_->setText(autoHeight_);
    }
    // La description : remplacee si elle est vide ou venait du modele d'avant.
    if (descBox_ && (descBox_->text().empty() || descBox_->text() == autoDesc_)) {
        autoDesc_ = e->viewDescription;
        descBox_->setText(autoDesc_);
    }
    // Un modele d'une autre source : elle s'ouvre, la vignette defile.
    if (search_.empty() && e->source != 0 && e->source != spec_.source) {
        spec_.source = e->source;
        if (auto* b = dynamic_cast<GalleryBody*>(body_)) b->scroll = 0.f;
    }
    reveal(e->key);
}

void HmiTemplateGallery::reveal(const std::string& key) {
    auto* b = dynamic_cast<GalleryBody*>(body_);
    if (!b || b->bounds().w <= 60.f || b->bounds().h <= 60.f) return;
    b->place();
    for (const auto& [x, r] : b->cards) {
        if (x->key != key) continue;
        const float top = b->centre.y + 56.f, bottom = b->centre.bottom() - 54.f;
        const float maxScroll = std::max(0.f, b->contentH - (b->centre.h - 110.f));
        if (r.y < top) b->scroll = std::clamp(b->scroll - (top - r.y), 0.f, maxScroll);
        else if (r.bottom() > bottom) b->scroll = std::clamp(b->scroll + (r.bottom() - bottom), 0.f, maxScroll);
        b->place();
        break;
    }
}

std::string HmiTemplateGallery::fieldText(const std::string& which) const {
    const ui::InputText* box = which == "nom" ? nameBox_ : which == "largeur" ? widthBox_ : which == "hauteur" ? heightBox_
                             : which == "description" ? descBox_ : nullptr;
    return box ? box->text() : std::string{};
}

void HmiTemplateGallery::typeField(const std::string& which, const std::string& text) {
    ui::InputText* box = which == "nom" ? nameBox_ : which == "largeur" ? widthBox_ : which == "hauteur" ? heightBox_
                       : which == "description" ? descBox_ : nullptr;
    if (box) box->setText(text);
}

int HmiTemplateGallery::templateListIndex() const { return templateBox_ ? templateBox_->selectedIndex() : -1; }

int HmiTemplateGallery::scrollOffset() const {
    const auto* b = dynamic_cast<const GalleryBody*>(body_);
    return b ? static_cast<int>(b->scroll) : 0;
}

void HmiTemplateGallery::chooseSource(int source) {
    spec_.source = std::clamp(source, 0, 2);
    if (auto* b = dynamic_cast<GalleryBody*>(body_)) b->scroll = 0.f;
    // Le premier modele de la categorie, s'il y en a un (du role : 1.10.3).
    for (const auto& e : spec_.entries)
        if (e.source == spec_.source && e.fits(spec_.role)) { (void)choose(e.key); break; }
    if (body_) { body_->invalidateLayout(); body_->invalidate(); }
}

bool HmiTemplateGallery::choose(const std::string& nameOrKey) {
    const auto* e = find(nameOrKey);
    if (!e) return false;
    const std::string key = e->key;
    spec_.selected = key;
    // Le role du modele (une popup d'equipement : Popup ; un modele de ma
    // bibliotheque : le sien). Tout suit : le titre, le nom, les modeles.
    if (roleBox_ && e->role != spec_.role && !e->role.empty() && (e->source != 0 || e->role != "vue")) {
        applyRole(e->role, true);
        if (!find(key)) return false;
    }
    // La vignette, la liste Modele, la taille, la description, la source.
    applyModel(key, true);
    sync();
    if (body_) { body_->invalidateLayout(); body_->invalidate(); }
    return true;
}

void HmiTemplateGallery::setSearch(const std::string& text) {
    search_ = text;
    if (auto* b = dynamic_cast<GalleryBody*>(body_)) b->scroll = 0.f;
    if (searchBox_ && searchBox_->text() != text) searchBox_->setText(text);
    const auto list = shown();
    if (!list.empty() && std::none_of(list.begin(), list.end(), [&](const Entry* e) { return e->key == spec_.selected; }))
        (void)choose(list.front()->key);
    if (body_) { body_->invalidateLayout(); body_->invalidate(); }
}

gfx::Rect HmiTemplateGallery::cardRect(const std::string& nameOrKey) const {
    auto* b = dynamic_cast<GalleryBody*>(body_);
    if (!b) return {};
    b->place();
    const auto* e = find(nameOrKey);
    for (const auto& [x, r] : b->cards)
        if (x == e) return r;
    return {};
}

gfx::Rect HmiTemplateGallery::sourceRect(int source) const {
    auto* b = dynamic_cast<GalleryBody*>(body_);
    if (!b) return {};
    b->place();
    return b->sources[static_cast<std::size_t>(std::clamp(source, 0, 2))];
}

void HmiTemplateGallery::sync() {
    if (!ok_ || !nameBox_) return;
    const std::string name = nameBox_->text();
    std::string hint;
    if (name.empty()) hint = "Un nom : lettres, chiffres, _";
    else if (!hmi::isIdentifier(name)) hint = "Nom invalide : lettres, chiffres et _ (pas d'espace)";
    else if (spec_.nameTaken && spec_.nameTaken(name)) hint = name + " existe d\xC3\xA9j\xC3\xA0 dans le projet";
    else if (!find(spec_.selected)) hint = "Choisis un mod\xC3\xA8le";
    ok_->setEnabled(hint.empty());
    if (auto* b = dynamic_cast<GalleryBody*>(body_)) b->hint = hint;
}

void HmiTemplateGallery::confirm() {
    if (ok_ && ok_->enabled()) finish(true);
}

std::string HmiTemplateGallery::payload(const std::string& request) const {
    std::string role = spec_.role;
    if (roleBox_ && roleBox_->selectedIndex() >= 0) {
        int i = 0;
        for (const auto r : hmi::kViewRoles) {
            if (i == roleBox_->selectedIndex()) role = std::string(r);
            ++i;
        }
    }
    return "key=" + spec_.selected + "\nname=" + (nameBox_ ? nameBox_->text() : spec_.name) + "\nrole=" + role
         + "\nwidth=" + (widthBox_ ? widthBox_->text() : std::string{}) + "\nheight=" + (heightBox_ ? heightBox_->text() : std::string{})
         + "\ndescription=" + (descBox_ ? descBox_->text() : std::string{}) + "\nrequest=" + request;
}

void HmiTemplateGallery::finish(bool ok, const std::string& request) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, payload(request)});
}

HmiTemplateGallery::Answer HmiTemplateGallery::parse(const std::string& payload) {
    Answer a;
    std::stringstream in(payload);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "key") a.key = value;
        else if (key == "name") a.name = value;
        else if (key == "role") a.role = value;
        else if (key == "width") a.width = std::atoi(value.c_str());
        else if (key == "height") a.height = std::atoi(value.c_str());
        else if (key == "description") a.description = value;
        else if (key == "request") a.request = value;
    }
    return a;
}

ui::EventResult HmiTemplateGallery::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        const bool open = (roleBox_ && roleBox_->isOpen()) || (templateBox_ && templateBox_->isOpen());
        if (k->key == ui::Key::Escape && !open) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return && !open) {
            confirm();
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app
