#include "HmiDesignPanes.hpp"

#include "HmiIcons.hpp"
#include "HmiPainter.hpp"
#include "HmiSemanticPane.hpp"              // 1.23.1 : l'onglet Couleurs semantiques
#include "HmiSimMarks.hpp"                 // 1.12.27 : fitText (l'apercu des styles)
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiStyles.hpp"         // 1.23.0 : les styles par type d'objet
#include "../../hmi/HmiSemanticColors.hpp"   // 1.23.1 : le compte de l onglet Couleurs semantiques
#include "../../hmi/HmiSymbols.hpp"
#include "../../hmi/HmiTypes.hpp"          // 1.23.1 : folderLeaf (le nom d'un dossier de styles)
#include "../../ui/Commands.hpp"          // 1.23.1 : les entrees des menus du clic droit
#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/ExprField.hpp"   // 1.23.1 : le genre de valeur d'une case (expectOf)

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <tuple>

namespace app {

using hmi::Id;
using hmi::kNoId;

using PG = ui::PropertyGrid;
namespace styles = hmi::styles;

namespace {

// Une table de chaines, dans l'ordre donne.
class Rows final : public ui::ITableModel {
public:
    Rows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, std::vector<ui::CellStyle> styles = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), styles_(std::move(styles)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return headers_[c]; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        return c == 0 && r < styles_.size() ? styles_[r] : ui::CellStyle{};
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<ui::CellStyle> styles_;
};

// 1.23.0 : une table dont chaque case a son style et chaque ligne son infobulle (les styles fournis, les types).
class CellRows final : public ui::ITableModel {
public:
    CellRows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows,
             std::vector<std::vector<ui::CellStyle>> styles, std::vector<std::string> tips)
        : headers_(std::move(headers)), rows_(std::move(rows)), styles_(std::move(styles)), tips_(std::move(tips)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        return r < styles_.size() && c < styles_[r].size() ? styles_[r][c] : ui::CellStyle{};
    }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override { return r < tips_.size() ? tips_[r] : std::string{}; }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<std::vector<ui::CellStyle>> styles_;
    std::vector<std::string> tips_;
};

PG::Property prop(std::string name, std::string value, PG::ValueType type, std::function<bool(std::string_view)> commit,
                  std::string help = {}, std::vector<std::string> choices = {}) {
    PG::Property p;
    p.name = std::move(name);
    p.value = std::move(value);
    p.type = type;
    p.commit = std::move(commit);
    p.description = std::move(help);
    p.enumValues = std::move(choices);
    return p;
}

hmi::Style* styleById(hmi::Project& p, Id id) {
    for (auto& s : p.styles) if (s.id == id) return &s;
    return nullptr;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string upperAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Les libelles de la partie << Style >> de la fiche : une propriete imposee ne les prend pas (la ligne choisie se
// retrouve par son nom).
constexpr const char* kName = "Nom";
constexpr const char* kDescription = "Description";
constexpr const char* kUsers = "Objets qui le citent";
constexpr const char* kProblems = "\xC3\x80 v\xC3\xA9rifier";

// Le fond d'une vue du projet (la premiere qui n'est pas un symbole) : la ou le style servira.
gfx::Color viewBackground(const hmi::Project& p) {
    gfx::Color bg = gfx::Color::rgb(0x20242B);
    for (const auto& v : p.views)
        if (!hmi::isSymbolView(v)) {
            (void)tryParseColor(v.background, bg);
            break;
        }
    return bg;
}

// Une encre lisible sur un fond (les legendes de l'apercu).
gfx::Color inkOn(gfx::Color bg) {
    const float lum = 0.299f * static_cast<float>(bg.r) + 0.587f * static_cast<float>(bg.g) + 0.114f * static_cast<float>(bg.b);
    return lum > 140.f ? gfx::Color::rgb(0x202326) : gfx::Color::rgb(0xE6E9EE);
}

// 1.23.0 : LA PASTILLE D'UN STYLE - les couleurs qui le disent (remplissage, allume, defaut, eteint, contour, texte...),
// la premiere valeur de chaque cle dans l'ordre du style ; trois au plus, sans doublon.
std::vector<gfx::Color> swatchOf(const hmi::Style& s) {
    static const char* const kKeys[] = {"fill", "background", "colorOn", "colorFault", "colorOff", "stroke", "textColor",
                                        "fillColor", "fluidColor"};
    std::vector<gfx::Color> out;
    const auto first = [&](const char* key, gfx::Color& c) {
        for (const auto& pr : s.props)
            if (pr.key == key && tryParseColor(pr.value, c)) return true;
        for (const auto& t : s.targets)
            for (const auto& pr : t.props)
                if (pr.key == key && tryParseColor(pr.value, c)) return true;
        return false;
    };
    for (const char* k : kKeys) {
        if (out.size() >= 3) break;
        gfx::Color c{};
        if (!first(k, c) || c.a == 0) continue;
        if (std::none_of(out.begin(), out.end(), [&](gfx::Color o) { return o.r == c.r && o.g == c.g && o.b == c.b; })) out.push_back(c);
    }
    return out;
}

void paintSwatch(gfx::IRenderer& r, const std::vector<gfx::Color>& colors, const gfx::Rect& box, gfx::Color ink) {
    const float side = box.h;
    if (colors.empty()) {
        r.strokeRect({box.x, box.y, side, side}, ink.withAlpha(90), 1.f);
        return;
    }
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const gfx::Rect sq{box.x + static_cast<float>(i) * (side + 3.f), box.y, side, side};
        r.fillRoundedRect(sq, colors[i], 3.f);
        r.strokeRect(sq, ink.withAlpha(110), 1.f);
    }
}

// Le genre d'un type (un symbole : l'instance de symbole) ; nul : inconnu, ou tous les objets.
std::optional<hmi::Kind> kindOfType(std::string_view type) {
    if (styles::isSymbolType(type)) return hmi::Kind::SymbolInstance;
    return hmi::kindFromKey(type);
}

// Le parametre d'un symbole vise par "param:<Nom>" (nul : pas un parametre, ou inconnu).
const hmi::ViewParam* paramOf(const hmi::Project& p, std::string_view type, std::string_view key) {
    if (key.substr(0, styles::kParamPrefix.size()) != styles::kParamPrefix || !styles::isSymbolType(type)) return nullptr;
    const hmi::View* sym = p.viewByName(styles::symbolName(type));
    if (!sym) return nullptr;
    const std::string want = upperAscii(key.substr(styles::kParamPrefix.size()));
    for (const auto& prm : sym->params)
        if (upperAscii(prm.name) == want) return &prm;
    return nullptr;
}

// 1.23.0 : LA CASE D'UNE PROPRIETE IMPOSEE, celle de l'inspecteur (hmiKeyInfo) : couleur, nombre, bascule, liste, texte ;
// un parametre de symbole : d'apres son type (BOOL : une bascule ; un nombre ; sinon du texte).
struct KeyKind {
    PG::ValueType            type{PG::ValueType::Text};
    std::vector<std::string> choices;
    std::string              help;
};
KeyKind keyKindOf(const hmi::Project& p, std::string_view type, std::string_view key) {
    KeyKind k;
    if (const hmi::ViewParam* prm = paramOf(p, type, key)) {
        const std::string t = upperAscii(prm->type);
        static const std::set<std::string> numbers = {"SINT", "INT", "DINT", "LINT", "USINT", "UINT", "UDINT", "ULINT",
                                                      "BYTE", "WORD", "DWORD", "LWORD", "REAL", "LREAL"};
        if (t == "BOOL") k.type = PG::ValueType::Boolean;
        else if (numbers.count(t)) k.type = PG::ValueType::Real;
        k.help = prm->description;
        return k;
    }
    if (const HmiKeyInfo* info = hmiKeyInfo(key)) {
        k.type = info->type;
        if (k.type == PG::ValueType::ReadOnly || k.type == PG::ValueType::Address) k.type = PG::ValueType::Text;
        k.choices = info->choices;
        k.help = info->help ? info->help : "";
    }
    return k;
}

// Une valeur tapee pour une propriete imposee, mise au propre (une bascule : TRUE / FALSE) ; faux : refusee (`why`).
bool cleanValue(const KeyKind& k, std::string_view key, std::string& value, std::string& why) {
    switch (k.type) {
        case PG::ValueType::Integer:
        case PG::ValueType::Real: {
            double n = 0;
            if (value.empty() || !hmi::parseNumber(value, n)) { why = "un nombre"; return false; }
            const bool positive = key == "strokeWidth" || key == "radius" || key == "opacity" || key == "fontSize";
            if (positive && n < 0) { why = "un nombre positif"; return false; }
            return true;
        }
        case PG::ValueType::Boolean: {
            const std::string u = upperAscii(value);
            if (u == "TRUE" || u == "1" || u == "VRAI" || u == "OUI") value = "TRUE";
            else if (u == "FALSE" || u == "0" || u == "FAUX" || u == "NON") value = "FALSE";
            else { why = "TRUE ou FALSE"; return false; }
            return true;
        }
        case PG::ValueType::Color: {
            gfx::Color c{};
            if (!value.empty() && !tryParseColor(value, c)) { why = "une couleur #RRGGBB (vide : transparent)"; return false; }
            return true;
        }
        case PG::ValueType::Enum: {
            if (k.choices.empty() || std::find(k.choices.begin(), k.choices.end(), value) != k.choices.end()) return true;
            why = "un des choix :";
            for (std::size_t i = 0; i < k.choices.size(); ++i) why += (i ? ", " : " ") + k.choices[i];
            return false;
        }
        default:
            return true;
    }
}

// La valeur d'une propriete qu'on ajoute : celle d'un objet neuf ; vide, selon sa case (une couleur grise, 0, FALSE,
// le premier choix).
std::string startValue(const KeyKind& k, const std::string& fresh) {
    if (!fresh.empty()) return fresh;
    switch (k.type) {
        case PG::ValueType::Color: return "#808080";
        case PG::ValueType::Integer:
        case PG::ValueType::Real: return "0";
        case PG::ValueType::Boolean: return "FALSE";
        case PG::ValueType::Enum: return k.choices.empty() ? std::string{} : k.choices.front();
        default: return {};
    }
}

// Le libelle d'un type dans les listes : un symbole porte son losange.
std::string typeName(std::string_view type) {
    return styles::isSymbolType(type) ? "\xE2\x97\x86 " + styles::typeLabel(type) : styles::typeLabel(type);
}

// Les proprietes que le style porte pour un type ("*" : tous les objets).
const std::vector<hmi::Prop>* propsOf(const hmi::Style& s, std::string_view type) {
    if (type == styles::kAnyType) return &s.props;
    const hmi::StyleTarget* t = styles::target(s, type);
    return t ? &t->props : nullptr;
}

// Les objets qui citent un style, et leur valeur pour une cle (avant / apres un changement : ceux qui suivent).
std::map<Id, std::string> citedValues(const hmi::Project& p, std::string_view styleName, std::string_view key) {
    std::map<Id, std::string> out;
    for (const auto& v : p.views)
        for (const auto& o : v.objects)
            if (const std::string c = styles::citedStyle(o); !c.empty() && upperAscii(c) == upperAscii(styleName))
                out[o.id] = styles::valueOf(p, o, key).value_or(std::string{});
    return out;
}
std::set<Id> brokenOn(const hmi::Project& p, std::string_view styleName, std::string_view key) {
    std::set<Id> out;
    for (const auto& v : p.views)
        for (const auto& o : v.objects)
            if (const std::string c = styles::citedStyle(o); !c.empty() && upperAscii(c) == upperAscii(styleName)) {
                const auto broken = styles::brokenKeys(o);
                if (std::find(broken.begin(), broken.end(), key) != broken.end()) out.insert(o.id);
            }
    return out;
}

// 1.23.0 : LA LISTE DES PROPRIETES IMPOSEES - la grille de l'inspecteur, plus une ligne CHOISIE (le clic sur une ligne :
// - Retirer la propriete la vise), soulignee d'un trait d'accent ; Suppr la retire, Inser en ajoute une.
class StylePropertyGrid final : public ui::PropertyGrid {
public:
    explicit StylePropertyGrid(std::string id) : ui::PropertyGrid(std::move(id)) {}
    std::function<bool(gfx::Point)> picked;          // vrai : une propriete imposee etait sous le clic
    std::function<std::string()>    chosenName;
    std::function<bool()>           removeChosen;
    std::function<bool()>           addOne;
    // 1.23.1 : le clic droit (ou la touche Menu) - le menu du volet, pas celui des cases (`hit` : sur une propriete
    // imposee, alors choisie).
    std::function<void(gfx::Point, bool hit)> menuAt;
protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Right && bounds().contains(d->pos) && menuAt) {
            grabFocus();               // une case ouverte est validee (elle perd la main), comme avant le menu des cases
            const bool hit = picked && picked(d->pos);
            menuAt(d->pos, hit);
            return ui::EventResult::Consumed;
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && !editing() && ui::isContextMenuKey(*k) && menuAt) {
            const std::string name = chosenName ? chosenName() : std::string{};
            gfx::Rect r{};
            const auto b = bounds();
            if (!name.empty() && valueRect(name, r)) menuAt({b.x + 40.f, r.y + r.h / 2.f}, true);
            else menuAt({b.x + 40.f, b.y + 30.f}, !name.empty());
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && bounds().contains(d->pos) && picked)
            (void)picked(d->pos);
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && !editing() && k->mods.none()) {
            if (k->key == ui::Key::Delete && removeChosen && removeChosen()) return ui::EventResult::Consumed;
            if (k->key == ui::Key::Insert && addOne && addOne()) return ui::EventResult::Consumed;
        }
        return ui::PropertyGrid::onEvent(ev);
    }
    void onPaint(const ui::PaintContext& ctx) override {
        ui::PropertyGrid::onPaint(ctx);
        const std::string name = chosenName ? chosenName() : std::string{};
        gfx::Rect r{};
        if (name.empty() || !valueRect(name, r)) return;
        const auto b = bounds();
        ctx.r.fillRect({b.x, r.y, b.w, r.h}, ctx.theme.color.accent.withAlpha(34));
        ctx.r.fillRect({b.x, r.y, 3.f, r.h}, ctx.theme.color.accent);
    }
};

