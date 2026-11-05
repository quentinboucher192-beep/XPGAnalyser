// =============================================================================
//  app/hmi/HmiImageLibraryPane.cpp - 1.25.1 : la bibliotheque d'images (voir HmiImageLibraryPane.hpp)
// =============================================================================
#include "HmiImageLibraryPane.hpp"

#include "HmiImages.hpp"            // les vignettes et le grand apercu (une texture par contenu)
#include "HmiPaneKit.hpp"
#include "HmiPanels.hpp"
#include "../../hmi/HmiTemplates.hpp"   // la vue de demarrage, sinon la premiere vue (l'encre Auto)
#include "../../ui/Icons.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/SearchField.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>   // le fichier d'une de Mes images est-il la (Retirer)

namespace app {

using hmi::Id;
using hmi::kNoId;
using namespace hmikit;
namespace il = hmi::imagelib;
using Backdrop = HmiImageLibraryPane::Backdrop;

namespace {

// Le fond du grand apercu : le meme pour tous les volets ouverts (et d'une ouverture a l'autre).
Backdrop& sharedBackdrop() {
    static Backdrop b = Backdrop::Checker;
    return b;
}

// Le damier : deux gris moyens (une image claire comme une sombre s'y lit) ; clair : blanc ; sombre : le fond des vues.
constexpr std::uint32_t kCheckerA = 0x9EA3AA, kCheckerB = 0x868B93, kLightBg = 0xFFFFFF, kDarkBg = 0x20242B;

std::string hex6(std::uint32_t rgb) {
    char b[8];
    std::snprintf(b, sizeof b, "#%06X", static_cast<unsigned>(rgb & 0xFFFFFFu));
    return b;
}

std::uint32_t rgbOf(gfx::Color c) {
    return (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | static_cast<std::uint32_t>(c.b);
}

// Les dessins monochromes du grand apercu : Auto sur le fond choisi (le damier : sa teinte moyenne).
std::uint32_t backdropInk(Backdrop b) {
    switch (b) {
        case Backdrop::Light: return il::autoInk(hex6(kLightBg));
        case Backdrop::Dark: return il::autoInk(hex6(kDarkBg));
        case Backdrop::Checker: break;
    }
    return il::autoInk(hex6(kCheckerA));
}

// "64 x 64" (le signe de multiplication).
std::string dims(const il::Image& img) { return std::to_string(img.width) + " \xC3\x97 " + std::to_string(img.height); }

// "812 octets", "1,4 Ko", "2,1 Mo".
std::string weightText(std::uint64_t n) {
    if (n < 1024) return std::to_string(n) + " octets";
    char b[32];
    const double ko = static_cast<double>(n) / 1024.0;
    if (ko < 1024.0) std::snprintf(b, sizeof b, "%.1f Ko", ko);
    else std::snprintf(b, sizeof b, "%.1f Mo", ko / 1024.0);
    std::string s = b;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

// Le nom d'une couleur d'encre (les couleurs proposees) : "clair", "sombre" ; sinon "#RRGGBB".
std::string inkName(std::uint32_t rgb) {
    for (const auto& s : il::swatches())
        if (s.rgb == (rgb & 0xFFFFFFu)) return lower(std::string(s.label));
    return hex6(rgb);
}

// Une ressource d'apercu (rien du projet) : le contenu d'une image, l'encre a cette couleur.
hmi::Resource previewResource(const il::Image& img, std::uint32_t ink) {
    hmi::Resource r;
    r.name = img.id;
    r.format = img.format;
    r.width = img.width;
    r.height = img.height;
    r.data = il::contentFor(img, ink);
    return r;
}

// Une image dans `box`, a l'echelle qui la fait tenir (un SVG : rasterise a cette taille, net ; une image en pixels :
// agrandie au plus `maxUp` fois).
void drawFitted(gfx::IRenderer& r, const hmi::Resource& res, const il::Image& img, const gfx::Rect& box, float maxUp) {
    if (!res.data || box.w <= 1.f || box.h <= 1.f) return;
    const float iw = static_cast<float>(std::max(1, img.width)), ih = static_cast<float>(std::max(1, img.height));
    float scale = std::min(box.w / iw, box.h / ih);
    if (img.format != "SVG") scale = std::min(scale, maxUp);
    const float w = std::max(1.f, iw * scale), h = std::max(1.f, ih * scale);
    const gfx::Rect dst{std::round(box.x + (box.w - w) / 2), std::round(box.y + (box.h - h) / 2), w, h};
    auto& cache = HmiImageCache::instance();
    HmiImageCache::Image tex;
    if (img.format == "SVG")
        tex = cache.svg(r, res, std::max(4, static_cast<int>(std::ceil(w))), std::max(4, static_cast<int>(std::ceil(h))), {});
    else
        tex = cache.resource(r, res);
    if (tex.valid()) r.drawImage(tex.tex, dst);
}

// Un texte coupe en lignes a la largeur (aux espaces).
std::vector<std::string> wrapped(gfx::IRenderer& r, const std::string& text, gfx::FontId font, float maxW) {
    std::vector<std::string> out;
    std::string line, word;
    for (const char ch : text + " ") {
        if (ch != ' ') {
            word += ch;
            continue;
        }
        if (word.empty()) continue;
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && r.measure(candidate, font).width > maxW) {
            out.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

} // namespace

// ---- la fiche de l'image choisie : le grand apercu (son fond au choix), puis ses renseignements -----------------------
class HmiImageDetail final : public ui::Widget {
public:
    explicit HmiImageDetail(std::string id) : ui::Widget(std::move(id)) { setFocusPolicy(true); }

    struct Facts {
        std::string inProject;      // le nom de la ressource ; vide : pas encore
        std::string inkNote;        // un dessin monochrome : la couleur de l'import
    };
    void show(const il::Image* img, Facts facts) {
        has_ = img != nullptr;
        image_ = img ? *img : il::Image{};
        facts_ = std::move(facts);
        previewInk_ = ~0u;
        invalidate();
    }
    bool backdropRect(Backdrop b, gfx::Rect& out) const {
        const auto i = static_cast<std::size_t>(b);
        if (!has_ || i >= 3 || buttons_[i].w <= 0.f) return false;
        out = buttons_[i];
        return true;
    }

protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && bounds().contains(m->pos)) {
            grabFocus();
            for (std::size_t i = 0; has_ && i < 3; ++i)
                if (buttons_[i].contains(m->pos)) {
                    sharedBackdrop() = static_cast<Backdrop>(i);
                    invalidate();
                    return ui::EventResult::Consumed;
                }
        }
        if (const auto* mv = std::get_if<ui::MouseMove>(&ev)) {
            static const char* const tips[3] = {
                "Un damier : la transparence de l'image se voit",
                "Un fond clair : les dessins monochromes s'y montrent en sombre (Auto)",
                "Un fond sombre, celui des vues : les dessins monochromes s'y montrent en clair (Auto)"};
            std::string tip;
            for (std::size_t i = 0; has_ && i < 3; ++i)
                if (buttons_[i].contains(mv->pos)) tip = tips[i];
            setTooltip(tip);
        }
        return ui::EventResult::Ignored;
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.panelBg);
        ctx.r.fillRect({b.x, b.y, 1.f, b.h}, c.border);
        for (auto& r : buttons_) r = {};
        if (!has_) {
            ctx.r.drawText({b.x + 16, b.y + 16}, "Choisis une image pour la voir.", ctx.theme.font.ui, c.textMuted);
            return;
        }
        ctx.r.pushClip(b);
        // Le grand apercu, sur le fond choisi.
        const Backdrop back = sharedBackdrop();
        const float side = std::clamp(std::min(b.w - 32.f, b.h * 0.5f), 80.f, 320.f);
        const gfx::Rect box{std::round(b.x + (b.w - side) / 2), b.y + 14.f, side, side};
        if (back == Backdrop::Checker) {
            const float sq = 10.f;
            ctx.r.pushClip(box);
            for (int row = 0; static_cast<float>(row) * sq < box.h; ++row)
                for (int col = 0; static_cast<float>(col) * sq < box.w; ++col)
                    ctx.r.fillRect({box.x + static_cast<float>(col) * sq, box.y + static_cast<float>(row) * sq, sq, sq},
                                   gfx::Color::rgb((row + col) % 2 ? kCheckerB : kCheckerA));
            ctx.r.popClip();
        } else {
            ctx.r.fillRect(box, gfx::Color::rgb(back == Backdrop::Light ? kLightBg : kDarkBg));
        }
        ctx.r.strokeRect(box, c.border, 1.f);
        const std::uint32_t ink = image_.ink ? backdropInk(back) : il::kInk;
        if (ink != previewInk_) {
            preview_ = previewResource(image_, ink);
            previewInk_ = ink;
        }
        drawFitted(ctx.r, preview_, image_, {box.x + 16.f, box.y + 16.f, box.w - 32.f, box.h - 32.f}, 8.f);
        // Les trois fonds.
        float y = box.bottom() + 8.f;
        {
            static const char* const names[3] = {"Damier", "Clair", "Sombre"};
            const auto f = ctx.theme.font.smallUi;
            const float bw = 68.f, bh = 22.f, gap = 6.f;
            float x = std::round(b.x + (b.w - (3 * bw + 2 * gap)) / 2);
            for (std::size_t i = 0; i < 3; ++i) {
                const gfx::Rect r{x, y, bw, bh};
                buttons_[i] = r;
                const bool on = static_cast<std::size_t>(back) == i;
                ctx.r.fillRoundedRect(r, on ? c.accent.withAlpha(60) : c.inputBg, 3);
                ctx.r.strokeRect(r, on ? c.accent : c.border, 1.f);
                // Une pastille de la couleur du fond, puis son nom.
                const gfx::Rect sw{r.x + 6.f, r.y + 6.f, 10.f, 10.f};
                if (i == 0) {
                    ctx.r.fillRect({sw.x, sw.y, 5.f, 5.f}, gfx::Color::rgb(kCheckerA));
                    ctx.r.fillRect({sw.x + 5.f, sw.y, 5.f, 5.f}, gfx::Color::rgb(kCheckerB));
                    ctx.r.fillRect({sw.x, sw.y + 5.f, 5.f, 5.f}, gfx::Color::rgb(kCheckerB));
                    ctx.r.fillRect({sw.x + 5.f, sw.y + 5.f, 5.f, 5.f}, gfx::Color::rgb(kCheckerA));
                } else {
                    ctx.r.fillRect(sw, gfx::Color::rgb(i == 1 ? kLightBg : kDarkBg));
                }
                ctx.r.strokeRect(sw, c.border, 1.f);
                ctx.r.drawText({sw.right() + 5.f, r.y + (bh - ctx.r.lineHeight(f)) / 2}, names[i], f, on ? c.text : c.textMuted);
                x += bw + gap;
            }
            y += bh + 12.f;
        }
        // Le nom, puis << Categorie . 64 x 64 . PNG . image native >>.
        const float maxW = std::max(40.f, b.w - 32.f);
        ctx.r.drawText({b.x + 16, y}, image_.label, ctx.theme.font.uiBold, c.text);
        y += ctx.r.lineHeight(ctx.theme.font.uiBold) + 4.f;
        const std::string facts = image_.category + "  \xC2\xB7  " + dims(image_) + "  \xC2\xB7  " + image_.format + "  \xC2\xB7  "
                                + (image_.mine() ? "une de tes images" : "image native");
        for (const auto& l : wrapped(ctx.r, facts, ctx.theme.font.smallUi, maxW)) {
            ctx.r.drawText({b.x + 16, y}, l, ctx.theme.font.smallUi, c.textMuted);
            y += ctx.r.lineHeight(ctx.theme.font.smallUi) + 2.f;
        }
        y += 6.f;
        // Ce qu'elle est : ses mots-cles (une native), son fichier (une des tiennes).
        const std::string about = image_.mine() ? "Fichier : " + image_.file : "Mots-cl\xC3\xA9s : " + image_.words;
        for (const auto& l : wrapped(ctx.r, about, ctx.theme.font.ui, maxW)) {
            ctx.r.drawText({b.x + 16, y}, l, ctx.theme.font.ui, c.text);
            y += ctx.r.lineHeight(ctx.theme.font.ui) + 2.f;
        }
        y += 6.f;
        const bool in = !facts_.inProject.empty();
        const std::string project = in ? "Dans le projet : la ressource " + facts_.inProject
                                       : std::string("Dans le projet : pas encore (Importer dans le projet)");
        for (const auto& l : wrapped(ctx.r, project, ctx.theme.font.smallUi, maxW)) {
            ctx.r.drawText({b.x + 16, y}, l, ctx.theme.font.smallUi, in ? c.ok : c.textMuted);
            y += ctx.r.lineHeight(ctx.theme.font.smallUi) + 2.f;
        }
        if (!facts_.inkNote.empty())
            for (const auto& l : wrapped(ctx.r, facts_.inkNote, ctx.theme.font.smallUi, maxW)) {
                ctx.r.drawText({b.x + 16, y}, l, ctx.theme.font.smallUi, c.textMuted);
                y += ctx.r.lineHeight(ctx.theme.font.smallUi) + 2.f;
            }
        const std::uint64_t bytes = image_.data ? image_.data->size() : 0u;
        ctx.r.drawText({b.x + 16, y + 2.f}, "Poids : " + weightText(bytes), ctx.theme.font.smallUi, c.textMuted);
        ctx.r.popClip();
    }

private:
    il::Image     image_;
    bool          has_{false};
    Facts         facts_;
    hmi::Resource preview_;
    std::uint32_t previewInk_{~0u};
    gfx::Rect     buttons_[3]{};
};

// =============================================================================================================== le volet ===
HmiImageLibraryPane::HmiImageLibraryPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ActImport, HmiGlyph::Import,
               "Importer dans le projet (Entr\xC3\xA9" "e) : l'image devient une ressource, \xC3\xA0 choisir dans une Image, un bouton, "
               "une expression (Ctrl+Z la retire)",
               "Importer dans le projet");
    tools->add(ActEditor, HmiGlyph::View,
               "Ouvrir dans l'\xC3\xA9" "diteur de vues (double-clic) : le sous-onglet Images de sa biblioth\xC3\xA8que, sur cette image - "
               "pour la poser sur une vue, ou l'importer dans une autre couleur",
               "Ouvrir dans l'\xC3\xA9" "diteur de vues");
    tools->separator();
    tools->add(ActAdd, HmiGlyph::Plus,
               "Ajouter un fichier PNG, JPG, SVG... \xC3\xA0 Mes images (gard\xC3\xA9" "es hors du projet, pour tous tes projets)",
               "Ajouter \xC3\xA0 Mes images\xE2\x80\xA6");
    tools->add(ActRemove, HmiGlyph::Delete, "Retirer l'image choisie de Mes images (Suppr) - une image native se garde", "Retirer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    // Plusieurs images choisies : Retirer les retire toutes de Mes images (les natives se gardent, le compte rendu le
    // dit ; un seul Ctrl+Z les rend) ; Importer, Ouvrir dans l'editeur : une a la fois.
    tools_->setEnabledWhen(ActImport, [this] { return selectedImage() != nullptr && selectedIds().size() <= 1; });
    tools_->setEnabledWhen(ActEditor, [this] { return static_cast<bool>(hosts_.openInEditor) && selectedIds().size() <= 1; });
    tools_->setEnabledWhen(ActRemove, [this] { const auto* s = selectedImage(); return (s && s->mine()) || selectedIds().size() > 1; });
    tools_->setLabelWhen(ActRemove, [this] { return several::label("Retirer", selectedIds().size()); });
    search_ = &static_cast<ui::SearchField&>(addChild(std::make_unique<ui::SearchField>(
        base + ".search", "Chercher une image\xE2\x80\xA6",
        "Chaque mot est cherch\xC3\xA9 au d\xC3\xA9" "but d'un mot du nom de l'image, de sa cat\xC3\xA9gorie et de ses mots-cl\xC3\xA9s "
        "(vanne, fl\xC3\xA8" "che, danger...), sans casse ni accents - la recherche du sous-onglet Images de l'\xC3\xA9" "diteur de vues.")));
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"Image", 270.f}, {"Taille (px)", 90.f}, {"Format", 64.f}, {"Dans le projet", 200.f}, {"Mots-cl\xC3\xA9s", 380.f}});
    table->setSelectionMode(ui::SelectionMode::Extended);   // plusieurs images a retirer d'un coup
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    // La vignette devant le nom, et le cadenas (native) ou la personne (une des tiennes) devant elle.
    table_->setIconPainter([this](gfx::IRenderer& r, int icon, const gfx::Rect& box, gfx::Color color) { paintThumb(r, icon, box, color); });
    tools_->bindListKeys(*table_, {ActRemove, 0, 0});
    detail_ = &static_cast<HmiImageDetail&>(addChild(std::make_unique<HmiImageDetail>(base + ".detail")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    links_ += tools_->triggered->connect([this](int a) { runAction(a); });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!refreshing_) showDetail();
    });
    links_ += table_->activated->connect([this](ui::RowIndex r) {
        if (r < lines_.size() && lines_[r].header) setCategoryOpen(lines_[r].category, closed_.count(lines_[r].category) != 0);
        else runAction(ActEditor);   // le double-clic : l'editeur de vues, sur cette image
    });
    links_ += table_->expanderClicked->connect([this](ui::RowIndex r) {
        if (r < lines_.size() && lines_[r].header) setCategoryOpen(lines_[r].category, closed_.count(lines_[r].category) != 0);
    });
    table_->setExtraContextItems([this] { return contextItems(); });
    table_->setExtraContextOnly(true);
    links_ += table_->contextAction->connect([this](int a) { runAction(a); });
    const auto keys = table_->keyHandler();
    table_->setKeyHandler([this, keys](const ui::KeyDown& k) {
        const bool plain = !k.mods.ctrl && !k.mods.alt && !k.mods.shift;
        if (k.key == ui::Key::Return && plain && selectedImage()) { runAction(ActImport); return true; }
        if (k.key == ui::Key::C && k.mods.ctrl && !k.mods.alt && !k.mods.shift && selectedImage()) { runAction(ActCopy); return true; }
        return keys ? keys(k) : false;
    });
    links_ += search_->changed->connect([this] { setSearch(search_->text()); });
    links_ += doc_->changed->connect([this](Id) { refresh(); });   // les ressources du projet ont pu changer
    refresh();
}