// 1.12.27, 1.23.0 : L'APERCU - << APERCU  Type : [liste]  tout type de la bibliotheque ou un de tes symboles >>, puis le
// type choisi fait par le style (en ses etats), dessine par le vrai painter sur le fond d'une vue du projet.
class StylePreview final : public ui::Widget {
public:
    StylePreview(std::string id, hmi::DocumentPtr doc, const HmiStylesPane& pane)
        : ui::Widget(std::move(id)), doc_(std::move(doc)), pane_(pane) {}
    static constexpr float kBar = 32.f;
    void setList(ui::DropDown* list) noexcept { list_ = list; }
protected:
    void onLayout() override {
        const auto b = bounds();
        if (list_) list_->setBounds({b.x + 116.f, b.y + 3.f, std::min(240.f, std::max(80.f, b.w - 124.f)), kBar - 6.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        const auto font = ctx.theme.font.smallUi;
        ctx.r.fillRect(b, c.panelBg);
        ctx.r.fillRect({b.x, b.y, b.w, kBar}, c.headerBg);
        ctx.r.fillRect({b.x, b.y, b.w, 1.f}, c.border);
        ctx.r.drawText({b.x + 8.f, b.y + 9.f}, "APER\xC3\x87U", font, c.textMuted);
        ctx.r.drawText({b.x + 70.f, b.y + 9.f}, "Type :", font, c.text);
        const float hintX = b.x + 116.f + std::min(240.f, std::max(80.f, b.w - 124.f)) + 10.f;
        if (b.right() - hintX > 60.f)
            ctx.r.drawText({hintX, b.y + 9.f},
                           simmark::fitText(ctx.r, "tout type de la biblioth\xC3\xA8que ou un de tes symboles", font, b.right() - hintX - 8.f),
                           font, c.textMuted);
        const gfx::Rect well{b.x + 8.f, b.y + kBar + 6.f, std::max(0.f, b.w - 16.f), std::max(0.f, b.h - kBar - 12.f)};
        const gfx::Color bg = viewBackground(doc_->project);
        const gfx::Color ink = inkOn(bg);
        ctx.r.fillRect(well, bg);
        ctx.r.strokeRect(well, c.border, 1.f);
        const hmi::Style* s = pane_.currentStyle();
        if (!s) {
            ctx.r.drawText({well.x + 10.f, well.y + 8.f}, "Aper\xC3\xA7u : choisis un style dans la liste.", font, ink);
            return;
        }
        const std::string type = pane_.previewType();
        ctx.r.drawText({well.x + 8.f, well.y + 6.f},
                       simmark::fitText(ctx.r, s->name + " \xC2\xB7 " + styles::typeLabel(type), font, well.w - 16.f), font, ink.withAlpha(200));
        const auto items = HmiStylesPane::previewItems(doc_->project, *s, type);
        if (items.empty() || well.w < 40.f || well.h < 40.f) {
            ctx.r.drawText({well.x + 10.f, well.y + 28.f}, "Ce type ne se dessine pas ici.", font, ink);
            return;
        }
        double W = 0, H = 0;
        for (const auto& it : items) {
            const auto box = it.object.box();
            W = std::max(W, box.x + box.w);
            H = std::max(H, box.y + box.h);
        }
        const bool captions = items.size() > 1;
        const float top = 26.f, bottom = captions ? 24.f : 8.f;
        const float z = std::min(1.5f, static_cast<float>(std::min((well.w - 24.f) / std::max(1.0, W), (well.h - top - bottom) / std::max(1.0, H))));
        if (z <= 0.f) return;
        hmi::View v;
        v.width = static_cast<int>(W);
        v.height = static_cast<int>(H);
        hmi::Layer layer;
        layer.id = 1;
        v.layers.push_back(layer);
        const float ox = well.x + (well.w - static_cast<float>(W) * z) / 2.f;
        const float oy = well.y + top + (well.h - top - bottom - static_cast<float>(H) * z) / 2.f;
        const HmiViewport vp{ox, oy, z};
        const HmiPropertySource src;
        HmiPaintOptions opt;
        opt.editor = false;            // l'objet tel qu'en marche, fige (pas de cadre d'edition)
        opt.project = &doc_->project;  // une instance de symbole se resout dans le projet
        ctx.r.pushClip(well);
        for (const auto& it : items) {
            paintHmiObject(ctx.r, v, it.object, vp, src, ctx.theme, opt);
            if (!captions) continue;
            const auto box = it.object.box();
            const float cx = ox + static_cast<float>(box.x + box.w / 2) * z;
            const float w = ctx.r.measure(it.caption, font).width;
            ctx.r.drawText({cx - w / 2.f, oy + static_cast<float>(box.y + box.h) * z + 6.f}, it.caption, font, ink);
        }
        ctx.r.popClip();
    }
private:
    hmi::DocumentPtr     doc_;
    const HmiStylesPane& pane_;
    ui::DropDown*        list_{nullptr};
};

// 1.23.1 : les dossiers des choix qui ne viennent ni de la bibliotheque ni de l'inspecteur.
constexpr const char* kGroupOthers = "Autres";
constexpr const char* kGroupSymbols = "Mes symboles";
constexpr const char* kGroupParams = "Param\xC3\xA8tres du symbole";

// 1.23.1 : LE GENRE DE VALEUR D'UNE CASE, le mot que l'inspecteur ecrit au bout de son nom (DataViews : la meme table) ;
// une case qui n'en dit pas : d'apres son genre de case.
std::string kindWord(PG::ValueType t) {
    switch (t) {
        case PG::ValueType::Boolean: return "bool\xC3\xA9" "en";
        case PG::ValueType::Integer:
        case PG::ValueType::Real: return "nombre";
        case PG::ValueType::Color: return "couleur";
        case PG::ValueType::Enum: return "choix";
        case PG::ValueType::Text:
        case PG::ValueType::Address: return "texte";
        case PG::ValueType::ReadOnly: break;
    }
    return {};
}
std::string inspectorWord(const PG::Property& p) {
    using E = ui::exprfield::Expect;
    if (p.type == PG::ValueType::Boolean) return kindWord(p.type);
    switch (ui::exprfield::expectOf(p)) {
        case E::Bool: return "bool\xC3\xA9" "en";
        case E::Number: return "nombre";
        case E::Text:
        case E::Template: return "texte";
        case E::Color: return "couleur";
        case E::View: return "vue";
        case E::List: return "choix";
        case E::Time: return "dur\xC3\xA9" "e";
        case E::Value: break;
    }
    // Un texte qui n'en dit pas plus (un parametre de symbole ANY, BOOL...) : rien - le genre de la case du style le dira.
    return p.type == PG::ValueType::Text ? std::string{} : kindWord(p.type);
}
// Le libelle d'une case sans ce que l'inspecteur lui ajoute (sa formule : "  \xC6\x92 ...").
std::string plainLabel(std::string_view name) {
    if (const auto at = name.find("  \xC6\x92"); at != std::string_view::npos) name = name.substr(0, at);
    return std::string(name);
}

// Le choix avec recherche : ses mesures (1.23.1 : la petite icone d'une ligne).
constexpr float kChooserTitle = 26.f, kChooserField = 30.f, kChooserRow = 24.f, kChooserIcon = 16.f;
constexpr int kChooserRows = 12;

// 1.23.1 : LE CHAMP DE RECHERCHE DU CHOIX - Droite, Gauche, +, - vont d'abord aux dossiers (HmiStyleChooser::foldKey) ;
// un + ou un - pris par un dossier ne s'ecrit pas (son TextInput suit le KeyDown).
class ChooserField final : public ui::InputText {
public:
    ChooserField(std::string id, HmiStyleChooser& owner) : ui::InputText(std::move(id)), owner_(owner) {}
protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused()) {
            swallow_ = false;
            if (owner_.foldKey(*k)) {
                swallow_ = k->key != ui::Key::Left && k->key != ui::Key::Right;
                return ui::EventResult::Consumed;
            }
        }
        if (std::holds_alternative<ui::TextInput>(ev) && swallow_) {
            swallow_ = false;
            return ui::EventResult::Consumed;
        }
        return ui::InputText::onEvent(ev);
    }
private:
    HmiStyleChooser& owner_;
    bool             swallow_{false};
};

} // namespace

// ===================================================== 1.23.0 : le choix avec recherche ===
HmiStyleChooser::HmiStyleChooser(std::string id) : ui::Widget(std::move(id)) {
    auto f = std::make_unique<ChooserField>(this->id() + ".search", *this);
    f->setPlaceholder("Rechercher\xE2\x80\xA6");
    field_ = &static_cast<ui::InputText&>(addChild(std::move(f)));
    setVisibility(ui::Visibility::Hidden);
    // 1.23.1 : chaque frappe deplie de nouveau tout ce qui a un resultat.
    links_ += field_->textChanged->connect([this](const std::string&) {
        closedInSearch_.clear();
        scroll_ = 0.f;
        filter(highlighted(), {});
        invalidate();
    });
    // Entree : la ligne en surbrillance (un dossier : il se deplie ou se replie). Le champ finit aussi sa saisie quand
    // il perd la main (Echap, un clic ailleurs) : il ne l'a alors plus - rien n'est choisi.
    links_ += field_->editingDone->connect([this](const std::string&) {
        if (field_->focused()) (void)chooseHighlighted();
    });
    // Echap (le champ rend la main), ou la main donnee ailleurs : la liste se ferme.
    links_ += field_->focusLost->connect([this] {
        if (open_) close();
    });
}

void HmiStyleChooser::open(std::string title, std::vector<Entry> entries, gfx::Rect anchor, gfx::Rect area, Folding folding) {
    title_ = std::move(title);
    // Les lignes et la surbrillance d'avant designent l'ancienne liste : effacees AVANT de la remplacer (setText refiltre
    // et relit la surbrillance - un rang hors de la nouvelle liste lisait n'importe quoi).
    lines_.clear();
    hot_ = -1;
    entries_ = std::move(entries);
    folding_ = std::move(folding);
    area_ = area;
    open_ = true;
    // 1.23.1 : les dossiers - le premier seul deplie, ou tous.
    closed_.clear();
    closedInSearch_.clear();
    const auto names = groups();
    if (folding_.firstOnly)
        for (std::size_t i = 1; i < names.size(); ++i) closed_.insert(names[i]);
    setVisibility(ui::Visibility::Visible);
    field_->setText({});
    scroll_ = 0.f;
    filter({}, {});
    const int lines = std::clamp(static_cast<int>(entries_.size() + names.size()), 3, kChooserRows);
    const float w = std::min(area.w, std::max(anchor.w, 340.f));   // 1.23.1 : la place du genre et de la valeur
    const float h = std::min(area.h, kChooserTitle + kChooserField + 8.f + kChooserRow * static_cast<float>(lines) + 6.f);
    const float x = std::clamp(anchor.x, area.x, std::max(area.x, area.right() - w));
    float y = anchor.bottom() + 2.f;
    if (y + h > area.bottom()) y = anchor.y - 2.f - h >= area.y ? anchor.y - 2.f - h : std::max(area.y, area.bottom() - h);
    setBounds({x, y, w, h});
    layout();
    reveal(hot_);
    field_->takeFocus();
    invalidate();
}

void HmiStyleChooser::close() {
    if (!open_) return;
    open_ = false;
    setVisibility(ui::Visibility::Hidden);
    invalidate();
}

void HmiStyleChooser::setSearch(const std::string& text) { field_->setText(text); }   // textChanged -> filter()

const std::string& HmiStyleChooser::searchText() const { return field_->text(); }

std::vector<std::string> HmiStyleChooser::groups() const {
    std::vector<std::string> out;
    for (const auto& e : entries_)
        if (!e.group.empty() && std::find(out.begin(), out.end(), e.group) == out.end()) out.push_back(e.group);
    return out;
}

bool HmiStyleChooser::groupOpen(std::string_view group) const {
    const bool searching = field_ && !ui::SearchQuery(field_->text()).empty();
    const auto& closed = searching ? closedInSearch_ : closed_;
    return closed.find(std::string(group)) == closed.end();
}

void HmiStyleChooser::setGroupOpen(std::string_view group, bool on) {
    const auto all = groups();
    if (std::find(all.begin(), all.end(), group) == all.end()) return;
    const bool searching = field_ && !ui::SearchQuery(field_->text()).empty();
    auto& closed = searching ? closedInSearch_ : closed_;
    if (on) closed.erase(std::string(group));
    else closed.insert(std::string(group));
    refilter();
    invalidate();
}

void HmiStyleChooser::toggleGroup(std::string_view group) { setGroupOpen(group, !groupOpen(group)); }

void HmiStyleChooser::refilter() {
    std::string value, group;
    if (hot_ >= 0 && static_cast<std::size_t>(hot_) < lines_.size()) {
        const auto& l = lines_[static_cast<std::size_t>(hot_)];
        if (!l.group) value = entries_[l.entry].value;
        group = entries_[l.entry].group;     // une ligne cachee par son dossier replie : la surbrillance va au dossier
    }
    const float keepScroll = scroll_;
    filter(value, group);
    const auto list = listRect();
    scroll_ = std::clamp(keepScroll, 0.f, std::max(0.f, static_cast<float>(lines_.size()) * kChooserRow - list.h));
    reveal(hot_);
}

void HmiStyleChooser::filter(const std::string& keepValue, const std::string& keepGroup) {
    lines_.clear();
    const ui::SearchQuery q(field_ ? field_->text() : std::string{});
    std::string group;
    bool first = true;
    int head = -1;             // la ligne du dossier en cours (-1 : des lignes sans dossier)
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (!q.empty()
            && !q.matches({std::string_view(e.label), std::string_view(e.value), std::string_view(e.detail), std::string_view(e.extra),
                           std::string_view(e.group)}))
            continue;
        if (first || e.group != group) {
            group = e.group;
            first = false;
            head = -1;
            if (!e.group.empty()) {
                lines_.push_back({true, i, 0});
                head = static_cast<int>(lines_.size()) - 1;
            }
        }
        if (head >= 0) ++lines_[static_cast<std::size_t>(head)].count;
        if (head < 0 || groupOpen(e.group)) lines_.push_back({false, i, 0});
    }
    hot_ = -1;
    for (std::size_t k = 0; k < lines_.size() && hot_ < 0 && !keepValue.empty(); ++k)
        if (!lines_[k].group && entries_[lines_[k].entry].value == keepValue) hot_ = static_cast<int>(k);
    for (std::size_t k = 0; k < lines_.size() && hot_ < 0 && !keepGroup.empty(); ++k)
        if (lines_[k].group && entries_[lines_[k].entry].group == keepGroup) hot_ = static_cast<int>(k);
    for (std::size_t k = 0; k < lines_.size() && hot_ < 0; ++k)
        if (!lines_[k].group) hot_ = static_cast<int>(k);
    if (hot_ < 0 && !lines_.empty()) hot_ = 0;     // tout est replie : le premier dossier
    reveal(hot_);
}

std::vector<std::string> HmiStyleChooser::shownLines() const {
    std::vector<std::string> out;
    for (const auto& l : lines_) {
        const auto& e = entries_[l.entry];
        if (!l.group) out.push_back(e.label);
        else out.push_back(std::string("# ") + (groupOpen(e.group) ? "\xE2\x96\xBE " : "\xE2\x96\xB8 ") + e.group + " (" + std::to_string(l.count) + ")");
    }
    return out;
}

std::vector<std::string> HmiStyleChooser::shownValues() const {
    std::vector<std::string> out;
    for (const auto& l : lines_)
        if (!l.group) out.push_back(entries_[l.entry].value);
    return out;
}

bool HmiStyleChooser::hotIsGroup() const {
    return hot_ >= 0 && static_cast<std::size_t>(hot_) < lines_.size() && lines_[static_cast<std::size_t>(hot_)].group;
}

std::string HmiStyleChooser::highlighted() const {
    if (hot_ < 0 || static_cast<std::size_t>(hot_) >= lines_.size() || lines_[static_cast<std::size_t>(hot_)].group) return {};
    const std::size_t e = lines_[static_cast<std::size_t>(hot_)].entry;
    return e < entries_.size() ? entries_[e].value : std::string{};
}

std::string HmiStyleChooser::highlightedGroup() const {
    return hotIsGroup() ? entries_[lines_[static_cast<std::size_t>(hot_)].entry].group : std::string{};
}

void HmiStyleChooser::moveHighlight(int delta) {
    if (lines_.empty() || delta == 0) return;
    // 1.23.1 : les lignes des dossiers se choisissent aussi (Droite, Gauche, Entree les deplient) ; ce qu'un dossier
    // replie contient n'est pas dans la liste.
    const int last = static_cast<int>(lines_.size()) - 1;
    const int from = hot_ < 0 ? (delta > 0 ? -1 : last + 1) : hot_;
    hot_ = std::clamp(from + delta, 0, last);
    reveal(hot_);
    invalidate();
}

bool HmiStyleChooser::choose(std::string_view value) {
    for (std::size_t k = 0; k < lines_.size(); ++k)
        if (!lines_[k].group && entries_[lines_[k].entry].value == value) {
            hot_ = static_cast<int>(k);
            return chooseHighlighted();
        }
    return false;
}

bool HmiStyleChooser::chooseHighlighted() {
    if (!open_) return false;
    if (hotIsGroup()) {        // 1.23.1 : Entree sur un dossier - il se deplie ou se replie, rien n'est choisi
        toggleGroup(highlightedGroup());
        return false;
    }
    const std::string value = highlighted();
    if (value.empty()) return false;
    close();                   // d'abord : le volet peut en rouvrir une (le type ajoute : ses proprietes)
    chosen->emit(value);
    return true;
}

bool HmiStyleChooser::foldKey(const ui::KeyDown& k) {
    if (!open_ || k.mods.ctrl || k.mods.alt || k.mods.super) return false;
    const bool plus = k.key == ui::Key::Plus || k.layout == ui::Key::Plus;
    const bool minus = k.key == ui::Key::Minus || k.layout == ui::Key::Minus;
    const bool right = k.key == ui::Key::Right && !k.mods.shift;
    const bool left = k.key == ui::Key::Left && !k.mods.shift;
    if ((!plus && !minus && !right && !left) || hot_ < 0 || static_cast<std::size_t>(hot_) >= lines_.size()) return false;
    const Line l = lines_[static_cast<std::size_t>(hot_)];
    const std::string group = entries_[l.entry].group;
    if (l.group) {
        const bool on = groupOpen(group);
        if (right && on) {     // deja deplie : Droite va a sa premiere ligne
            if (static_cast<std::size_t>(hot_) + 1 < lines_.size() && !lines_[static_cast<std::size_t>(hot_) + 1].group) moveHighlight(1);
            return true;
        }
        if (right || plus) {
            if (!on) setGroupOpen(group, true);
            return true;
        }
        if (on) setGroupOpen(group, false);     // Gauche, -
        return true;
    }
    // Une ligne, rien de tape : Gauche replie son dossier (la surbrillance y remonte). Sinon : le curseur du champ, ou le
    // + / - tape (le - exclut un mot de la recherche).
    if (left && !group.empty() && field_->text().empty()) {
        setGroupOpen(group, false);
        return true;
    }
    return false;
}