HmiImageLibraryPane::~HmiImageLibraryPane() = default;

void HmiImageLibraryPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

bool HmiImageLibraryPane::fail(std::string text, std::string* why) {
    say(text, true);
    if (why) *why = std::move(text);
    return false;
}

// Mes images (relues quand elles ont change : ce volet, la palette d'un editeur, Ressources > Ma bibliotheque), puis les
// natives.
void HmiImageLibraryPane::syncMine() {
    if (loaded_ && mineRevision_ == il::userRevision()) return;
    loaded_ = true;
    mineRevision_ = il::userRevision();
    images_ = il::userImages();
    for (const auto& i : il::builtIn()) images_.push_back(i);
    thumbs_.assign(images_.size(), {});
}

const il::Image* HmiImageLibraryPane::byId(const std::string& id) const {
    for (const auto& i : images_)
        if (i.id == id) return &i;
    return nullptr;
}

std::string HmiImageLibraryPane::selectedId() const {
    const auto rows = table_->selectedModelRows();
    if (rows.empty() || rows.front() >= lines_.size()) return {};
    const auto& l = lines_[rows.front()];
    return l.image >= 0 && static_cast<std::size_t>(l.image) < images_.size() ? images_[static_cast<std::size_t>(l.image)].id : std::string{};
}