gfx::Rect HmiStyleChooser::listRect() const {
    const auto b = bounds();
    const float top = kChooserTitle + kChooserField + 6.f;
    return {b.x + 1.f, b.y + top, std::max(0.f, b.w - 2.f), std::max(0.f, b.h - top - 4.f)};
}

int HmiStyleChooser::lineAt(gfx::Point p) const {
    const auto list = listRect();
    if (!list.contains(p)) return -1;
    const int k = static_cast<int>((p.y - list.y + scroll_) / kChooserRow);
    return k >= 0 && k < static_cast<int>(lines_.size()) ? k : -1;
}

int HmiStyleChooser::groupLine(std::string_view group) const {
    for (std::size_t k = 0; k < lines_.size(); ++k)
        if (lines_[k].group && entries_[lines_[k].entry].group == group) return static_cast<int>(k);
    return -1;
}

void HmiStyleChooser::reveal(int line) {
    if (line < 0) return;
    const auto list = listRect();
    const float top = static_cast<float>(line) * kChooserRow;
    if (top < scroll_) scroll_ = top;
    else if (list.h > 0.f && top + kChooserRow > scroll_ + list.h) scroll_ = top + kChooserRow - list.h;
}

bool HmiStyleChooser::rowRect(std::string_view value, gfx::Rect& out) const {
    const auto list = listRect();
    for (std::size_t k = 0; k < lines_.size(); ++k)
        if (!lines_[k].group && entries_[lines_[k].entry].value == value) {
            out = {list.x, list.y + static_cast<float>(k) * kChooserRow - scroll_, list.w, kChooserRow};
            return out.y >= list.y - 0.5f && out.bottom() <= list.bottom() + 0.5f;
        }
    return false;
}

bool HmiStyleChooser::groupRect(std::string_view group, gfx::Rect& out) const {
    const int k = groupLine(group);
    if (k < 0) return false;
    const auto list = listRect();
    out = {list.x, list.y + static_cast<float>(k) * kChooserRow - scroll_, list.w, kChooserRow};
    return out.y >= list.y - 0.5f && out.bottom() <= list.bottom() + 0.5f;
}

bool HmiStyleChooser::iconRect(std::string_view value, gfx::Rect& out) const {
    gfx::Rect row{};
    if (!rowRect(value, row)) return false;
    for (const auto& l : lines_)
        if (!l.group && entries_[l.entry].value == value) {
            if (!entries_[l.entry].glyph) return false;
            out = {row.right() - 10.f - kChooserIcon, row.y + (row.h - kChooserIcon) / 2.f, kChooserIcon, kChooserIcon};
            return true;
        }
    return false;
}

gfx::Rect HmiStyleChooser::eventBounds() const { return open_ ? area_ : bounds(); }

void HmiStyleChooser::onLayout() {
    const auto b = bounds();
    field_->setBounds({b.x + 8.f, b.y + kChooserTitle, std::max(0.f, b.w - 16.f), kChooserField - 4.f});
}

void HmiStyleChooser::onPaint(const ui::PaintContext& ctx) {
    if (!open_) return;
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect({b.x + 3.f, b.y + 4.f, b.w, b.h}, gfx::Color{0, 0, 0, 90});
    ctx.r.fillRoundedRect(b, c.headerBg, 6.f);
    ctx.r.strokeRect(b, c.borderStrong, 1.f);
    ctx.r.drawText({b.x + 10.f, b.y + 6.f}, title_, ctx.theme.font.smallUi, c.textMuted);
    const auto list = listRect();
    ctx.r.fillRect({b.x + 1.f, list.y - 2.f, b.w - 2.f, 1.f}, c.border);
    if (lines_.empty()) {
        ctx.r.drawText({list.x + 10.f, list.y + 6.f}, "Rien ne correspond.", ctx.theme.font.smallUi, c.textMuted);
        return;
    }
    ctx.r.pushClip(list);
    const float textDy = (kChooserRow - ctx.r.lineHeight(ctx.theme.font.ui)) / 2.f;
    const float smallDy = (kChooserRow - ctx.r.lineHeight(ctx.theme.font.smallUi)) / 2.f;
    const bool folders = std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.group.empty(); });
    for (std::size_t k = 0; k < lines_.size(); ++k) {
        const float y = list.y + static_cast<float>(k) * kChooserRow - scroll_;
        if (y + kChooserRow < list.y || y > list.bottom()) continue;
        const auto& e = entries_[lines_[k].entry];
        const bool hot = static_cast<int>(k) == hot_;
        if (hot) ctx.r.fillRect({list.x, y, list.w, kChooserRow}, c.selectionBg);
        const gfx::Color ink = hot ? c.selectionText : c.text;
        const gfx::Color muted = hot ? c.selectionText : c.textMuted;
        if (lines_[k].group) {
            // 1.23.1 : LE DOSSIER - son chevron (vers le bas : deplie), son dossier, son nom, combien il contient.
            const bool on = groupOpen(e.group);
            const float ax = list.x + 12.f, ay = y + kChooserRow * 0.5f;
            if (on) {
                ctx.r.line({ax - 4.f, ay - 2.f}, {ax, ay + 2.f}, muted, 1.5f);
                ctx.r.line({ax, ay + 2.f}, {ax + 4.f, ay - 2.f}, muted, 1.5f);
            } else {
                ctx.r.line({ax - 2.f, ay - 4.f}, {ax + 2.f, ay}, muted, 1.5f);
                ctx.r.line({ax + 2.f, ay}, {ax - 2.f, ay + 4.f}, muted, 1.5f);
            }
            ui::drawIcon(ctx.r, on ? ui::Icon::FolderOpen : ui::Icon::Folder, {list.x + 22.f, y + (kChooserRow - 14.f) / 2.f, 14.f, 14.f},
                         hot ? ink : c.warning);
            const std::size_t n = lines_[k].count;
            std::string count = std::to_string(n);
            if (const std::string& noun = n > 1 ? folding_.many : folding_.one; !noun.empty()) count += " " + noun;
            const float cw = ctx.r.measure(count, ctx.theme.font.smallUi).width;
            ctx.r.drawText({list.right() - 10.f - cw, y + smallDy}, count, ctx.theme.font.smallUi, muted);
            ctx.r.drawText({list.x + 42.f, y + textDy}, simmark::fitText(ctx.r, e.group, ctx.theme.font.uiBold, list.w - 62.f - cw),
                           ctx.theme.font.uiBold, ink);
            continue;
        }
        // Une ligne : sous le nom de son dossier ; a droite, sa petite icone, son genre, sa valeur d'objet neuf.
        const float left = list.x + (folders && !e.group.empty() ? 42.f : 14.f);
        float right = list.right() - 10.f;
        if (e.glyph) {
            drawHmiGlyph(ctx.r, *e.glyph, {right - kChooserIcon, y + (kChooserRow - kChooserIcon) / 2.f, kChooserIcon, kChooserIcon},
                         hot ? ink : c.accent);
            right -= kChooserIcon + 8.f;
        }
        if (!e.detail.empty()) {
            const std::string d = simmark::fitText(ctx.r, e.detail, ctx.theme.font.smallUi, list.w * 0.3f);
            const float dw = ctx.r.measure(d, ctx.theme.font.smallUi).width;
            ctx.r.drawText({right - dw, y + smallDy}, d, ctx.theme.font.smallUi, muted);
            right -= dw + 10.f;
        }
        const float labelW = ctx.r.measure(e.label, ctx.theme.font.ui).width;
        if (!e.extra.empty()) {
            // Plus discrete : seulement si le libelle garde sa place.
            const float room = std::min(list.w * 0.28f, right - left - std::min(labelW, list.w * 0.45f) - 12.f);
            if (room >= 28.f) {
                const std::string x = simmark::fitText(ctx.r, e.extra, ctx.theme.font.smallUi, room);
                const float xw = ctx.r.measure(x, ctx.theme.font.smallUi).width;
                ctx.r.drawText({right - xw, y + smallDy}, x, ctx.theme.font.smallUi, muted.withAlpha(hot ? 200 : 140));
                right -= xw + 10.f;
            }
        }
        ctx.r.drawText({left, y + textDy}, simmark::fitText(ctx.r, e.label, ctx.theme.font.ui, std::max(0.f, right - left - 4.f)),
                       ctx.theme.font.ui, ink);
    }
    ctx.r.popClip();
    // Plus de lignes que de place : la position, a droite.
    const float total = static_cast<float>(lines_.size()) * kChooserRow;
    if (total > list.h + 0.5f && list.h > 0.f) {
        const float th = std::max(16.f, list.h * list.h / total);
        const float ty = list.y + (list.h - th) * (scroll_ / std::max(1.f, total - list.h));
        ctx.r.fillRoundedRect({list.right() - 5.f, ty, 3.f, th}, c.scrollbar, 1.5f);
    }
}

ui::EventResult HmiStyleChooser::onEvent(const ui::InputEvent& ev) {
    if (!open_) return ui::EventResult::Ignored;
    const auto b = bounds();
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (!b.contains(d->pos)) {         // un clic ailleurs : la liste se ferme, rien d'autre
            close();
            return ui::EventResult::Consumed;
        }
        if (const int at = lineAt(d->pos); at >= 0 && d->button == ui::MouseButton::Left) {
            hot_ = at;
            (void)chooseHighlighted();     // 1.23.1 : un dossier se deplie ou se replie
            invalidate();
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        if (!b.contains(m->pos)) return ui::EventResult::Ignored;
        if (const int at = lineAt(m->pos); at >= 0 && at != hot_) {
            hot_ = at;
            invalidate();
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev)) return b.contains(u->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (!b.contains(w->pos)) return ui::EventResult::Ignored;
        const auto list = listRect();
        const float most = std::max(0.f, static_cast<float>(lines_.size()) * kChooserRow - list.h);
        scroll_ = std::clamp(scroll_ - w->dy * kChooserRow * 3.f, 0.f, most);
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (foldKey(*k)) return ui::EventResult::Consumed;
        switch (k->key) {
            case ui::Key::Down: moveHighlight(1); return ui::EventResult::Consumed;
            case ui::Key::Up: moveHighlight(-1); return ui::EventResult::Consumed;
            case ui::Key::PageDown: moveHighlight(kChooserRows - 1); return ui::EventResult::Consumed;
            case ui::Key::PageUp: moveHighlight(-(kChooserRows - 1)); return ui::EventResult::Consumed;
            case ui::Key::Return: (void)chooseHighlighted(); return ui::EventResult::Consumed;
            case ui::Key::Escape: close(); return ui::EventResult::Consumed;
            default: break;
        }
    }
    return ui::EventResult::Ignored;
}

// =================================================================== styles ===
std::string HmiStylesPane::propertyName(std::string_view type, std::string_view key) {
    if (key.substr(0, styles::kParamPrefix.size()) == styles::kParamPrefix) return styles::propertyLabel(key);
    if (hmiKeyInfo(key)) {
        if (const auto kind = kindOfType(type)) return hmiPropertyLabel(*kind, key);
        return hmiKeyInfo(key)->label;
    }
    return styles::propertyLabel(key);
}

std::vector<HmiStylesPane::PreviewItem> HmiStylesPane::previewItems(const hmi::Project& p, const hmi::Style& s, std::string_view type) {
    std::vector<PreviewItem> out;
    const auto kind = kindOfType(type);
    if (!kind) return out;
    hmi::Object base = styles::previewObject(p, s, type, 1);
    if (base.props.empty()) return out;
    base.layer = 1;
    // Un equipement a etats : sa valeur est une bascule, et il a ses couleurs d'etat (ou un defaut).
    const hmi::Object fresh = hmi::makeObject(*kind, 1, {}, 0, 0, kNoId);
    const hmi::Prop* value = fresh.find("value");
    const bool boolish = value && (value->value == "TRUE" || value->value == "FALSE");
    const bool states = boolish && ((fresh.find("colorOn") && fresh.find("colorOff")) || fresh.find("fault"));
    const bool faults = states && fresh.find("fault") != nullptr;
    if (!states) {
        out.push_back({std::move(base), {}});
    } else {
        hmi::Object on = base, off = base;
        on.set("value", "TRUE");
        off.set("value", "FALSE");
        out.push_back({std::move(on), "en marche"});
        out.push_back({std::move(off), "\xC3\xA0 l'arr\xC3\xAAt"});
        if (faults) {
            hmi::Object bad = base;
            bad.set("value", "FALSE");
            bad.set("fault", "TRUE");
            out.push_back({std::move(bad), "en d\xC3\xA9" "faut"});
        }
    }
    const double w = std::max(1.0, out.front().object.number("w", 60));
    const double gap = std::max(24.0, w * 0.4);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i].object.id = static_cast<Id>(i + 1);
        out[i].object.setNumber("x", static_cast<double>(i) * (w + gap));
        out[i].object.setNumber("y", 0);
    }
    return out;
}