const il::Image* HmiImageLibraryPane::selectedImage() const { return byId(selectedId()); }

void HmiImageLibraryPane::setCategoryOpen(const std::string& category, bool open) {
    if (open) closed_.erase(category);
    else closed_.insert(category);
    rebuild();
}

bool HmiImageLibraryPane::categoryOpen(const std::string& category) const { return closed_.count(category) == 0; }

void HmiImageLibraryPane::setSearch(const std::string& text) {
    query_ = text;
    if (search_ && search_->text() != text) search_->setText(text);
    rebuild();
}

void HmiImageLibraryPane::refresh() { rebuild(); }

const hmi::Resource* HmiImageLibraryPane::importedAs(const il::Image& img) const {
    if (!img.data) return nullptr;
    // Changer la couleur de l'encre ne change pas la taille du contenu ("#E6EAF0" -> "#RRGGBB") : seules les ressources
    // de la meme taille se comparent.
    const auto size = img.data->size();
    for (const auto& r : doc_->project.assets.resources) {
        if (!r.data || r.data->size() != size) continue;
        if (r.data == img.data || *r.data == *img.data) return &r;
        if (!img.ink) continue;
        for (const auto& s : il::swatches())
            if (const auto blob = il::contentFor(img, s.rgb); blob && (blob == r.data || *blob == *r.data)) return &r;
    }
    return nullptr;
}

std::uint32_t HmiImageLibraryPane::importInk(const il::Image& img) const {
    if (!img.ink) return il::kInk;
    const auto& p = doc_->project;
    const hmi::View* v = p.view(p.config.startView);
    if (!v)
        if (const auto views = hmi::viewsInFolder(p, hmi::ViewFolder::Views); !views.empty()) v = views.front();
    return il::autoInk(v && !v->background.empty() ? v->background : std::string("#20242B"));
}

void HmiImageLibraryPane::rebuild() {
    refreshing_ = true;
    const std::string keep = selectedId();
    syncMine();
    lines_.clear();
    std::vector<std::string> categories{std::string(il::kMine)};
    for (const auto& c : il::builtInCategories()) categories.push_back(c);
    const bool searching = !trimmed(query_).empty();
    for (const auto& cat : categories) {
        std::vector<int> found;
        for (std::size_t i = 0; i < images_.size(); ++i)
            if (images_[i].category == cat && (!searching || il::matches(images_[i], query_))) found.push_back(static_cast<int>(i));
        // Mes images : toujours la (une ligne pour dire comment en ajouter) ; un dossier sans resultat de recherche : cache.
        if (found.empty() && (searching || cat != il::kMine)) continue;
        lines_.push_back({true, cat, found.size(), -1});
        if (closed_.count(cat) && !searching) continue;   // une recherche deplie ce qui trouve
        for (const int i : found) lines_.push_back({false, cat, 0, i});
    }
    std::vector<std::vector<std::string>> rows;
    std::vector<int> kinds;   // 0 dossier, 1 native, 2 a toi
    std::vector<int> expanders;
    std::vector<int> icons;   // la vignette : le rang de l'image + 1
    std::vector<bool> inProject;
    for (const auto& l : lines_) {
        if (l.header) {
            rows.push_back({l.category + "  (" + std::to_string(l.count) + ")", {}, {}, {},
                            l.category == il::kMine && l.count == 0
                                ? std::string("Ajoute un fichier PNG, JPG, SVG\xE2\x80\xA6 (+), ou l\xC3\xA2" "che-le ici")
                                : std::string{}});
            kinds.push_back(0);
            expanders.push_back(searching ? 1 : closed_.count(l.category) ? 0 : 1);
            icons.push_back(0);
            inProject.push_back(false);
            continue;
        }
        const auto& img = images_[static_cast<std::size_t>(l.image)];
        const hmi::Resource* r = importedAs(img);
        rows.push_back({img.label, dims(img), img.format, r ? "\xE2\x9C\x93 " + r->name : std::string("\xE2\x80\x94"),
                        img.mine() ? img.file : img.words});
        kinds.push_back(img.mine() ? 2 : 1);
        expanders.push_back(-1);
        icons.push_back(l.image + 1);
        inProject.push_back(r != nullptr);
    }
    model_ = std::make_shared<Rows>(std::vector<std::string>{"Image", "Taille (px)", "Format", "Dans le projet", "Mots-cl\xC3\xA9s"},
                                    std::move(rows), [kinds, expanders, icons, inProject](ui::RowIndex r, std::size_t c) {
                                        ui::CellStyle st;
                                        if (r >= kinds.size()) return st;
                                        if (kinds[r] == 0) {
                                            st.bold = c == 0;
                                            st.expander = c == 0 ? expanders[r] : -1;
                                            st.icon = c == 0 ? ui::Icon::Folder : ui::Icon::None;
                                            st.iconTone = ui::Tone::Warning;
                                            if (c == 4) st.fgTone = ui::Tone::Muted;
                                            return st;
                                        }
                                        if (c == 0) {
                                            // La place du cadenas (ou de la personne) avant la vignette : paintThumb les dessine.
                                            st.indent = 40.f;
                                            st.customIcon = icons[r];
                                            st.iconTone = kinds[r] == 1 ? ui::Tone::Muted : ui::Tone::Accent;
                                        }
                                        if (c == 2) st.fgTone = ui::Tone::Muted;
                                        if (c == 3) st.fgTone = inProject[r] ? ui::Tone::Ok : ui::Tone::Muted;
                                        if (c == 4) st.fgTone = ui::Tone::Muted;
                                        return st;
                                    });
    table_->setModel(model_);
    bool kept = false;
    for (std::size_t i = 0; i < lines_.size() && !keep.empty(); ++i)
        if (lines_[i].image >= 0 && images_[static_cast<std::size_t>(lines_[i].image)].id == keep) {
            table_->selectModelRows({static_cast<ui::RowIndex>(i)});
            kept = true;
        }
    if (!kept)
        for (std::size_t i = 0; i < lines_.size(); ++i)
            if (lines_[i].image >= 0) {
                table_->selectModelRows({static_cast<ui::RowIndex>(i)});
                break;
            }
    refreshing_ = false;
    showDetail();
    std::size_t mine = 0;
    for (const auto& i : images_) mine += i.mine() ? 1u : 0u;
    statusText_ = std::to_string(il::builtIn().size()) + " images natives, " + std::to_string(mine)
                + " \xC3\xA0 toi \xC2\xB7 Entr\xC3\xA9" "e : importer dans le projet \xC2\xB7 Double-clic : l'\xC3\xA9" "diteur de vues";
    status_->setMessage(statusText_, ui::StatusBar::Severity::Info);
    invalidate();
}