HmiStylesPane::HmiStylesPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ActNew, HmiGlyph::Plus, "Nouveau style du projet (vide) : ajoute-lui des types d'objets et ce qu'il leur impose", "Nouveau");
    tools->add(ActDuplicate, HmiGlyph::Duplicate,
               "Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 : une copie modifiable du style choisi, m\xC3\xAAme fourni (verrouill\xC3\xA9) - "
               "son nom se tape tout de suite",
               "Dupliquer \xC3\xA0 partir de\xE2\x80\xA6");
    tools->add(ActRename, HmiGlyph::Text, "Renommer le style (les objets qui le citent suivent) - un style fourni ne se renomme pas", "Renommer");
    tools->add(ActRemove, HmiGlyph::Delete, "Retirer le style (les objets gardent leurs valeurs et ne le citent plus), ou le dossier "
                                            "choisi (ses styles remontent d'un cran) - Ctrl+Z le rend ; un style fourni ne se retire pas",
               "Retirer");
    tools->add(ActFolder, HmiGlyph::Plus, "Nouveau dossier (sans effet sur les noms) : glisse des styles dessus pour les y ranger", "Dossier");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    // 1.24.0 : plusieurs styles du projet choisis - Retirer les retire tous ; Dupliquer, Renommer : un a la fois.
    const auto one = [this] { return !builtin_.empty() || !folders_ || folders_->selectedCount() <= 1; };
    tools_->setEnabledWhen(ActDuplicate, [this, one] { return currentStyle() != nullptr && one(); });
    tools_->setEnabledWhen(ActRename, [this, one] { return currentStyle() != nullptr && one(); });
    tools_->setEnabledWhen(ActRemove, [this] { return selectedStyle() != kNoId || (builtin_.empty() && !folders_->selectedFolder().empty()); });
    tools_->setLabelWhen(ActRemove, [this] { return several::label("Retirer", builtin_.empty() && folders_ ? folders_->selectedCount() : 0); });
    // Lot API 8 : chercher un style - la recherche de toute l'application.
    search_ = &static_cast<ui::SearchField&>(addChild(std::make_unique<ui::SearchField>(
        base + ".search", "Chercher un style, un type, une valeur\xE2\x80\xA6",
        "Chaque mot est cherch\xC3\xA9 dans le nom du style, sa description, ses types d'objets (Pompe, Symbole Sym_Moteur...), "
        "ses propri\xC3\xA9t\xC3\xA9s et leurs valeurs (colorOn #4D4D4D...) et son dossier - tous les mots (ET), sans casse ni accents ; "
        "\"une phrase\" entre guillemets ; -mot : l'exclure.")));
    // 1.23.0 : les styles fournis, verrouilles (un cadenas), au-dessus de ceux du projet.
    auto fixed = std::make_unique<ui::TableView>(base + ".builtins");
    fixed->setColumns({{"Fournis \xE2\x80\x94 verrouill\xC3\xA9s", 160.f}, {"Objets", 56.f, 40.f, true, true, true, ui::Align::End},
                       {"Couleurs", 66.f, 40.f, true, false}});
    fixed->setSelectionMode(ui::SelectionMode::Single);
    builtins_ = &static_cast<ui::TableView&>(addChild(std::move(fixed)));
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"Du projet", 160.f}, {"Objets", 56.f, 40.f, true, true, true, ui::Align::End}, {"Couleurs", 66.f, 40.f, true, false}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    // 1.12.27 : Suppr, Ctrl+D, Inser dans la liste - comme les boutons de la barre. Les fournis : Ctrl+D, Inser.
    tools_->bindListKeys(*builtins_, {0, ActDuplicate, ActNew, 0, 0, 0});
    tools_->bindListKeys(*table_);
    // La pastille de chaque style (ses couleurs) ; les fournis par leur rang, ceux du projet par leur identifiant.
    builtins_->setIconPainter([](gfx::IRenderer& r, int icon, const gfx::Rect& box, gfx::Color ink) {
        const auto& all = styles::builtins();
        if (icon >= 1 && static_cast<std::size_t>(icon) <= all.size()) paintSwatch(r, swatchOf(all[static_cast<std::size_t>(icon) - 1]), box, ink);
    });
    table_->setIconPainter([this](gfx::IRenderer& r, int icon, const gfx::Rect& box, gfx::Color ink) {
        if (const hmi::Style* s = doc_->project.style(static_cast<Id>(icon))) paintSwatch(r, swatchOf(*s), box, ink);
    });
    // 1.23.0 : les types d'objets du style choisi.
    auto types = std::make_unique<ui::TableView>(base + ".types");
    types->setColumns({{"Type", 170.f}, {"Propri\xC3\xA9t\xC3\xA9s", 90.f, 50.f, true, true, true, ui::Align::End}});
    types->setSelectionMode(ui::SelectionMode::Single);
    types->setIconPainter([](gfx::IRenderer& r, int icon, const gfx::Rect& box, gfx::Color ink) {
        if (icon > 0) drawHmiGlyph(r, static_cast<HmiGlyph>(icon - 1), box, ink);
    });
    types_ = &static_cast<ui::TableView&>(addChild(std::move(types)));
    const auto button = [this, &base](const char* key, const char* text, const char* tip) {
        auto b = std::make_unique<ui::Button>(text, base + "." + key);
        b->setStyle(ui::Button::Style::Flat);
        b->setTooltip(tip);
        return &static_cast<ui::Button&>(addChild(std::move(b)));
    };
    addType_ = button("addType", "+ Ajouter un type\xE2\x80\xA6",
                      "Un genre de la biblioth\xC3\xA8que ou un de tes symboles : le style imposera des propri\xC3\xA9t\xC3\xA9s \xC3\xA0 ses objets (Inser)");
    removeType_ = button("removeType", "\xE2\x88\x92 Retirer le type choisi",
                         "Le style ne vise plus ce type : ses objets gardent leurs valeurs (Suppr ; Ctrl+Z le rend)");
    // 1.23.0 : les proprietes imposees au type choisi (et, en tete, le nom et la description du style).
    auto grid = std::make_unique<StylePropertyGrid>(base + ".grid");
    StylePropertyGrid* sg = grid.get();
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::move(grid)));
    addProp_ = button("addProperty", "+ Ajouter une propri\xC3\xA9t\xC3\xA9\xE2\x80\xA6",
                      "N'importe quelle propri\xC3\xA9t\xC3\xA9 du type (couleur, taille, texte, liste, param\xC3\xA8tre du symbole...), "
                      "avec la valeur d'un objet neuf (Inser)");
    removeProp_ = button("removeProperty", "\xE2\x88\x92 Retirer la propri\xC3\xA9t\xC3\xA9",
                         "Le style ne l'impose plus : les objets gardent leur valeur (Suppr ; Ctrl+Z la rend)");
    // 1.12.27, 1.23.0 : l'apercu, et sa liste Type : (tout type de la bibliotheque ou un symbole).
    auto pv = std::make_unique<StylePreview>(base + ".preview", doc_, *this);
    StylePreview* spv = pv.get();
    preview_ = &addChild(std::move(pv));
    previewTypes_ = &static_cast<ui::DropDown&>(preview_->addChild(std::make_unique<ui::DropDown>(base + ".previewType")));
    spv->setList(previewTypes_);
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    chooser_ = &static_cast<HmiStyleChooser&>(addChild(std::make_unique<HmiStyleChooser>(base + ".chooser")));   // au-dessus
    // 1.23.1 : le menu du clic droit des proprietes imposees (les tables ont le leur) - le dernier : au-dessus de tout.
    propMenu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(base + ".propMenu")));
    // Lot 21 : les styles ranges en dossiers ; glisser une ou plusieurs lignes. 1.23.0 : le nom se change dans sa case.
    folders_ = std::make_unique<HmiFolderTable>(*table_, doc_, apply_);
    folders_->setItemEditing([](Id, std::size_t c) { return c == 0; }, [this](Id style, std::size_t c, const std::string& text) {
        std::string why;
        return c == 0 && renameStyle(style, text, &why);
    });
    links_ += folders_->message->connect([this](const std::string& text) { say(text); });
    links_ += folders_->relayout->connect([this] { refresh(); });
    // 1.23.1 : LES MENUS DU CLIC DROIT - un par liste, a la place du menu commun des tableaux (Copier, Coller...). La
    // table choisit d'abord la ligne cliquee (selectionChanged), puis demande ses entrees.
    for (ui::TableView* t : {builtins_, table_}) {
        t->setExtraContextOnly(true);
        t->setExtraContextItems([this] { return styleMenuItems(); });
        links_ += t->contextAction->connect([this](int a) { menuChosen(a); });
    }
    types_->setExtraContextOnly(true);
    types_->setExtraContextItems([this] { return typeMenuItems(); });
    links_ += types_->contextAction->connect([this](int a) { menuChosen(a); });
    links_ += propMenu_->itemChosen->connect([this](int a) {
        propMenu_->close();        // choisi d'ailleurs (un script, un essai) : ferme aussi
        menuChosen(a);
    });
    links_ += propMenu_->dismissed->connect([this] { grid_->takeFocus(); });

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedStyle();
        if (a == ActNew) {
            const Id made = addStyle();
            selectStyle(made);
            beginRename(made);
        } else if (a == ActDuplicate) {
            if (const hmi::Style* s = currentStyle()) {
                const Id made = duplicateFrom(s->name);
                selectStyle(made);
                beginRename(made);
            }
        } else if (a == ActRename) {
            if (locked()) say("Un style fourni ne se renomme pas : \xC2\xAB Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 \xC2\xBB en fait une copie \xC3\xA0 toi.", true);
            else if (sel != kNoId) beginRename(sel);
        } else if (a == ActRemove && builtin_.empty() && folders_->selectedCount() > 1) {
            // 1.24.0 : plusieurs styles du projet (ou dossiers) - d'un coup, un seul Ctrl+Z (sans question, comme un seul).
            several::Batch b;
            b.verb = "Retirer";
            b.noun = "style";
            b.nouns = "styles";
            folders_->fillBatch(b, [this](Id id) { const auto* s = doc_->project.style(id); return s ? s->name : std::string("?"); },
                                [this](Id id, std::string*) { return removeStyle(id); });
            b.report = [this](const std::string& text, bool warning) { say(text + " (les objets gardent leurs valeurs)", warning); };
            b.alive = tools_->triggered;
            several::run(std::move(b));
        } else if (a == ActRemove && sel != kNoId) {
            (void)removeStyle(sel);
        } else if (a == ActRemove && builtin_.empty()) {
            if (const auto f = folders_->selectedFolder(); !f.empty()) (void)folders_->deleteFolder(f);
        } else if (a == ActFolder) {
            (void)folders_->newFolder();
        }
    });
    // Le choix : un style fourni OU un style du projet.
    links_ += builtins_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        const auto rows = builtins_->selectedModelRows();
        builtin_ = !rows.empty() && rows.front() < builtinRows_.size() ? builtinRows_[rows.front()] : std::string{};
        if (!builtin_.empty()) {
            table_->selectModelRows({}, false);
            projectSel_ = kNoId;
        }
        styleChanged();
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (!table_->selectedModelRows().empty() && !builtin_.empty()) {
            builtin_.clear();
            builtins_->selectModelRows({}, false);
        }
        projectSel_ = folders_->selectedItem();
        styleChanged();
    });
    links_ += types_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        const auto rows = types_->selectedModelRows();
        if (rows.empty() || rows.front() >= typeRows_.size()) return;
        selectType(typeRows_[rows.front()]);
    });
    types_->setKeyHandler([this](const ui::KeyDown& k) {
        if (!k.mods.none()) return false;
        if (k.key == ui::Key::Delete && removeType_->enabled()) return removeType(type_);
        if (k.key == ui::Key::Insert && addType_->enabled()) {
            openTypeChooser();
            return true;
        }
        return false;
    });
    links_ += addType_->clicked->connect([this] { openTypeChooser(); });
    links_ += removeType_->clicked->connect([this] { (void)removeType(type_); });
    links_ += addProp_->clicked->connect([this] { openPropertyChooser(); });
    links_ += removeProp_->clicked->connect([this] { (void)removeProperty(prop_); });
    sg->picked = [this](gfx::Point at) {
        const hmi::Style* s = currentStyle();
        const auto* props = s ? propsOf(*s, type_) : nullptr;
        if (!props) return false;
        for (const auto& pr : *props) {
            gfx::Rect r{};
            if (grid_->valueRect(propertyName(type_, pr.key), r) && at.y >= r.y && at.y < r.y + r.h) {
                selectProperty(pr.key);
                return true;
            }
        }
        return false;
    };
    // 1.23.1 : le clic droit sur la fiche - le menu des proprietes imposees ; ailleurs que sur l'une d'elles (le nom du
    // style, un titre), il ne vise aucune propriete.
    sg->menuAt = [this](gfx::Point at, bool hit) {
        if (!hit && !prop_.empty()) selectProperty({});
        openPropertyMenu(at);
    };
    sg->chosenName = [this] { return prop_.empty() ? std::string{} : propertyName(type_, prop_); };
    sg->removeChosen = [this] { return removeProp_->enabled() && removeProperty(prop_); };
    sg->addOne = [this] {
        if (!addProp_->enabled()) return false;
        openPropertyChooser();
        return true;
    };
    links_ += chooser_->chosen->connect([this](const std::string& value) {
        if (chooserFor_ == 1) {
            if (addType(value)) openPropertyChooser();     // un type sans propriete n'impose rien : la premiere, tout de suite
        } else if (chooserFor_ == 2) {
            (void)addProperty(value);
        }
    });
    links_ += previewTypes_->selectionChanged->connect([this](int i) {
        const auto* item = i >= 0 ? previewTypes_->selectedItem() : nullptr;
        if (item && item->value != previewType_) {
            previewType_ = item->value;
            preview_->invalidate();
        }
    });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
    // Lot API 8 : la recherche, branchee, puis relue (retenue d'une seance a l'autre).
    links_ += search_->changed->connect([this] { refresh(); });
    search_->recall();
    // 1.23.1 : l'onglet Couleurs semantiques, cache tant qu'on ne le choisit pas.
    colors_ = &static_cast<HmiSemanticColorsPane&>(addChild(std::make_unique<HmiSemanticColorsPane>(base + ".semantic", doc_, apply_)));
    applyTab();
}

void HmiStylesPane::applyTab() {
    for (const auto& child : children())
        child->setVisibility(child.get() == colors_ ? (colorsShown_ ? ui::Visibility::Visible : ui::Visibility::Collapsed)
                                                    : (colorsShown_ ? ui::Visibility::Collapsed : ui::Visibility::Visible));
    invalidateLayout();
    invalidate();
}

void HmiStylesPane::showSemanticColors(const std::string& name) {
    colorsShown_ = true;
    applyTab();
    layout();
    if (!name.empty()) (void)colors_->select(name);
    colors_->table().takeFocus();
}

void HmiStylesPane::showStyles() {
    colorsShown_ = false;
    applyTab();
    layout();
}