void HmiImageLibraryPane::showDetail() {
    const il::Image* s = selectedImage();
    HmiImageDetail::Facts facts;
    if (s) {
        if (const hmi::Resource* r = importedAs(*s)) facts.inProject = r->name;
        if (s->ink)
            facts.inkNote = "Dessin monochrome : import\xC3\xA9 en " + inkName(importInk(*s))
                          + " (Auto, lisible sur le fond de la vue de d\xC3\xA9marrage) ; une autre couleur : l'\xC3\xA9" "diteur de vues.";
    }
    detail_->show(s, std::move(facts));
}

bool HmiImageLibraryPane::select(const std::string& imageId) {
    if (!loaded_ || mineRevision_ != il::userRevision()) rebuild();   // Mes images a change : la liste d'abord
    const il::Image* img = byId(imageId);
    if (!img) return false;
    const std::string category = img->category;
    if (closed_.count(category)) setCategoryOpen(category, true);
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = 0; i < lines_.size(); ++i)
            if (lines_[i].image >= 0 && images_[static_cast<std::size_t>(lines_[i].image)].id == imageId) {
                table_->selectModelRows({static_cast<ui::RowIndex>(i)});
                showDetail();
                return true;
            }
        if (trimmed(query_).empty()) break;
        setSearch("");   // la recherche la cachait
    }
    return false;
}