ui::EventResult HmiStylesPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        if (tabRects_[0].contains(d->pos)) {
            if (colorsShown_) showStyles();
            return ui::EventResult::Consumed;
        }
        if (tabRects_[1].contains(d->pos)) {
            if (!colorsShown_) showSemanticColors();
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

void HmiStylesPane::say(std::string text, bool error) {
    if (error) ui::PropertyGrid::refuse(text);   // 1.12.19 (la regle des cases) : un refus se dit sous la case
    message_ = std::move(text);
    status_->setMessage(message_, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

Id HmiStylesPane::selectedStyle() const { return builtin_.empty() && folders_ ? folders_->selectedItem() : kNoId; }

const hmi::Style* HmiStylesPane::currentStyle() const {
    if (!builtin_.empty()) return styles::builtin(builtin_);
    const Id id = selectedStyle();
    return id != kNoId ? doc_->project.style(id) : nullptr;
}

bool HmiStylesPane::locked() const {
    const hmi::Style* s = currentStyle();
    return s && s->builtin;
}

std::string HmiStylesPane::lockedBanner() const {
    return locked() ? "Style fourni : verrouill\xC3\xA9. \xC2\xAB Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 \xC2\xBB pour le modifier." : std::string{};
}

const hmi::Style* HmiStylesPane::editable(std::string* why) const {
    const hmi::Style* s = currentStyle();
    std::string e;
    if (!s) e = "choisis d'abord un style dans la liste";
    else if (s->builtin) e = "un style fourni ne se modifie pas : \xC2\xAB Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 \xC2\xBB en fait une copie \xC3\xA0 toi";
    if (!e.empty()) {
        if (why) *why = e;
        return nullptr;
    }
    return s;
}

void HmiStylesPane::selectStyle(Id id) {
    // Lot API 8 : un style demande (cree, duplique) que la recherche cache : elle s'efface.
    if (id != kNoId && !search_->text().empty() && folders_->rowOfItem(id) < 0 && doc_->project.style(id))
        search_->setText("");       // -> refresh()
    builtin_.clear();
    builtins_->selectModelRows({}, false);
    table_->selectModelRows({}, false);
    projectSel_ = id;
    folders_->selectItem(id);
    styleChanged();
}

bool HmiStylesPane::selectBuiltin(std::string_view name) {
    const hmi::Style* s = styles::builtin(name);
    if (!s) return false;
    if (std::find(builtinRows_.begin(), builtinRows_.end(), s->name) == builtinRows_.end() && !search_->text().empty())
        search_->setText("");       // -> refresh()
    builtin_ = s->name;
    projectSel_ = kNoId;
    table_->selectModelRows({}, false);
    for (std::size_t i = 0; i < builtinRows_.size(); ++i)
        if (builtinRows_[i] == builtin_) builtins_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    styleChanged();
    return true;
}

void HmiStylesPane::selectType(const std::string& type) {
    type_ = type;
    prop_.clear();
    if (type != styles::kAnyType && styles::knownType(doc_->project, type)) previewType_ = type;
    for (std::size_t i = 0; i < typeRows_.size(); ++i)
        if (typeRows_[i] == type_) types_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    rebuildProperties();
    rebuildPreviewTypes();
    updateButtons();
    invalidate();
}

void HmiStylesPane::selectProperty(const std::string& key) {
    prop_ = key;
    updateButtons();
    grid_->invalidate();
}

bool HmiStylesPane::matchesSearch(const hmi::Style& st) const {
    const ui::SearchQuery& query = search_->query();
    if (query.empty()) return true;
    std::vector<std::string> texts{st.name, st.description, st.folder};
    const auto addProps = [&](std::string_view type, const std::vector<hmi::Prop>& props) {
        texts.push_back(std::string(type) + " " + styles::typeLabel(type));
        for (const auto& pr : props) texts.push_back(pr.key + " " + propertyName(type, pr.key) + " " + pr.value);
    };
    if (!st.props.empty()) addProps(styles::kAnyType, st.props);
    for (const auto& t : st.targets) addProps(t.type, t.props);
    return query.matches(texts);
}

void HmiStylesPane::rebuildBuiltins() {
    const auto& p = doc_->project;
    builtinRows_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<ui::CellStyle>> cells;
    std::vector<std::string> tips;
    const auto& all = styles::builtins();
    for (std::size_t i = 0; i < all.size(); ++i) {
        const auto& s = all[i];
        if (!matchesSearch(s)) continue;
        builtinRows_.push_back(s.name);
        const bool hidden = p.styleByName(s.name) != nullptr;
        rows.push_back({s.name, std::to_string(styles::users(p, s.name)), {}});
        ui::CellStyle name;
        name.icon = ui::Icon::Lock;
        name.iconTone = ui::Tone::Muted;
        if (hidden) name.fgTone = ui::Tone::Muted;
        ui::CellStyle swatch;
        swatch.customIcon = static_cast<int>(i + 1);
        cells.push_back({name, {}, swatch});
        tips.push_back(s.name + " (style fourni, verrouill\xC3\xA9) : " + s.description
                       + (hidden ? "\nUn style du projet porte ce nom : c'est lui que les objets citent." : std::string{})
                       + "\n\xC2\xAB Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 \xC2\xBB en fait une copie modifiable.");
    }
    builtins_->setModel(std::make_shared<CellRows>(
        std::vector<std::string>{"Fournis \xE2\x80\x94 verrouill\xC3\xA9s", "Objets", "Couleurs"}, std::move(rows), std::move(cells), std::move(tips)));
    for (std::size_t i = 0; i < builtinRows_.size(); ++i)
        if (builtinRows_[i] == builtin_) builtins_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
}

void HmiStylesPane::refresh() {
    const auto& p = doc_->project;
    // Un nom en cours de frappe (Nouveau, Dupliquer) : la liste va etre refaite, sa ligne peut changer de rang - la case
    // se ferme sans rien ecrire (sinon elle renommerait plus tard le style de ce rang). Pendant sa propre ecriture, elle
    // est deja fermee.
    if (table_->cellEditing()) table_->finishCellEdit(false, {});
    rebuildBuiltins();
    std::vector<Id> shown;
    // Lot API 8 : la recherche - le nom, la description, les types, les valeurs, le dossier.
    for (const auto& st : p.styles)
        if (matchesSearch(st)) shown.push_back(st.id);
    shown_ = shown.size() + builtinRows_.size();
    table_->setHighlight(search_->text());
    builtins_->setHighlight(search_->text());
    search_->setCount(shown_, p.styles.size() + styles::builtins().size());
    folders_->rebuild(hmi::fold::List::Styles, shown, {"Du projet", "Objets", "Couleurs"},
                      [&p](Id id) -> std::vector<std::string> {
                          const auto* s = p.style(id);
                          if (!s) return {};
                          return {s->name, std::to_string(styles::users(p, s->name)), {}};
                      },
                      [](Id id, std::size_t col) {
                          ui::CellStyle cs;
                          // 1.12.27 : la pastille du style (son identifiant ; la table la fait dessiner)
                          if (col == 2) cs.customIcon = id > 0 && id < 0x7FFFFFFFu ? static_cast<int>(id) : 0;
                          return cs;
                      });
    if (message_.empty())
        status_->setMessage(std::to_string(p.styles.size()) + " style(s) du projet, " + std::to_string(styles::builtins().size())
                            + " fournis. Un objet cite un style par sa propri\xC3\xA9t\xC3\xA9 \xC2\xAB Style nomm\xC3\xA9 \xC2\xBB ; le style "
                              "impose ses propri\xC3\xA9t\xC3\xA9s aux objets de ses types.");
    // Le style du projet choisi, cache par la recherche puis remontre : de nouveau choisi.
    if (builtin_.empty() && projectSel_ != kNoId && folders_->selectedItem() == kNoId && folders_->selectedFolder().empty())
        if (const int row = folders_->rowOfItem(projectSel_); row >= 0) table_->selectModelRows({static_cast<ui::RowIndex>(row)}, false);
    if (!p.style(projectSel_)) projectSel_ = kNoId;
    styleKey_.clear();          // le style a pu changer sous le meme nom : ses types sont relus (le type choisi reste)
    styleChanged();
}

void HmiStylesPane::styleChanged() {
    const hmi::Style* s = currentStyle();
    const std::string key = !s ? std::string{} : s->builtin ? "fourni:" + s->name : "projet:" + std::to_string(s->id);
    const bool other = !styleKey_.empty() && key != styleKey_;
    styleKey_ = key.empty() ? std::string("-") : key;
    // Le type choisi reste s'il est encore au style ; sinon (un autre style) le premier.
    std::vector<std::string> types;
    if (s) {
        for (const auto& t : s->targets) types.push_back(t.type);
        if (!s->props.empty()) types.emplace_back(styles::kAnyType);
    }
    if (other || std::find(types.begin(), types.end(), type_) == types.end()) {
        type_ = types.empty() ? std::string{} : types.front();
        prop_.clear();
        if (s) {
            std::string first;
            for (const auto& t : types)
                if (t != styles::kAnyType && styles::knownType(doc_->project, t)) {
                    first = t;
                    break;
                }
            if (!first.empty()) previewType_ = first;
        }
    }
    if (s && !prop_.empty()) {
        const auto* props = propsOf(*s, type_);
        if (!props || std::none_of(props->begin(), props->end(), [&](const hmi::Prop& pr) { return pr.key == prop_; })) prop_.clear();
    }
    if (previewType_.empty() || !styles::knownType(doc_->project, previewType_) || previewType_ == styles::kAnyType) previewType_ = "Button";
    invalidateLayout();         // le bandeau d'un style fourni, la hauteur des fournis (la recherche)
    rebuildTypes();
    rebuildProperties();
    rebuildPreviewTypes();
    updateButtons();
    if (preview_) preview_->invalidate();
    invalidate();
}

void HmiStylesPane::rebuildTypes() {
    typeRows_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<ui::CellStyle>> cells;
    std::vector<std::string> tips;
    const auto& p = doc_->project;
    const hmi::Style* s = currentStyle();
    const auto add = [&](const std::string& type, std::size_t count) {
        typeRows_.push_back(type);
        const bool known = styles::knownType(p, type);
        std::string label = typeName(type);
        if (styles::isSymbolType(type)) label += " (utilisateur)";
        rows.push_back({label, std::to_string(count) + (count > 1 ? " propri\xC3\xA9t\xC3\xA9s" : " propri\xC3\xA9t\xC3\xA9")});
        ui::CellStyle name;
        if (const auto kind = kindOfType(type)) name.customIcon = static_cast<int>(styles::isSymbolType(type) ? HmiGlyph::Symbol : glyphFor(*kind)) + 1;
        else name.icon = ui::Icon::Settings;
        if (!known) name.fgTone = ui::Tone::Warning;
        ui::CellStyle n;
        n.fgTone = ui::Tone::Muted;
        cells.push_back({name, n});
        tips.push_back(type == styles::kAnyType
                           ? std::string("Tous les objets (un style d'avant la 1.23.0) : ces propri\xC3\xA9t\xC3\xA9s valent pour tout objet qui les a ; "
                                         "celles d'un type passent avant.")
                       : !known ? styles::typeLabel(type) + " : type inconnu (symbole supprim\xC3\xA9 ?) - il n'impose plus rien."
                                : "Les " + styles::typeLabel(type) + " qui citent le style prennent ces " + std::to_string(count) + " valeur(s).");
    };
    if (s) {
        for (const auto& t : s->targets) add(t.type, t.props.size());
        if (!s->props.empty()) add(std::string(styles::kAnyType), s->props.size());
    }
    types_->setModel(std::make_shared<CellRows>(std::vector<std::string>{"Type", "Propri\xC3\xA9t\xC3\xA9s"}, std::move(rows), std::move(cells),
                                                std::move(tips)));
    for (std::size_t i = 0; i < typeRows_.size(); ++i)
        if (typeRows_[i] == type_) types_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
}

void HmiStylesPane::rebuildProperties() {
    grid_->invalidate();
    const hmi::Style* s = currentStyle();
    if (!s) {
        grid_->clearProperties();
        return;
    }
    const auto& p = doc_->project;
    const bool fixed = s->builtin;
    const Id sid = s->id;
    std::vector<PG::Category> cats;
    PG::Category head;
    head.name = fixed ? "Style fourni (verrouill\xC3\xA9)" : "Style";
    {
        auto name = prop(kName, s->name, PG::ValueType::Text,
                         fixed ? std::function<bool(std::string_view)>{} : [this, sid](std::string_view v) { return renameStyle(sid, std::string(v)); },
                         "Unique, jamais celui d'un style fourni. Renomm\xC3\xA9, les objets qui le citent suivent.");
        name.locked = fixed;
        head.properties.push_back(std::move(name));
        auto desc = prop(kDescription, s->description, PG::ValueType::Text,
                         fixed ? std::function<bool(std::string_view)>{}
                               : [this, sid](std::string_view v) { return setStyleField(sid, "description", std::string(v)); });
        desc.locked = fixed;
        head.properties.push_back(std::move(desc));
        head.properties.push_back(prop(kUsers, std::to_string(styles::users(p, s->name)), PG::ValueType::ReadOnly, {}));
        const auto issues = styles::problems(p, *s);
        if (!issues.empty()) {
            std::string all;
            for (const auto& i : issues) all += (all.empty() ? "" : " ; ") + i;
            auto pb = prop(kProblems, all, PG::ValueType::ReadOnly, {});
            pb.valueTone = ui::Tone::Warning;
            head.properties.push_back(std::move(pb));
        }
    }
    cats.push_back(std::move(head));
    if (!type_.empty()) {
        PG::Category look;
        look.name = "Propri\xC3\xA9t\xC3\xA9s impos\xC3\xA9" "es \xE2\x80\x94 " + styles::typeLabel(type_);
        const auto* props = propsOf(*s, type_);
        std::set<std::string> taken{kName, kDescription, kUsers, kProblems};
        const std::string type = type_;
        for (const auto& pr : props ? *props : std::vector<hmi::Prop>{}) {
            const KeyKind k = keyKindOf(p, type, pr.key);
            std::string label = propertyName(type, pr.key);
            if (!taken.insert(label).second) label += " (" + pr.key + ")";
            const std::string key = pr.key;
            auto one = prop(label, pr.value, k.type,
                            fixed ? std::function<bool(std::string_view)>{}
                                  : [this, sid, type, key](std::string_view v) {
                                        prop_ = key;
                                        return setTypeValue(sid, type, key, std::string(v));
                                    },
                            (k.help.empty() ? std::string{} : k.help + "\n")
                                + "Impos\xC3\xA9" "e aux " + styles::typeLabel(type) + " qui citent le style ; chang\xC3\xA9" "e, ils la prennent, "
                                  "sauf ceux qui l'avaient chang\xC3\xA9" "e eux-m\xC3\xAAmes.",
                            k.choices);
            one.key = key;
            one.locked = fixed;
            look.properties.push_back(std::move(one));
        }
        cats.push_back(std::move(look));
    }
    grid_->setCategories(std::move(cats));
}

std::vector<std::string> HmiStylesPane::previewTypeChoices() const {
    std::vector<std::string> out;
    const auto& p = doc_->project;
    if (const hmi::Style* s = currentStyle())
        for (const auto& t : s->targets)
            if (styles::knownType(p, t.type) && std::find(out.begin(), out.end(), t.type) == out.end()) out.push_back(t.type);
    for (auto& t : styles::availableTypes(p))
        if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(std::move(t));
    return out;
}

void HmiStylesPane::rebuildPreviewTypes() {
    if (!previewTypes_) return;
    const hmi::Style* s = currentStyle();
    std::vector<ui::DropDown::Item> items;
    int at = -1;
    for (const auto& t : previewTypeChoices()) {
        const bool mine = s && styles::target(*s, t);
        if (t == previewType_) at = static_cast<int>(items.size());
        items.push_back({(mine ? std::string("\xE2\x9C\x93 ") : std::string{}) + typeName(t), t});
    }
    previewTypes_->setItems(std::move(items));
    previewTypes_->setSelectedIndex(at);
    if (preview_) preview_->invalidate();
}

void HmiStylesPane::setPreviewType(const std::string& type) {
    if (!styles::knownType(doc_->project, type) || type == styles::kAnyType) return;
    previewType_ = type;
    rebuildPreviewTypes();
}

void HmiStylesPane::updateButtons() {
    const bool can = editable(nullptr) != nullptr;
    addType_->setEnabled(can);
    removeType_->setEnabled(can && !type_.empty());
    addProp_->setEnabled(can && !type_.empty() && styles::knownType(doc_->project, type_));
    removeProp_->setEnabled(can && !type_.empty() && !prop_.empty());
}

void HmiStylesPane::beginRename(Id style) {
    if (const int row = folders_->rowOfItem(style); row >= 0) (void)table_->beginCellEdit(static_cast<ui::RowIndex>(row), 0);
}

std::vector<HmiStyleChooser::Entry> HmiStylesPane::typeChoices() const {
    std::vector<HmiStyleChooser::Entry> out;
    const hmi::Style* s = currentStyle();
    if (!s) return out;
    // 1.23.1 : RANGES COMME LA BIBLIOTHEQUE - ses categories dans son ordre (Formes, Commandes, Affichage...), chacune
    // dans l'ordre de ses tuiles ; un genre qu'aucune ne montre : Autres ; puis Mes symboles. Chacun sa petite icone (le
    // glyphe de sa tuile ; un symbole : celui des symboles).
    const auto cats = hmiPaletteCategories();
    std::map<hmi::Kind, std::pair<std::size_t, std::size_t>> place;   // le rang de sa categorie, son rang dedans
    for (std::size_t c = 0; c < cats.size(); ++c)
        for (std::size_t k = 0; k < cats[c].kinds.size(); ++k) place.emplace(cats[c].kinds[k], std::make_pair(c, k));
    struct Ranked {
        std::size_t            cat, pos, order;
        HmiStyleChooser::Entry entry;
    };
    std::vector<Ranked> ranked;
    std::size_t order = 0;
    for (const auto& t : styles::availableTypes(doc_->project)) {
        ++order;
        if (styles::target(*s, t)) continue;     // deja au style
        HmiStyleChooser::Entry e;
        e.value = t;
        if (styles::isSymbolType(t)) {
            e.label = styles::symbolName(t);
            e.group = kGroupSymbols;
            e.glyph = HmiGlyph::Symbol;
            ranked.push_back({cats.size() + 1, 0, order, std::move(e)});
            continue;
        }
        e.label = styles::typeLabel(t);
        const auto kind = hmi::kindFromKey(t);
        if (kind) e.glyph = glyphFor(*kind);
        const auto at = kind ? place.find(*kind) : place.end();
        if (at != place.end()) {
            e.group = cats[at->second.first].name;
            ranked.push_back({at->second.first, at->second.second, order, std::move(e)});
        } else {
            e.group = kGroupOthers;
            ranked.push_back({cats.size(), 0, order, std::move(e)});
        }
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        return std::tie(a.cat, a.pos, a.order) < std::tie(b.cat, b.pos, b.order);
    });
    for (auto& r : ranked) out.push_back(std::move(r.entry));
    return out;
}

std::vector<HmiStyleChooser::Entry> HmiStylesPane::propertyChoices() const {
    std::vector<HmiStyleChooser::Entry> out;
    const hmi::Style* s = currentStyle();
    if (!s || type_.empty()) return out;
    const auto& p = doc_->project;
    const auto* props = propsOf(*s, type_);
    // 1.23.1 (le client : << pareil ici avec type variable mais sans icone >>) : RANGEES COMME L'INSPECTEUR RANGE UN OBJET
    // NEUF DE CE TYPE - ses categories, dans son ordre (Position et taille, Apparence, la section du genre, Parametres
    // du symbole...) ; une cle qu'il ne montre pas : Autres. A droite, le genre de valeur qu'il ecrit au bout du nom
    // (nombre, couleur, booleen...), puis, plus discrete, la valeur d'un objet neuf.
    struct Seen {
        std::size_t cat, pos;
        std::string word;
    };
    std::vector<std::string> catNames;
    std::map<std::string, Seen> byKey, byName;
    if (const auto kind = kindOfType(type_)) {
        hmi::View tmp;
        hmi::Layer layer;
        layer.id = 1;
        tmp.layers.push_back(layer);
        hmi::Object o = hmi::makeObject(*kind, 1, {}, 0, 0, layer.id);
        if (styles::isSymbolType(type_)) o.set("symbol", styles::symbolName(type_));
        tmp.objects.push_back(std::move(o));
        std::size_t pos = 0;
        const std::function<void(const PG::Category&, std::size_t)> walk = [&](const PG::Category& c, std::size_t at) {
            for (const auto& pr : c.properties) {
                const Seen seen{at, pos++, inspectorWord(pr)};
                if (!pr.key.empty()) byKey.emplace(pr.key, seen);
                byName.emplace(plainLabel(pr.name), seen);
            }
            for (const auto& child : c.children) walk(child, at);
        };
        for (const auto& c : hmiPropertyCategories(tmp, {1}, nullptr, HmiPropertyCommits{}, &p.assets, &p)) {
            catNames.push_back(c.name);
            walk(c, catNames.size() - 1);
        }
    }
    const auto paramsAt = std::find(catNames.begin(), catNames.end(), std::string(kGroupParams));
    struct Ranked {
        std::size_t            cat, pos, order;
        HmiStyleChooser::Entry entry;
    };
    std::vector<Ranked> ranked;
    std::size_t order = 0;
    for (const auto& c : styles::propertiesOf(p, type_)) {
        ++order;
        if (props && std::any_of(props->begin(), props->end(), [&](const hmi::Prop& pr) { return pr.key == c.key; })) continue;
        const bool param = c.key.substr(0, styles::kParamPrefix.size()) == styles::kParamPrefix;
        HmiStyleChooser::Entry e;
        e.value = c.key;
        e.label = propertyName(type_, c.key);
        e.extra = c.defaultValue;
        const Seen* seen = nullptr;
        if (const auto k = byKey.find(c.key); k != byKey.end()) seen = &k->second;
        else if (const auto n = byName.find(plainLabel(e.label)); n != byName.end()) seen = &n->second;
        std::size_t cat = catNames.size() + 1, pos = 0;
        if (seen) {
            e.group = catNames[seen->cat];
            e.detail = seen->word;
            cat = seen->cat;
            pos = seen->pos;
        } else if (param) {
            e.group = kGroupParams;
            cat = paramsAt != catNames.end() ? static_cast<std::size_t>(paramsAt - catNames.begin()) : catNames.size();
            pos = static_cast<std::size_t>(-1);
        } else {
            e.group = kGroupOthers;
        }
        if (e.detail.empty()) e.detail = kindWord(keyKindOf(p, type_, c.key).type);
        ranked.push_back({cat, pos, order, std::move(e)});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        return std::tie(a.cat, a.pos, a.order) < std::tie(b.cat, b.pos, b.order);
    });
    for (auto& r : ranked) out.push_back(std::move(r.entry));
    return out;
}

void HmiStylesPane::openTypeChooser() {
    std::string why;
    if (!editable(&why)) {
        say(why, true);
        return;
    }
    auto entries = typeChoices();
    if (entries.empty()) {
        say("Le style vise d\xC3\xA9j\xC3\xA0 tous les types.", true);
        return;
    }
    chooserFor_ = 1;
    const auto b = bounds();
    // 1.23.1 : les categories en dossiers, la premiere seule depliee.
    chooser_->open("AJOUTER UN TYPE", std::move(entries), addType_->bounds(), {b.x, b.y, b.w, std::max(0.f, b.h - 24.f)},
                   {true, "type", "types"});
}

void HmiStylesPane::openPropertyChooser() {
    std::string why;
    if (!editable(&why)) {
        say(why, true);
        return;
    }
    if (type_.empty() || !styles::knownType(doc_->project, type_)) {
        say("Choisis d'abord un type d'objet du style (ou ajoute-en un).", true);
        return;
    }
    auto entries = propertyChoices();
    if (entries.empty()) {
        say(styles::typeLabel(type_) + " : le style impose d\xC3\xA9j\xC3\xA0 toutes ses propri\xC3\xA9t\xC3\xA9s.", true);
        return;
    }
    chooserFor_ = 2;
    const auto b = bounds();
    // 1.23.1 : les categories de l'inspecteur en dossiers, toutes depliees (une propriete se cherche vite).
    chooser_->open("AJOUTER UNE PROPRI\xC3\x89T\xC3\x89 \xE2\x80\x94 " + styles::typeLabel(type_), std::move(entries), addProp_->bounds(),
                   {b.x, b.y, b.w, std::max(0.f, b.h - 24.f)}, {false, "propri\xC3\xA9t\xC3\xA9", "propri\xC3\xA9t\xC3\xA9s"});
}

// ============================================== 1.23.1 : les menus du clic droit ===
std::vector<ui::PopupMenu::Item> HmiStylesPane::styleMenuItems() const {
    namespace cmd = ui::cmd;
    namespace fold = hmi::fold;
    std::vector<ui::PopupMenu::Item> items;
    const hmi::Style* s = currentStyle();
    const bool fixed = s && s->builtin;
    const std::string folder = !s && builtin_.empty() ? folders_->selectedFolder() : std::string{};
    const std::string none = "choisis d'abord un style (un clic droit sur sa ligne)";
    // 1.24.0 : plusieurs styles du projet choisis - << Retirer les 5 >> ; Dupliquer, Renommer, Ranger : un a la fois.
    const std::size_t chosen = builtin_.empty() && folders_ ? folders_->selectedCount() : 0;
    const bool many = chosen > 1;
    if (many) items.push_back(cmd::heading(std::to_string(chosen) + " lignes", "styles et dossiers"));
    else if (s) items.push_back(cmd::heading(s->name, fixed ? std::string("style fourni") : s->folder.empty() ? std::string("du projet") : s->folder));
    else if (!folder.empty()) items.push_back(cmd::heading(hmi::types::folderLeaf(folder), "dossier"));
    else items.push_back(cmd::heading("Styles"));
    items.push_back(cmd::item(cmd::Id::Ajouter, MenuNew, true, {}, "Nouveau style"));
    items.push_back(cmd::item(cmd::Id::Dupliquer, MenuDuplicate, s != nullptr && !many,
                              many ? several::kOneAtATime
                                   : folder.empty() ? none : std::string("un dossier ne se duplique pas : choisis un de ses styles"),
                              "Dupliquer \xC3\xA0 partir de\xE2\x80\xA6"));
    const bool own = s && !fixed;
    items.push_back(cmd::item(cmd::Id::Renommer, MenuRename, (own || !folder.empty()) && !many,
                              many  ? several::kOneAtATime
                              : fixed ? std::string("un style fourni ne se renomme pas : \xC2\xAB Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 \xC2\xBB en fait "
                                                  "une copie \xC3\xA0 toi")
                                    : none,
                              folder.empty() ? std::string{} : std::string("Renommer le dossier")));
    items.push_back(cmd::item(cmd::Id::Retirer, MenuRemove, own || !folder.empty() || many,
                              fixed ? std::string("un style fourni ne se retire pas : il n'est pas dans le projet, et reste verrouill\xC3\xA9") : none,
                              many ? several::label("Retirer", chosen) : folder.empty() ? std::string{} : std::string("Retirer le dossier")));
    // Ranger dans un dossier > la racine, chaque dossier des styles (celui du style coche), Nouveau dossier...
    ui::PopupMenu::Item put;
    put.label = "Ranger dans un dossier";
    put.icon = ui::Icon::Folder;
    put.id = MenuFolder;
    menuFolders_.clear();
    if (own && !many) {
        const auto& p = doc_->project;
        const std::string at = fold::folderOf(p, fold::List::Styles, s->id);
        const auto entry = [&](std::string label, int id, bool here) {
            ui::PopupMenu::Item it;
            it.label = std::move(label);
            it.id = id;
            it.radio = true;
            it.checked = here;
            it.enabled = !here;
            if (here) it.disabledReason = "le style y est d\xC3\xA9j\xC3\xA0";
            return it;
        };
        put.children.push_back(entry("\xC3\x80 la racine", MenuFolderRoot, at.empty()));
        menuFolders_ = fold::allFolders(p, fold::List::Styles);
        for (std::size_t i = 0; i < menuFolders_.size(); ++i)
            put.children.push_back(entry(menuFolders_[i], MenuFolderAt + static_cast<int>(i), fold::sameFolder(menuFolders_[i], at)));
        put.children.push_back(cmd::separator());
        ui::PopupMenu::Item fresh;
        fresh.label = "Nouveau dossier\xE2\x80\xA6";
        fresh.icon = ui::Icon::Plus;
        fresh.id = MenuFolderNew;
        put.children.push_back(std::move(fresh));
    } else {
        put.enabled = false;
        put.disabledReason = many ? several::kOneAtATime : fixed ? std::string("un style fourni reste en t\xC3\xAAte de la liste, hors des dossiers")
                           : !folder.empty() ? std::string("un dossier se range en le glissant sur un autre")
                                             : none;
    }
    items.push_back(std::move(put));
    items.push_back(cmd::separator());
    items.push_back(cmd::item(cmd::Id::CopierNom, MenuCopyName, s != nullptr || !folder.empty(), none));
    return items;
}

std::vector<ui::PopupMenu::Item> HmiStylesPane::typeMenuItems() const {
    namespace cmd = ui::cmd;
    std::vector<ui::PopupMenu::Item> items;
    const hmi::Style* s = currentStyle();
    std::string why;
    const bool can = editable(&why) != nullptr;
    items.push_back(cmd::heading(type_.empty() ? std::string("Types d'objets") : styles::typeLabel(type_), s ? s->name : std::string{}));
    items.push_back(cmd::item(cmd::Id::Ajouter, MenuAddType, can, why, "Ajouter un type\xE2\x80\xA6"));
    items.push_back(cmd::item(cmd::Id::Retirer, MenuRemoveType, can && !type_.empty(), can ? std::string("choisis d'abord un type") : why,
                              "Retirer le type"));
    // L'apercu ne change pas le style : un style fourni aussi. Coche : le type y est deja.
    ui::PopupMenu::Item look;
    look.label = "Choisir pour l'aper\xC3\xA7u";
    look.id = MenuPreviewType;
    look.checkable = true;
    look.checked = !type_.empty() && previewType_ == type_;
    look.enabled = !type_.empty() && type_ != styles::kAnyType && styles::knownType(doc_->project, type_);
    if (!look.enabled)
        look.disabledReason = type_.empty() ? std::string("choisis d'abord un type")
                            : type_ == styles::kAnyType
                                ? std::string("\xC2\xAB Tous les objets \xC2\xBB ne se dessine pas : choisis un type dans la liste Type :")
                                : std::string("type inconnu (un symbole supprim\xC3\xA9 ?) : il ne se dessine pas");
    items.push_back(std::move(look));
    return items;
}

std::vector<ui::PopupMenu::Item> HmiStylesPane::propertyMenuItems() const {
    namespace cmd = ui::cmd;
    std::vector<ui::PopupMenu::Item> items;
    std::string why;
    const bool can = editable(&why) != nullptr;
    const bool typed = !type_.empty() && styles::knownType(doc_->project, type_);
    items.push_back(cmd::heading(prop_.empty() ? std::string("Propri\xC3\xA9t\xC3\xA9s impos\xC3\xA9" "es") : propertyName(type_, prop_),
                                 type_.empty() ? std::string{} : styles::typeLabel(type_)));
    items.push_back(cmd::item(cmd::Id::Ajouter, MenuAddProperty, can && typed,
                              !can ? why : std::string("choisis d'abord un type d'objet du style (ou ajoute-en un)"),
                              "Ajouter une propri\xC3\xA9t\xC3\xA9\xE2\x80\xA6"));
    items.push_back(cmd::item(cmd::Id::Retirer, MenuRemoveProperty, can && !type_.empty() && !prop_.empty(),
                              !can ? why : std::string("un clic droit sur une propri\xC3\xA9t\xC3\xA9 impos\xC3\xA9" "e la vise"),
                              "Retirer la propri\xC3\xA9t\xC3\xA9"));
    return items;
}

void HmiStylesPane::openPropertyMenu(gfx::Point at) {
    propMenu_->setItems(propertyMenuItems());
    propMenu_->openInWindow(at);
}

void HmiStylesPane::menuChosen(int id) {
    const Id sel = selectedStyle();
    const hmi::Style* s = currentStyle();
    switch (id) {
        case MenuNew: tools_->triggered->emit(ActNew); return;
        case MenuDuplicate:
            if (s) tools_->triggered->emit(ActDuplicate);
            return;
        case MenuRename:
            if (sel != kNoId) beginRename(sel);
            else if (s && s->builtin) tools_->triggered->emit(ActRename);      // il dit pourquoi
            else if (builtin_.empty() && !folders_->selectedFolder().empty()) renameFolderRow(folders_->selectedFolder());
            return;
        case MenuRemove:
            if (s && s->builtin) say("Un style fourni ne se retire pas : il n'est pas dans le projet, et reste verrouill\xC3\xA9.", true);
            else if (tools_->isEnabled(ActRemove)) tools_->triggered->emit(ActRemove);
            return;
        case MenuCopyName: {
            const std::string name = s ? s->name : builtin_.empty() ? hmi::types::folderLeaf(folders_->selectedFolder()) : std::string{};
            if (name.empty()) return;
            ui::setClipboardText(name);
            say("Nom copi\xC3\xA9 : " + name);
            return;
        }
        case MenuAddType: openTypeChooser(); return;
        case MenuRemoveType: (void)removeType(type_); return;
        case MenuPreviewType:
            if (!type_.empty() && type_ != styles::kAnyType && styles::knownType(doc_->project, type_)) {
                setPreviewType(type_);
                say("Aper\xC3\xA7u : " + styles::typeLabel(type_) + ".");
            }
            return;
        case MenuAddProperty: openPropertyChooser(); return;
        case MenuRemoveProperty: (void)removeProperty(prop_); return;
        case MenuFolderRoot:
            if (sel != kNoId && folders_->moveItems({sel}, {})) selectStyle(sel);
            return;
        case MenuFolderNew:
            if (sel != kNoId) (void)newFolderWith(sel);
            return;
        default: break;
    }
    if (id >= MenuFolderAt && sel != kNoId) {
        const auto i = static_cast<std::size_t>(id - MenuFolderAt);
        if (i < menuFolders_.size() && folders_->moveItems({sel}, menuFolders_[i])) selectStyle(sel);
    }
}

bool HmiStylesPane::newFolderWith(Id style) {
    namespace fold = hmi::fold;
    const auto& p = doc_->project;
    const hmi::Style* s = p.style(style);
    if (!s) return false;
    const std::string name = s->name;
    // Dans le dossier du style (comme le bouton Dossier) : "Nouveau dossier", "Nouveau dossier 2"...
    const std::string parent = fold::folderOf(p, fold::List::Styles, style);
    const auto all = fold::allFolders(p, fold::List::Styles);
    std::string path;
    for (int i = 1; i < 1000; ++i) {
        const std::string leaf = i == 1 ? std::string("Nouveau dossier") : "Nouveau dossier " + std::to_string(i);
        path = parent.empty() ? leaf : parent + "/" + leaf;
        if (std::none_of(all.begin(), all.end(), [&](const std::string& f) { return fold::sameFolder(f, path); })) break;
    }
    bool ok = false;
    std::string why;
    auto cmd = hmi::changeProject(doc_, "Ranger " + name + " dans un nouveau dossier", [&](hmi::Project& pr) {
        ok = fold::addFolder(pr, fold::List::Styles, path, &why) && fold::moveToFolder(pr, fold::List::Styles, {style}, path) > 0;
    });
    if (!ok) {
        say("Dossier non cr\xC3\xA9\xC3\xA9 : " + (why.empty() ? std::string("rien n'a chang\xC3\xA9") : why), true);
        return false;
    }
    if (cmd) apply_(std::move(cmd));
    selectStyle(style);
    renameFolderRow(path);
    say("\xC2\xAB " + name + " \xC2\xBB rang\xC3\xA9 dans \xC2\xAB " + path + " \xC2\xBB : tape le nom du dossier (Entr\xC3\xA9" "e) - Ctrl+Z d\xC3\xA9" "fait tout.");
    return true;
}

// Le nom d'un dossier se tape dans sa case (le choix ne bouge pas : le style range reste choisi).
void HmiStylesPane::renameFolderRow(const std::string& path) {
    const auto& rows = folders_->rows();
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].folder && hmi::fold::sameFolder(rows[i].path, path)) {
            (void)table_->beginCellEdit(static_cast<ui::RowIndex>(i), 0);
            return;
        }
}