bool HmiImageLibraryPane::reveal(const std::string& folder, const std::string& imageId) {
    if (!loaded_ || mineRevision_ != il::userRevision()) rebuild();
    if (!imageId.empty()) return select(imageId);
    if (folder.empty()) return true;
    const auto has = [&] {
        return std::any_of(lines_.begin(), lines_.end(), [&](const Line& l) { return l.header && l.category == folder; });
    };
    if (!has() && !trimmed(query_).empty()) setSearch("");
    if (closed_.count(folder)) setCategoryOpen(folder, true);
    // Sa premiere image (sa fiche se montre) ; un dossier vide (Mes images) : son titre.
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (!lines_[i].header && lines_[i].category == folder) {
            table_->selectModelRows({static_cast<ui::RowIndex>(i)});
            showDetail();
            return true;
        }
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (lines_[i].header && lines_[i].category == folder) {
            table_->selectModelRows({static_cast<ui::RowIndex>(i)});
            showDetail();
            return true;
        }
    return false;
}

Id HmiImageLibraryPane::importImage(const std::string& imageId, std::string* why) {
    if (!loaded_ || mineRevision_ != il::userRevision()) rebuild();
    const il::Image* img = byId(imageId);
    if (!img) {
        (void)fail("Choisis une image \xC3\xA0 importer.", why);
        return kNoId;
    }
    const il::Image copy = *img;
    if (const hmi::Resource* r = importedAs(copy)) {
        say(copy.label + " est d\xC3\xA9j\xC3\xA0 dans le projet : la ressource " + r->name + ".");
        return r->id;
    }
    il::Imported made;
    const std::uint32_t ink = importInk(copy);
    auto cmd = hmi::changeProject(doc_, "Importer l'image " + copy.label, [&](hmi::Project& p) { made = il::importImage(p, copy, ink); });
    if (!cmd || made.name.empty()) {
        (void)fail("Import impossible : " + copy.label, why);
        return kNoId;
    }
    apply_(std::move(cmd));
    refresh();
    (void)select(copy.id);
    say(copy.label + " import\xC3\xA9" "e : la ressource " + made.name
        + " - choisis-la dans une Image, un bouton ou une expression (Ctrl+Z la retire).");
    return made.id;
}

bool HmiImageLibraryPane::addFile(const std::string& path, std::string* why) {
    std::string stored, error;
    bool existed = false;
    if (!il::addUserFile(path, &stored, &existed, &error)) return fail("Mes images : " + error, why);
    refresh();
    (void)select("mes/" + stored);
    say(existed ? "D\xC3\xA9j\xC3\xA0 dans Mes images : " + stored
                : "Ajout\xC3\xA9" "e \xC3\xA0 Mes images : " + stored + " (pour tous tes projets).");
    return true;
}

namespace {
// RETIRER UNE IMAGE DE MES IMAGES, CTRL+Z LA REND (comme Mes sons). La commande garde son contenu : l'annuler la rend a
// Mes images sous son nom (le nom pris entretemps par un autre contenu : "nom (2).png", la regle de Mes images).
// Plusieurs retirees d'un coup font un groupe : un seul Ctrl+Z les rend.
class MyImageRemoveCommand final : public core::ICommand {
public:
    MyImageRemoveCommand(il::Image image, std::function<void()> refresh) : image_(std::move(image)), refresh_(std::move(refresh)) {}
    core::Status execute() override {
        std::string why;
        if (!il::removeUserImage(image_, &why)) return core::fail(core::ErrorCode::FileUnreadable, why);
        if (refresh_) refresh_();
        return core::ok();
    }
    core::Status undo() override {
        const auto slash = image_.file.find_last_of("/\\");
        const std::string name = slash == std::string::npos ? image_.file : image_.file.substr(slash + 1);
        std::string stored, why;
        if (!image_.data || !il::addUserImage(name, *image_.data, &stored, nullptr, &why))
            return core::fail(core::ErrorCode::FileUnreadable, "rendre " + image_.label + " : " + (why.empty() ? std::string("contenu perdu") : why));
        // Son fichier, pour le refaire (Ctrl+Y).
        if (slash != std::string::npos) image_.file = image_.file.substr(0, slash + 1) + stored;
        if (refresh_) refresh_();
        return core::ok();
    }
    [[nodiscard]] std::string label() const override { return "Retirer " + image_.label + " de Mes images"; }

private:
    il::Image             image_;
    std::function<void()> refresh_;
};
} // namespace

std::vector<std::string> HmiImageLibraryPane::selectedIds() const {
    std::vector<std::string> out;
    auto rows = table_->selectedModelRows();
    std::sort(rows.begin(), rows.end());
    for (const auto r : rows)
        if (r < lines_.size() && lines_[r].image >= 0 && static_cast<std::size_t>(lines_[r].image) < images_.size())
            out.push_back(images_[static_cast<std::size_t>(lines_[r].image)].id);
    return out;
}

bool HmiImageLibraryPane::selectIds(const std::vector<std::string>& ids) {
    if (!loaded_ || mineRevision_ != il::userRevision()) rebuild();
    std::vector<ui::RowIndex> rows;
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (lines_[i].image >= 0 && std::find(ids.begin(), ids.end(), images_[static_cast<std::size_t>(lines_[i].image)].id) != ids.end())
            rows.push_back(static_cast<ui::RowIndex>(i));
    table_->selectModelRows(rows);
    showDetail();
    return rows.size() == ids.size();
}