Id HmiStylesPane::addStyle(const std::string& name) {
    Id made = kNoId;
    std::string madeName;
    auto cmd = hmi::changeProject(doc_, "Nouveau style", [&](hmi::Project& p) {
        hmi::Style s;
        s.id = p.allocate();
        s.name = styles::uniqueName(p, trimmed(name));
        made = s.id;
        madeName = s.name;
        p.styles.push_back(std::move(s));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("Style " + madeName + " cr\xC3\xA9\xC3\xA9 : ajoute-lui un type d'objet (+ Ajouter un type\xE2\x80\xA6) et ce qu'il lui impose ; "
        "un objet le cite par \xC2\xAB Style nomm\xC3\xA9 \xC2\xBB.");
    return made;
}

Id HmiStylesPane::duplicateFrom(const std::string& name, const std::string& newName) {
    const hmi::Style* src = styles::find(doc_->project, name);
    if (!src) {
        say("Style introuvable : " + name, true);
        return kNoId;
    }
    const hmi::Style from = *src;
    Id made = kNoId;
    std::string madeName;
    auto cmd = hmi::changeProject(doc_, "Dupliquer \xC3\xA0 partir de " + from.name, [&](hmi::Project& p) {
        hmi::Style s = styles::duplicate(p, from, trimmed(newName));
        made = s.id;
        madeName = s.name;
        p.styles.push_back(std::move(s));
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    say("\xC2\xAB " + madeName + " \xC2\xBB : une copie modifiable de \xC2\xAB " + from.name + " \xC2\xBB" + (from.builtin ? " (style fourni)" : "")
        + " - renomme-la, puis r\xC3\xA8gle ses types et leurs propri\xC3\xA9t\xC3\xA9s (Ctrl+Z la retire).");
    return made;
}

Id HmiStylesPane::duplicateStyle(Id style) {
    const hmi::Style* s = doc_->project.style(style);
    return s ? duplicateFrom(s->name) : kNoId;
}

bool HmiStylesPane::removeStyle(Id style) {
    std::string name;
    std::size_t freed = 0;
    auto cmd = hmi::changeProject(doc_, "Retirer le style", [&](hmi::Project& p) {
        const hmi::Style* s = styleById(p, style);
        if (!s) return;
        name = s->name;
        freed = styles::forget(p, name);
    });
    if (!cmd) return false;
    apply_(std::move(cmd));
    refresh();
    say("Style " + name + " retir\xC3\xA9 : " + std::to_string(freed) + " objet(s) gardent leurs valeurs (Ctrl+Z le rend).");
    return true;
}

bool HmiStylesPane::renameStyle(Id style, const std::string& name, std::string* why) { return setStyleField(style, "nom", name, why); }

bool HmiStylesPane::setStyleField(Id style, const std::string& key, const std::string& raw, std::string* why) {
    const std::string value = trimmed(raw);
    std::size_t followed = 0;
    std::string shownName;
    const auto fail = [&](std::string e) {
        if (why) *why = e;
        say(e, true);
        return false;
    };
    const hmi::Style* current = doc_->project.style(style);
    if (!current) return fail(style == kNoId && locked() ? std::string("un style fourni ne se modifie pas") : std::string("style introuvable"));
    if (key == "nom") {
        if (value.empty()) return fail("un style a un nom");
        if (styles::builtin(value)) return fail("\xC2\xAB " + value + " \xC2\xBB est le nom d'un style fourni : choisis-en un autre");
        if (!styles::nameFree(doc_->project, value, style)) return fail("'" + value + "' existe d\xC3\xA9j\xC3\xA0");
    } else if (key != "description") {
        // Une propriete : celle du type choisi (ce style etant le style choisi), sinon du premier type qui la porte, sinon
        // de tous les objets.
        std::string type;
        if (style == selectedStyle() && !type_.empty()) type = type_;
        for (const auto& t : current->targets)
            if (type.empty() && std::any_of(t.props.begin(), t.props.end(), [&](const hmi::Prop& pr) { return pr.key == key; })) type = t.type;
        if (type.empty()) type = std::string(styles::kAnyType);
        if (!value.empty()) return setTypeValue(style, type, key, value, why);
        const auto* props = propsOf(*current, type);
        if (!props || std::none_of(props->begin(), props->end(), [&](const hmi::Prop& pr) { return pr.key == key; })) return true;
        return dropKey(style, type, key, why);
    }
    auto cmd = hmi::changeProject(doc_, "Style : " + key, [&](hmi::Project& p) {
        hmi::Style* s = styleById(p, style);
        if (!s) return;
        const hmi::Style before = *s;
        if (key == "nom") s->name = value;
        else s->description = value;
        const hmi::Style after = *s;
        shownName = after.name;
        followed = styles::propagate(p, before, after);
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    if (key == "nom") say("Style renomm\xC3\xA9 " + shownName + " : " + std::to_string(followed) + " objet(s) le citent sous ce nom (Ctrl+Z pour revenir).");
    else say(shownName + " : description chang\xC3\xA9" "e (Ctrl+Z pour revenir).");
    return true;
}

bool HmiStylesPane::setTypeValue(Id style, const std::string& type, const std::string& key, const std::string& raw, std::string* why) {
    std::string value = trimmed(raw);
    const auto fail = [&](std::string e) {
        if (why) *why = e;
        say(e, true);
        return false;
    };
    const hmi::Style* current = doc_->project.style(style);
    if (!current) return fail(styles::builtin(builtin_) ? std::string("un style fourni ne se modifie pas") : std::string("style introuvable"));
    if (type != styles::kAnyType && !styles::knownType(doc_->project, type)) return fail(styles::typeLabel(type) + " : type inconnu");
    const KeyKind k = keyKindOf(doc_->project, type, key);
    const std::string label = propertyName(type, key);
    std::string bad;
    if (!cleanValue(k, key, value, bad)) return fail(label + " : " + bad);
    const std::string name = current->name;
    std::size_t followed = 0, kept = 0;
    auto cmd = hmi::changeProject(doc_, "Style " + name + " : " + label, [&](hmi::Project& p) {
        hmi::Style* s = styleById(p, style);
        if (!s) return;
        const hmi::Style before = *s;
        auto& props = type == styles::kAnyType ? s->props : styles::targetFor(*s, type).props;
        const auto it = std::find_if(props.begin(), props.end(), [&](const hmi::Prop& x) { return x.key == key; });
        if (it != props.end()) it->value = value;
        else props.push_back({key, value, ""});
        const hmi::Style after = *s;
        const auto was = citedValues(p, name, key);
        const auto brokenBefore = brokenOn(p, name, key);
        (void)styles::propagate(p, before, after);
        const auto now = citedValues(p, name, key);
        for (const auto& [id, v] : now)
            if (const auto w = was.find(id); w != was.end() && w->second != v) ++followed;
        for (const Id id : brokenOn(p, name, key)) kept += brokenBefore.count(id) ? 0u : 1u;
    });
    prop_ = key;
    if (cmd) apply_(std::move(cmd));
    refresh();
    say(name + " \xC2\xB7 " + styles::typeLabel(type) + " : " + label + " = " + (value.empty() ? std::string("(vide)") : value) + " \xE2\x80\x94 "
        + std::to_string(followed) + " objet(s) suivent"
        + (kept ? " ; " + std::to_string(kept) + " l'avai(en)t chang\xC3\xA9" "e : elle reste la leur (bris\xC3\xA9" "e)" : std::string{})
        + " (Ctrl+Z pour revenir).");
    return true;
}

bool HmiStylesPane::addType(const std::string& type, std::string* why) {
    const auto fail = [&](std::string e) {
        if (why) *why = e;
        say(e, true);
        return false;
    };
    std::string e;
    const hmi::Style* s = editable(&e);
    if (!s) return fail(e);
    if (type == styles::kAnyType || !styles::knownType(doc_->project, type)) return fail(styles::typeLabel(type) + " : pas un type qu'on peut viser");
    if (styles::target(*s, type)) return fail(styles::typeLabel(type) + " : d\xC3\xA9j\xC3\xA0 au style");
    const Id sid = s->id;
    auto cmd = hmi::changeProject(doc_, "Style " + s->name + " : ajouter " + styles::typeLabel(type), [&](hmi::Project& p) {
        if (hmi::Style* st = styleById(p, sid)) (void)styles::targetFor(*st, type);
    });
    if (!cmd) return fail("rien n'a chang\xC3\xA9");
    apply_(std::move(cmd));
    type_ = type;
    prop_.clear();
    previewType_ = type;
    refresh();
    say(styles::typeLabel(type) + " ajout\xC3\xA9 au style : choisis les propri\xC3\xA9t\xC3\xA9s qu'il lui impose (+ Ajouter une propri\xC3\xA9t\xC3\xA9\xE2\x80\xA6) - "
        "un type sans propri\xC3\xA9t\xC3\xA9 n'impose rien (il n'est pas enregistr\xC3\xA9).");
    return true;
}

bool HmiStylesPane::removeType(const std::string& type, std::string* why) {
    const auto fail = [&](std::string e) {
        if (why) *why = e;
        say(e, true);
        return false;
    };
    std::string e;
    const hmi::Style* s = editable(&e);
    if (!s) return fail(e);
    if (type.empty() || !propsOf(*s, type) || (type == styles::kAnyType && s->props.empty())) return fail("choisis un type du style");
    const Id sid = s->id;
    std::size_t kept = 0;
    auto cmd = hmi::changeProject(doc_, "Style " + s->name + " : retirer " + styles::typeLabel(type), [&](hmi::Project& p) {
        hmi::Style* st = styleById(p, sid);
        if (!st) return;
        const hmi::Style before = *st;
        if (type == styles::kAnyType) st->props.clear();
        else std::erase_if(st->targets, [&](const hmi::StyleTarget& t) { return t.type == type; });
        const hmi::Style after = *st;
        (void)styles::propagate(p, before, after);
        // Les objets de ce type qui citent le style : ils gardent leurs valeurs (plus imposees).
        for (const auto& v : p.views)
            for (const auto& o : v.objects)
                if (upperAscii(styles::citedStyle(o)) == upperAscii(after.name) && (type == styles::kAnyType || styles::typeOf(o) == type)) ++kept;
    });
    if (!cmd) return fail("rien n'a chang\xC3\xA9");
    apply_(std::move(cmd));
    const std::string gone = type;
    type_.clear();
    prop_.clear();
    refresh();
    say(styles::typeLabel(gone) + " retir\xC3\xA9 du style : " + std::to_string(kept) + " objet(s) gardent leurs valeurs (Ctrl+Z le rend).");
    return true;
}

bool HmiStylesPane::addProperty(const std::string& key, std::string* why) {
    const auto fail = [&](std::string e) {
        if (why) *why = e;
        say(e, true);
        return false;
    };
    std::string e;
    const hmi::Style* s = editable(&e);
    if (!s) return fail(e);
    if (type_.empty() || !styles::knownType(doc_->project, type_)) return fail("choisis d'abord un type d'objet du style");
    const auto choices = styles::propertiesOf(doc_->project, type_);
    const auto c = std::find_if(choices.begin(), choices.end(), [&](const styles::PropChoice& pc) { return pc.key == key; });
    if (c == choices.end()) return fail(styles::typeLabel(type_) + " n'a pas de propri\xC3\xA9t\xC3\xA9 " + key);
    if (const auto* props = propsOf(*s, type_); props && std::any_of(props->begin(), props->end(), [&](const hmi::Prop& pr) { return pr.key == key; }))
        return fail(propertyName(type_, key) + " : d\xC3\xA9j\xC3\xA0 impos\xC3\xA9" "e");
    const KeyKind k = keyKindOf(doc_->project, type_, key);
    const std::string value = startValue(k, c->defaultValue);
    const std::string type = type_;
    const Id sid = s->id;
    const std::string name = s->name;
    std::size_t followed = 0;
    auto cmd = hmi::changeProject(doc_, "Style " + name + " : imposer " + propertyName(type, key), [&](hmi::Project& p) {
        hmi::Style* st = styleById(p, sid);
        if (!st) return;
        const hmi::Style before = *st;
        auto& props = type == styles::kAnyType ? st->props : styles::targetFor(*st, type).props;
        props.push_back({key, value, ""});
        const hmi::Style after = *st;
        const auto was = citedValues(p, name, key);
        (void)styles::propagate(p, before, after);
        for (const auto& [id, v] : citedValues(p, name, key))
            if (const auto w = was.find(id); w != was.end() && w->second != v) ++followed;
    });
    if (!cmd) return fail("rien n'a chang\xC3\xA9");
    apply_(std::move(cmd));
    prop_ = key;
    refresh();
    say(propertyName(type, key) + " impos\xC3\xA9" "e aux " + styles::typeLabel(type) + " : " + (value.empty() ? std::string("(vide)") : value)
        + " \xE2\x80\x94 " + std::to_string(followed) + " objet(s) suivent. Change sa valeur dans la liste (Ctrl+Z la retire).");
    return true;
}

bool HmiStylesPane::removeProperty(const std::string& key, std::string* why) {
    std::string e;
    const hmi::Style* s = editable(&e);
    if (!s) {
        if (why) *why = e;
        say(e, true);
        return false;
    }
    return dropKey(s->id, type_, key, why);
}

bool HmiStylesPane::dropKey(Id style, const std::string& type, const std::string& key, std::string* why) {
    const auto fail = [&](std::string e) {
        if (why) *why = e;
        say(e, true);
        return false;
    };
    const hmi::Style* s = doc_->project.style(style);
    if (!s) return fail("style introuvable");
    const auto* props = propsOf(*s, type);
    if (key.empty() || !props || std::none_of(props->begin(), props->end(), [&](const hmi::Prop& pr) { return pr.key == key; }))
        return fail("choisis une propri\xC3\xA9t\xC3\xA9 impos\xC3\xA9" "e (un clic sur sa ligne)");
    auto cmd = hmi::changeProject(doc_, "Style " + s->name + " : ne plus imposer " + propertyName(type, key), [&](hmi::Project& p) {
        hmi::Style* st = styleById(p, style);
        if (!st) return;
        const hmi::Style before = *st;
        auto& list = type == styles::kAnyType ? st->props : styles::targetFor(*st, type).props;
        std::erase_if(list, [&](const hmi::Prop& pr) { return pr.key == key; });
        const hmi::Style after = *st;
        (void)styles::propagate(p, before, after);
    });
    if (!cmd) return fail("rien n'a chang\xC3\xA9");
    apply_(std::move(cmd));
    if (prop_ == key) prop_.clear();
    refresh();
    say(propertyName(type, key) + " : le style ne l'impose plus aux " + styles::typeLabel(type) + " - les objets gardent leur valeur (Ctrl+Z pour revenir).");
    return true;
}

void HmiStylesPane::onLayout() {
    // 1.23.1 : les deux onglets en haut ; le reste dessous.
    auto b = bounds();
    {
        const float tabH = 30.f;
        tabRects_[0] = {b.x + 8.f, b.y + 2.f, 150.f, tabH - 2.f};
        tabRects_[1] = {b.x + 8.f + 152.f, b.y + 2.f, 230.f, tabH - 2.f};
        b.y += tabH;
        b.h = std::max(0.f, b.h - tabH);
        if (colors_) colors_->setBounds(b);
        if (colorsShown_) return;
    }
    const float bar = 38.f, statusH = 24.f, head = 26.f, pad = 8.f;
    const float searchW = std::clamp(b.w * 0.3f, 180.f, 340.f);
    tools_->setBounds({b.x, b.y, std::max(0.f, b.w - searchW - 12.f), bar});
    search_->setBounds({b.x + b.w - searchW - 8.f, b.y + 5.f, searchW, 28.f});
    status_->setBounds({b.x, b.y + b.h - statusH, b.w, statusH});
    const float top = b.y + bar, bottom = b.y + b.h - statusH;
    const float h = std::max(0.f, bottom - top);
    const float w1 = std::clamp(b.w * 0.24f, 220.f, 320.f);
    const float w2 = std::clamp(b.w * 0.22f, 200.f, 300.f);
    colStyles_ = {b.x, top, w1, h};
    colTypes_ = {b.x + w1, top, w2, h};
    colProps_ = {b.x + w1 + w2, top, std::max(0.f, b.w - w1 - w2), h};
    // Les styles : les fournis (toutes leurs lignes), puis ceux du projet.
    const float fixedH = std::min(h * 0.45f, 28.f + 24.f * static_cast<float>(std::max<std::size_t>(1, builtinRows_.size())) + 2.f);
    builtins_->setBounds({colStyles_.x, top + head, w1 - 1.f, fixedH});
    table_->setBounds({colStyles_.x, top + head + fixedH + 4.f, w1 - 1.f, std::max(0.f, h - head - fixedH - 4.f)});
    // Les types : la liste, puis ses deux boutons.
    const float btn = 30.f;
    const float typesH = std::max(0.f, h - head - 2.f * (btn + pad) - pad);
    types_->setBounds({colTypes_.x, top + head, w2 - 1.f, typesH});
    addType_->setBounds({colTypes_.x + pad, top + head + typesH + pad, w2 - 2.f * pad - 1.f, btn});
    removeType_->setBounds({colTypes_.x + pad, top + head + typesH + 2.f * pad + btn, w2 - 2.f * pad - 1.f, btn});
    // Les proprietes : le bandeau d'un style fourni, la fiche, ses deux boutons ; dessous, l'apercu.
    const float bannerH = locked() ? 34.f : 0.f;
    banner_ = {colProps_.x + pad, top + head + 4.f, std::max(0.f, colProps_.w - 2.f * pad), bannerH > 0.f ? bannerH - 8.f : 0.f};
    const float previewH = std::clamp(h * 0.42f, 150.f, 300.f);
    const float gridTop = top + head + bannerH;
    const float gridH = std::max(0.f, h - head - bannerH - previewH - btn - 2.f * pad);
    grid_->setBounds({colProps_.x, gridTop, colProps_.w, gridH});
    const float half = std::max(0.f, (colProps_.w - 3.f * pad) / 2.f);
    addProp_->setBounds({colProps_.x + pad, gridTop + gridH + pad, std::min(260.f, half), btn});
    removeProp_->setBounds({colProps_.x + 2.f * pad + std::min(260.f, half), gridTop + gridH + pad, std::min(260.f, half), btn});
    preview_->setBounds({colProps_.x, bottom - previewH, colProps_.w, previewH});
}

void HmiStylesPane::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto font = ctx.theme.font.smallUi;
    ctx.r.fillRect(bounds(), c.panelBg);
    {
        // 1.23.1 : les deux onglets - Styles [n], Couleurs semantiques [n] ; celui qui est choisi sur le fond du volet.
        const auto b = bounds();
        ctx.r.fillRect({b.x, b.y, b.w, 30.f}, c.headerBg);
        ctx.r.fillRect({b.x, b.y + 29.f, b.w, 1.f}, c.border);
        const auto& p = doc_->project;
        const std::string labels[2] = {"Styles  " + std::to_string(styles::all(p).size()),
                                       "Couleurs s\xC3\xA9mantiques  " + std::to_string(hmi::semantic::all(p).size())};
        for (int t = 0; t < 2; ++t) {
            const gfx::Rect r = tabRects_[t];
            const bool on = (t == 1) == colorsShown_;
            if (on) {
                ctx.r.fillRect({r.x, r.y, r.w, r.h + 1.f}, c.panelBg);
                ctx.r.fillRect({r.x, r.y, r.w, 2.f}, c.accent);
            }
            const auto font = on ? ctx.theme.font.uiBold : ctx.theme.font.ui;
            ctx.r.drawText({r.x + 12.f, r.y + (r.h - ctx.r.lineHeight(font)) * 0.5f}, labels[t], font, on ? c.text : c.textMuted);
        }
        if (colorsShown_) return;
    }
    const float head = 26.f;
    const auto heading = [&](const gfx::Rect& col, const std::string& left, const std::string& right) {
        const gfx::Rect strip{col.x, col.y, col.w, head};
        ctx.r.fillRect(strip, c.headerBg);
        ctx.r.fillRect({strip.x, strip.bottom() - 1.f, strip.w, 1.f}, c.border);
        float rightW = 0.f;
        if (!right.empty()) {
            const std::string r = simmark::fitText(ctx.r, right, font, col.w * 0.45f);
            rightW = ctx.r.measure(r, font).width;
            ctx.r.drawText({col.right() - 10.f - rightW, col.y + 6.f}, r, font, c.textMuted);
        }
        ctx.r.drawText({col.x + 10.f, col.y + 6.f}, simmark::fitText(ctx.r, left, font, col.w - rightW - 28.f), font, c.textMuted);
    };
    heading(colStyles_, "STYLES", std::to_string(shown_));
    heading(colTypes_, "TYPES D'OBJETS DU STYLE", std::to_string(typeRows_.size()));
    heading(colProps_, type_.empty() ? std::string("PROPRI\xC3\x89T\xC3\x89S IMPOS\xC3\x89" "ES")
                                     : "PROPRI\xC3\x89T\xC3\x89S IMPOS\xC3\x89" "ES \xE2\x80\x94 " + styles::typeLabel(type_),
            "n'importe quelle propri\xC3\xA9t\xC3\xA9 du type");
    // Les colonnes : un trait entre elles.
    ctx.r.fillRect({colTypes_.x - 1.f, colTypes_.y, 1.f, colTypes_.h}, c.border);
    ctx.r.fillRect({colProps_.x - 1.f, colProps_.y, 1.f, colProps_.h}, c.border);
    // Un style fourni : le bandeau.
    if (const std::string text = lockedBanner(); !text.empty() && banner_.h > 0.f) {
        ctx.r.fillRoundedRect(banner_, c.accent.withAlpha(40), 4.f);
        ctx.r.strokeRect(banner_, c.accent.withAlpha(120), 1.f);
        ui::drawIcon(ctx.r, ui::Icon::Lock, {banner_.x + 8.f, banner_.y + (banner_.h - 14.f) / 2.f, 14.f, 14.f}, c.text);
        ctx.r.drawText({banner_.x + 30.f, banner_.y + (banner_.h - ctx.r.lineHeight(font)) / 2.f},
                       simmark::fitText(ctx.r, text, font, banner_.w - 40.f), font, c.text);
    }
    // Sans style choisi : ce qu'il faut faire, dans la colonne des types.
    if (!currentStyle() && colTypes_.w > 40.f)
        ctx.r.drawText({colTypes_.x + 10.f, colTypes_.y + head + 34.f},
                       simmark::fitText(ctx.r, "Choisis un style \xC3\xA0 gauche.", font, colTypes_.w - 20.f), font, c.textMuted);
}

// ===================================================== rechercher / remplacer ===
HmiFindPane::HmiFindPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto f = std::make_unique<ui::InputText>(base + ".find");
    f->setPlaceholder("Rechercher : Armoires[0], Pompe_Marche, #2F6FD6...");
    find_ = &static_cast<ui::InputText&>(addChild(std::move(f)));
    auto r = std::make_unique<ui::InputText>(base + ".replace");
    r->setPlaceholder("Remplacer par (vide : effacer)");
    replace_ = &static_cast<ui::InputText&>(addChild(std::move(r)));
    scope_ = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + ".scope")));
    case_ = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>("Respecter la casse", base + ".case")));
    word_ = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>("Mot entier", base + ".word")));
    auto sb = std::make_unique<ui::Button>("Chercher", base + ".search");
    sb->setStyle(ui::Button::Style::Default);
    searchButton_ = &static_cast<ui::Button&>(addChild(std::move(sb)));
    auto rb = std::make_unique<ui::Button>("Remplacer tout", base + ".replaceAll");
    rb->setStyle(ui::Button::Style::Primary);
    replaceButton_ = &static_cast<ui::Button&>(addChild(std::move(rb)));
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"O\xC3\xB9", 360.f}, {"Avant", 330.f}, {"Apr\xC3\xA8s", 330.f}, {"Fois", 60.f, 40.f, true, true, true, ui::Align::End}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += searchButton_->clicked->connect([this] { search(); });
    links_ += replaceButton_->clicked->connect([this] { (void)replaceAll(); });
    links_ += find_->textChanged->connect([this](const std::string&) { search(); });
    links_ += replace_->textChanged->connect([this](const std::string&) { search(); });
    links_ += scope_->selectionChanged->connect([this](int) { search(); });
    links_ += case_->stateChanged->connect([this](ui::Checkbox::State) { search(); });
    links_ += word_->stateChanged->connect([this](ui::Checkbox::State) { search(); });
    links_ += table_->activated->connect([this](ui::RowIndex row) {
        if (showingUsages()) {                                       // 1.14.0 (lot M1) : l'endroit exact
            if (row < usages_.size()) openUsage->emit(usages_[row]);
            return;
        }
        if (row < hits_.size() && hits_[row].view != kNoId) openHit->emit(hits_[row].view, hits_[row].object);
    });
    links_ += doc_->changed->connect([this](Id) {
        refreshScopes();
        search();
    });
    refreshScopes();
    search();
}