// PLUSIEURS D'UN COUP - les images choisies ; une native se garde (le compte rendu le dit), les tiennes partent ; un seul
// Ctrl+Z les rend (comme une seule, sans question : le contenu est garde par la commande).
bool HmiImageLibraryPane::removeSelected(std::string* why) {
    const auto ids = selectedIds();
    if (ids.size() > 1) {
        several::Batch b;
        b.verb = "Retirer";
        b.noun = "image";
        b.nouns = "images";
        b.feminine = true;
        for (const auto& id : ids) {
            const il::Image* s = byId(id);
            b.names.push_back(s ? s->label : id);
        }
        b.remove = [this, ids](std::size_t i, std::string* no) {
            const il::Image* s = byId(ids[i]);
            if (!s) {
                if (no) *no = "introuvable";
                return false;
            }
            if (!s->mine()) {
                if (no) *no = "image native : elle se garde";
                return false;
            }
            return removeOne(*s, no);
        };
        b.after = [this] { refresh(); };
        b.report = [this](const std::string& text, bool warning) {
            say(text + " (les projets qui les ont import\xC3\xA9" "es gardent leur ressource)", warning);
        };
        b.alive = table_->selectionChanged;
        several::run(std::move(b));
        return true;
    }
    const il::Image* s = selectedImage();
    if (!s) return fail("Choisis une de tes images.", why);
    if (!s->mine()) return fail(s->label + " est une image native : elle se garde (seules Mes images se retirent).", why);
    const std::string label = s->label;
    std::string error;
    if (!removeOne(*s, &error)) return fail(error, why);
    refresh();
    say(label + " retir\xC3\xA9" "e de Mes images (les projets qui l'ont import\xC3\xA9" "e gardent leur ressource ; Ctrl+Z la rend).");
    return true;
}

// Une de Mes images, par une commande (Ctrl+Z la rend) ; sans pile (un essai sans hote) : tout de suite.
bool HmiImageLibraryPane::removeOne(const il::Image& img, std::string* why) {
    const std::weak_ptr<const void> alive = table_->selectionChanged;
    auto cmd = std::make_unique<MyImageRemoveCommand>(img, [this, alive] {
        if (!alive.expired()) refresh();
    });
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::path(std::u8string(img.file.begin(), img.file.end())), ec)) {
        if (why) *why = "fichier introuvable : " + img.file;
        return false;
    }
    if (apply_) {
        apply_(std::move(cmd));
        return true;
    }
    if (const auto st = cmd->execute(); !st) {
        if (why) *why = st.error().context.empty() ? st.error().message() : st.error().context;
        return false;
    }
    return true;
}

std::vector<std::pair<std::string, std::size_t>> HmiImageLibraryPane::shownCategories() const {
    std::vector<std::pair<std::string, std::size_t>> out;
    for (const auto& l : lines_)
        if (l.header) out.emplace_back(l.category, l.count);
    return out;
}

std::vector<std::string> HmiImageLibraryPane::shownIds() const {
    std::vector<std::string> out;
    for (const auto& l : lines_)
        if (l.image >= 0) out.push_back(images_[static_cast<std::size_t>(l.image)].id);
    return out;
}

void HmiImageLibraryPane::setBackdrop(Backdrop b) {
    sharedBackdrop() = b;
    detail_->invalidate();
}

Backdrop HmiImageLibraryPane::backdrop() const noexcept { return sharedBackdrop(); }

bool HmiImageLibraryPane::backdropRect(Backdrop b, gfx::Rect& out) const { return detail_->backdropRect(b, out); }

std::vector<ui::PopupMenu::Item> HmiImageLibraryPane::contextItems() const {
    std::vector<ui::PopupMenu::Item> items;
    const il::Image* s = selectedImage();
    const hmi::Resource* r = s ? importedAs(*s) : nullptr;
    const auto add = [&](int action, std::string label, std::string key, ui::Icon icon, bool enabled, std::string whyNot = {}) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        it.shortcut = std::move(key);
        it.icon = icon;
        it.enabled = enabled;
        it.disabledReason = std::move(whyNot);
        it.id = action;
        items.push_back(std::move(it));
    };
    const auto sep = [&] {
        ui::PopupMenu::Item it;
        it.separator = true;
        items.push_back(it);
    };
    // Plusieurs images choisies : Retirer les N ; le reste, une a la fois.
    const auto chosen = selectedIds();
    const bool many = chosen.size() > 1;
    if (many) {
        ui::PopupMenu::Item head;
        head.heading = true;
        std::size_t mine = 0;
        for (const auto& id : chosen)
            if (const auto* x = byId(id); x && x->mine()) ++mine;
        head.label = std::to_string(chosen.size()) + " images";
        head.shortcut = std::to_string(mine) + " \xC3\xA0 toi";
        items.push_back(std::move(head));
    } else if (s) {
        ui::PopupMenu::Item head;
        head.heading = true;
        head.label = s->label;
        head.shortcut = s->mine() ? "une de tes images" : "image native";
        items.push_back(std::move(head));
    }
    const std::string none = many ? several::kOneAtATime : std::string("choisis une image");
    add(ActImport, r && !many ? "D\xC3\xA9j\xC3\xA0 dans le projet (" + r->name + ")" : std::string("Importer dans le projet"), "Entr\xC3\xA9" "e",
        ui::Icon::Import, s != nullptr && !r && !many, r && !many ? std::string("la m\xC3\xAAme image est d\xC3\xA9j\xC3\xA0 une ressource") : none);
    add(ActGoTo, "Aller \xC3\xA0 la ressource", {}, ui::Icon::GoTo, r != nullptr && !many && static_cast<bool>(hosts_.openResource),
        many ? none : r ? std::string("indisponible ici") : std::string("pas encore dans le projet"));
    add(ActEditor, "Ouvrir dans l'\xC3\xA9" "diteur de vues", "Double-clic", ui::Icon::Screen, !many && static_cast<bool>(hosts_.openInEditor),
        many ? none : std::string("l'\xC3\xA9" "diteur de vues n'est pas joignable ici"));
    add(ActCopy, "Copier le nom", "Ctrl+C", ui::Icon::Copy, s != nullptr && !many, none);
    sep();
    add(ActAdd, "Ajouter un fichier \xC3\xA0 Mes images\xE2\x80\xA6", {}, ui::Icon::Plus, true);
    add(ActReveal, "Ouvrir dans l'explorateur", {}, ui::Icon::Open, s && s->mine() && !many,
        many ? none : s ? std::string("une image native n'est pas un fichier : elle est dans le logiciel") : none);
    add(ActRemove, many ? several::label("Retirer", chosen.size()) + " de Mes images" : std::string("Retirer de Mes images"), "Suppr",
        ui::Icon::Delete, many || (s && s->mine()), s ? std::string("une image native se garde") : none);
    return items;
}

void HmiImageLibraryPane::runAction(int action) {
    const il::Image* s = selectedImage();
    // Plusieurs images choisies - Importer, Copier, Ouvrir, Aller a, l'editeur : une a la fois (Entree aussi).
    if (selectedIds().size() > 1
        && (action == ActImport || action == ActCopy || action == ActReveal || action == ActGoTo || action == ActEditor)) {
        say("Plusieurs images choisies : " + several::kOneAtATime + ".", true);
        return;
    }
    switch (action) {
        case ActImport: (void)importSelected(); break;
        case ActEditor: {
            if (!hosts_.openInEditor) {
                say("L'\xC3\xA9" "diteur de vues n'est pas joignable ici.", true);
                break;
            }
            // L'image choisie ; un dossier choisi (son titre) : ce dossier.
            std::string folder;
            if (s) folder = s->category;
            else if (const auto rows = table_->selectedModelRows(); !rows.empty() && rows.front() < lines_.size())
                folder = lines_[rows.front()].category;
            hosts_.openInEditor(folder, s ? s->id : std::string{});
            break;
        }
        case ActAdd:
            if (hosts_.askFile) hosts_.askFile();
            else {
                ui::FilePick pick;
                pick.title = "Ajouter une image \xC3\xA0 Mes images";
                pick.filters = {{"Images (PNG, JPG, SVG, BMP, GIF, ICO)", "png;jpg;jpeg;svg;bmp;gif;ico"}};
                if (!ui::pickFile(pick, [this](std::string path) {
                        if (!path.empty()) (void)addFile(path);
                    }))
                    say("Choisir un fichier n'est pas possible ici : l\xC3\xA2" "che un fichier PNG, JPG ou SVG sur la liste.", true);
            }
            break;
        case ActRemove: (void)removeSelected(); break;
        case ActCopy:
            if (s) {
                const std::string name = s->mine() ? il::resourceName(*s, il::kInk) : s->label;
                ui::setClipboardText(name);
                say("\xC2\xAB " + name + " \xC2\xBB copi\xC3\xA9.");
            }
            break;
        case ActReveal:
            if (s && s->mine()) {
                const std::string why = hosts_.reveal ? hosts_.reveal(s->file) : std::string("indisponible ici");
                if (why.empty()) say("Ouvert dans l'explorateur : " + s->file);
                else say(why, true);
            }
            break;
        case ActGoTo:
            if (s)
                if (const hmi::Resource* r = importedAs(*s); r && hosts_.openResource) hosts_.openResource(r->id);
            break;
        default: break;
    }
}

// La vignette d'une image (son contenu, l'encre a la couleur de la liste), gardee tant que la liste ne change pas.
const hmi::Resource& HmiImageLibraryPane::thumbOf(std::size_t index, std::uint32_t ink) {
    if (thumbs_.size() != images_.size()) thumbs_.assign(images_.size(), {});
    auto& slot = thumbs_[index];
    if (!slot.second.data || slot.first != ink) {
        slot.first = ink;
        slot.second = previewResource(images_[index], ink);
    }
    return slot.second;
}

void HmiImageLibraryPane::paintThumb(gfx::IRenderer& r, int icon, const gfx::Rect& box, gfx::Color color) {
    if (icon <= 0 || static_cast<std::size_t>(icon - 1) >= images_.size()) return;
    const std::size_t i = static_cast<std::size_t>(icon - 1);
    const auto& img = images_[i];
    // Le cadenas (native) ou la personne (une des tiennes), dans la marge reservee devant la vignette.
    ui::drawIcon(r, img.mine() ? ui::Icon::User : ui::Icon::Lock, {box.x - box.w - 6.f, box.y, box.w, box.h}, color);
    drawFitted(r, thumbOf(i, img.ink ? listInk_ : il::kInk), img, box, 1.f);
}

void HmiImageLibraryPane::onLayout() {
    const auto b = bounds();
    const float bar = 38.f, statusH = 24.f;
    const float searchW = std::clamp(b.w * 0.28f, 180.f, 320.f);
    tools_->setBounds({b.x, b.y, std::max(0.f, b.w - searchW - 12.f), bar});
    search_->setBounds({b.x + b.w - searchW - 8.f, b.y + 5.f, searchW, 28.f});
    status_->setBounds({b.x, b.y + b.h - statusH, b.w, statusH});
    const float h = std::max(0.f, b.h - bar - statusH);
    const float detailW = std::clamp(b.w * 0.34f, 280.f, 440.f);
    table_->setBounds({b.x, b.y + bar, std::max(0.f, b.w - detailW), h});
    detail_->setBounds({b.x + b.w - detailW, b.y + bar, detailW, h});
}

void HmiImageLibraryPane::onPaint(const ui::PaintContext& ctx) {
    // Mes images a change ailleurs (la palette d'un editeur, Ressources > Ma bibliotheque) : la liste relue.
    if (loaded_ && mineRevision_ != il::userRevision()) refresh();
    // Les vignettes monochromes : l'encre Auto du fond de la liste (claire sur un theme sombre).
    listInk_ = il::autoInk(hex6(rgbOf(ctx.theme.color.panelBg)));
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
}

// Un fichier image lache sur le volet rejoint Mes images.
ui::EventResult HmiImageLibraryPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::FileDropped>(&ev); d && bounds().contains(d->pos)) {
        (void)addFile(d->path);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app