void HmiFindPane::refreshScopes() {
    const Id keep = scope_->selectedIndex() >= 0 && static_cast<std::size_t>(scope_->selectedIndex()) < scopes_.size()
                      ? scopes_[static_cast<std::size_t>(scope_->selectedIndex())]
                      : kNoId;
    scopes_ = {kNoId};
    std::vector<ui::DropDown::Item> items{{"Tout le projet", ""}};
    for (const auto& v : doc_->project.views) {
        scopes_.push_back(v.id);
        items.push_back({"Vue : " + v.name, v.name});
    }
    scope_->setItems(std::move(items));
    int at = 0;
    for (std::size_t i = 0; i < scopes_.size(); ++i) if (scopes_[i] == keep) at = static_cast<int>(i);
    scope_->setSelectedIndex(at);
}

hmi::design::FindOptions HmiFindPane::options() const {
    hmi::design::FindOptions o;
    o.matchCase = case_->isChecked();
    o.wholeWord = word_->isChecked();
    const int i = scope_->selectedIndex();
    o.view = i >= 0 && static_cast<std::size_t>(i) < scopes_.size() ? scopes_[static_cast<std::size_t>(i)] : kNoId;
    return o;
}

void HmiFindPane::say(std::string text, bool error) {
    if (error) ui::PropertyGrid::refuse(text);   // 1.12.19 (la regle des cases) : un refus se dit sous la case
    message_ = std::move(text);
    status_->setMessage(message_, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Info);
}

void HmiFindPane::setFind(const std::string& text) { find_->setText(text); search(); }
void HmiFindPane::setReplace(const std::string& text) { replace_->setText(text); search(); }
void HmiFindPane::setMatchCase(bool on) { case_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked); search(); }
void HmiFindPane::setWholeWord(bool on) { word_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked); search(); }
void HmiFindPane::setScopeView(Id view) {
    for (std::size_t i = 0; i < scopes_.size(); ++i)
        if (scopes_[i] == view) scope_->setSelectedIndex(static_cast<int>(i));
    search();
}

// 1.14.0 (lot M1) : Voir les usages d'une variable IHM - ses utilisations, pas les occurrences du texte.
void HmiFindPane::showUsages(const std::string& name) {
    usagesOf_ = name;
    find_->setText(name);   // la frappe relance search() : la liste des usages, le nom etant le meme
    search();
}

void HmiFindPane::listUsages() {
    usages_ = hmi::decledit::globalUsages(doc_->project, usagesOf_);
    if (const Id only = options().view; only != kNoId)
        usages_.erase(std::remove_if(usages_.begin(), usages_.end(), [&](const hmi::decledit::Usage& u) { return u.view != only; }), usages_.end());
    hits_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<ui::CellStyle> styles;
    std::size_t refs = 0;
    for (const auto& u : usages_) {
        hmi::design::FindHit h;
        h.view = u.view;
        h.object = u.object;
        h.where = u.where;
        h.before = h.after = u.text;
        h.count = u.count;
        hits_.push_back(h);
        rows.push_back({u.where, u.text, "", std::to_string(u.count)});
        ui::CellStyle cs;
        cs.icon = u.object != kNoId ? ui::Icon::Document : ui::Icon::Code;
        styles.push_back(cs);
        refs += u.count;
    }
    table_->setModel(std::make_shared<Rows>(std::vector<std::string>{"O\xC3\xB9", "Texte", "", "Fois"}, std::move(rows), std::move(styles)));
    if (usages_.empty()) say("Usages de " + usagesOf_ + " : employ\xC3\xA9" "e nulle part" + (options().view != kNoId ? " dans cette vue." : "."));
    else
        say(std::to_string(refs) + " utilisation(s) de " + usagesOf_ + " en " + std::to_string(usages_.size())
            + " endroit(s), lues comme la colonne Utilisations (ni cha\xC3\xAEne, ni commentaire, ni membre d'une autre variable). "
              "Double-clic : y aller. Taper un autre texte : chercher.");
    invalidate();
}

void HmiFindPane::search() {
    if (!usagesOf_.empty() && find_->text() == usagesOf_) {         // 1.14.0 (lot M1) : Voir les usages
        listUsages();
        return;
    }
    usagesOf_.clear();
    usages_.clear();
    hits_ = hmi::design::find(doc_->project, find_->text(), replace_->text(), options());
    std::vector<std::vector<std::string>> rows;
    std::vector<ui::CellStyle> styles;
    std::size_t occurrences = 0;
    // Un extrait autour de la premiere occurrence (un long champ la cachait a
    // droite) ; les retours et separateurs en espaces ; coupe entre deux
    // caracteres UTF-8, jamais au milieu d'un.
    const auto excerpt = [](const std::string& s, std::size_t at) {
        const auto boundary = [&](std::size_t i) {
            while (i > 0 && i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
            return i;
        };
        const std::size_t from = at > 24 ? boundary(at - 24) : 0;
        std::string one = from > 0 ? std::string("\xE2\x80\xA6") : std::string{};
        for (std::size_t i = from; i < s.size(); ++i) {
            const auto c = static_cast<unsigned char>(s[i]);
            one += c < 0x20 || c == 0x7F ? ' ' : s[i];
        }
        if (one.size() > 160) {
            std::size_t cut = 157;
            while (cut > 0 && (static_cast<unsigned char>(one[cut]) & 0xC0) == 0x80) --cut;
            one = one.substr(0, cut) + "\xE2\x80\xA6";
        }
        return one;
    };
    for (const auto& h : hits_) {
        rows.push_back({h.where, excerpt(h.before, h.first), excerpt(h.after, h.first), std::to_string(h.count)});
        ui::CellStyle cs;
        cs.icon = h.object != kNoId ? ui::Icon::Document : ui::Icon::Code;
        styles.push_back(cs);
        occurrences += h.count;
    }
    table_->setModel(std::make_shared<Rows>(std::vector<std::string>{"O\xC3\xB9", "Avant", "Apr\xC3\xA8s", "Fois"}, std::move(rows), std::move(styles)));
    if (find_->text().empty()) say("Tape ce qu'il faut chercher : l'aper\xC3\xA7u montre chaque champ touch\xC3\xA9, avant et apr\xC3\xA8s.");
    else if (hits_.empty()) say("\xC2\xAB " + find_->text() + " \xC2\xBB : introuvable" + (options().view != kNoId ? " dans cette vue." : "."));
    else
        say(std::to_string(occurrences) + " occurrence(s) dans " + std::to_string(hits_.size())
            + " champ(s). Remplacer tout : une seule commande (Ctrl+Z). Double-clic : ouvrir la vue.");
    invalidate();
}

std::size_t HmiFindPane::replaceAll() {
    const std::string text = find_->text(), replacement = replace_->text();
    if (text.empty()) { say("Rien \xC3\xA0 remplacer : le texte cherch\xC3\xA9 est vide.", true); return 0; }
    const auto o = options();
    std::size_t fields = 0;
    auto cmd = hmi::changeProject(doc_, "Remplacer " + text, [&](hmi::Project& p) { fields = hmi::design::replaceAll(p, text, replacement, o); });
    if (cmd) apply_(std::move(cmd));
    search();
    say(fields ? std::to_string(fields) + " champ(s) chang\xC3\xA9(s) : \xC2\xAB " + text + " \xC2\xBB \xE2\x86\x92 \xC2\xAB " + replacement
                     + " \xC2\xBB (Ctrl+Z pour revenir)."
               : std::string("Rien n'a chang\xC3\xA9."),
        fields == 0);
    return fields;
}

void HmiFindPane::onLayout() {
    const auto b = bounds();
    const float row = 34, pad = 8;
    const float half = (b.w - 3 * pad) / 2;
    find_->setBounds({b.x + pad, b.y + pad, half, row - 4});
    replace_->setBounds({b.x + 2 * pad + half, b.y + pad, half, row - 4});
    const float y2 = b.y + pad + row;
    scope_->setBounds({b.x + pad, y2, std::min(300.f, half * 0.6f), row - 6});
    float x = b.x + pad + std::min(300.f, half * 0.6f) + 12;
    case_->setBounds({x, y2 + 3, 170, row - 10});
    x += 176;
    word_->setBounds({x, y2 + 3, 130, row - 10});
    replaceButton_->setBounds({b.x + b.w - pad - 150, y2, 150, row - 6});
    searchButton_->setBounds({b.x + b.w - 2 * pad - 150 - 110, y2, 110, row - 6});
    const float top = y2 + row + 4;
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    table_->setBounds({b.x, top, b.w, std::max(0.f, b.y + b.h - 24 - top)});
}

} // namespace app
