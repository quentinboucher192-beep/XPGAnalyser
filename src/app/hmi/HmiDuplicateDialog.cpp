// app/hmi/HmiDuplicateDialog.cpp - "Dupliquer..." (1.10.2, chantier D).
#include "HmiDuplicateDialog.hpp"
#include "HmiAssist.hpp"           // 1.11.2 (API-V, D13) : le programme installe, ce qu'est API.V
#include "HmiPanels.hpp"           // 1.11 (T2-22) : les libelles de l'inspecteur
#include "HmiParamPanes.hpp"       // 1.11.2 (SYM) : les DDT du programme (les membres d'un parametre structure)

#include "../../hmi/HmiApiVars.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiPopupParams.hpp"   // 1.11.2 (SYM) : typeMembers
#include "../../hmi/HmiSymbols.hpp"       // 1.11.2 (SYM) : isSymbolView
#include "../../menu/MenuManager.hpp"
#include "../../project/MemberTree.hpp"
#include "../../ui/widgets/Containers.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace dup = hmi::dup;

namespace {

// 1.11.2 (API-V, D13) : les bornes d'un tableau, pour les trois endroits qui les demandent
// (le suivi par defaut d'un indice, la case rouge, Remplir > Tableau...). D'abord les
// variables IHM du projet (V : 0..63, comme avant) ; puis un tableau de l'automate ecrit
// sous API. (API.V, API.Unite.Tab), d'apres ce que l'aide a la saisie en dit (le modele
// d'API-M, sur le programme installe) : une case $API.V[1].Ouv$ au-dela des bornes passe
// au rouge « hors des bornes de API.V (0..63) », comme une variable IHM. Le nom nu d'un
// tableau de l'automate (V[0] sans API.) garde la conduite d'avant (bornes inconnues) ;
// un projet IHM qui a sa propre variable ou vue API la garde (describe le sait).
// 1.11.2 (SYM, decision 240) : dans un symbole, ses parametres passent avant (`view` : la vue
// en cours d'edition ; nul ou pas un symbole : comme avant). Un parametre `Value : ARRAY[0..9]
// OF UINT` a les bornes 0..9 ; `Value[10]` passe au rouge « hors des bornes de Value (0..9) ».
dup::BoundsFn boundsOf(const hmi::DocumentPtr& doc, const hmi::View* view = nullptr);

// 1.11.2 (SYM) : les membres d'un type (un type IHM du projet, ou un DDT du programme de
// l'API) - pour `Armoire.ana.PT1` quand `Armoire : T_Armoire` est un parametre du symbole.
// Les DDT se relevent une fois (la fenetre est modale).
dup::TypeMembers symbolTypeMembers(const hmi::DocumentPtr& doc) {
    auto plcOf = assist::sourcesFor(doc).plc;
    auto memo = std::make_shared<std::optional<hmi::params::PlcTypes>>();
    return [doc, plcOf = std::move(plcOf), memo](std::string_view type) {
        if (!*memo) {
            const auto plc = plcOf ? plcOf() : nullptr;
            *memo = hmiparams::plcTypesOf(plc.get());
        }
        return hmi::params::typeMembers(doc->project, type, **memo);
    };
}

dup::BoundsFn boundsOf(const hmi::DocumentPtr& doc, const hmi::View* view) {
    auto hmiBounds = dup::projectBounds(doc->project);
    auto plcOf = assist::sourcesFor(doc).plc;
    // Une fenetre modale : le programme ne change pas pendant qu'elle est ouverte.
    auto memo = std::make_shared<std::map<std::string, std::optional<dup::Bounds>>>();
    dup::BoundsFn inner = [doc, hmiBounds = std::move(hmiBounds), plcOf = std::move(plcOf), memo](std::string_view path) -> std::optional<dup::Bounds> {
        if (auto b = hmiBounds(path)) return b;
        if (!plcOf || !hmi::apivars::isApiPath(path)) return std::nullopt;
        std::string key(path);
        for (auto& ch : key) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (const auto it = memo->find(key); it != memo->end()) return it->second;
        std::optional<dup::Bounds> out;
        if (const auto plc = plcOf()) {
            const auto d = assist::describe(doc->project, plc.get(), path);
            const auto shape = project::members::parseArray(d.type);
            if (d.found && shape.dims.size() == 1) out = dup::Bounds{shape.dims[0].first, shape.dims[0].second};
        }
        (*memo)[key] = out;
        return out;
    };
    // 1.11.2 (SYM) : dans un symbole, ses parametres d'abord.
    if (view && hmi::isSymbolView(*view)) return dup::symbolParamBounds(*view, std::move(inner), symbolTypeMembers(doc));
    return inner;
}

const gfx::FontId kSmall{13};
const gfx::FontId kText{15};
const gfx::Color  kOrange{230, 140, 30, 255};
const gfx::Color  kRed{220, 60, 50, 255};
const gfx::Color  kAmber{215, 170, 40, 255};
const gfx::Color  kBlue{60, 130, 220, 255};

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n > 1 ? many : one);
}

std::string markerLabel(const std::string& name) { return "$" + name + "$"; }

// ---- 1.11 (T2-22) : l'avant / apres nomme les proprietes comme l'inspecteur ----
// Les libelles que l'inspecteur affiche pour un objet, selon son genre ("opening" ->
// "Ouverture (%)", "fault" -> "D\xC3\xA9" "faut (expression)", "label" -> "Libell\xC3\xA9").
// Chaque ligne de l'inspecteur dit sa cle par son commit, qu'on ecoute sans rien
// changer (il rend faux) ; une ligne qui ne prend que l'expression est rangee sous
// "cle (expression)". Rien n'est recopie de l'inspecteur : ses libelles font foi.
using Labels = std::map<std::string, std::string>;
Labels inspectorLabels(const hmi::View& view, hmi::Id id, const hmi::Project* project) {
    Labels out;
    std::string key;
    bool expr = false;
    HmiPropertyCommits listen;
    listen.prop = [&](const std::string& k, const std::string&, bool e) {
        key = k;
        expr = e;
        return false;
    };
    for (const auto& cat : hmiPropertyCategories(view, {id}, nullptr, listen, nullptr, project))
        for (const auto& p : cat.properties) {
            if (!p.commit) continue;
            key.clear();
            (void)p.commit("x");
            if (key.empty()) continue;
            std::string name = p.name;
            if (const auto f = name.find("  \xC6\x92"); f != std::string::npos) name.resize(f);   // la marque d'une expression
            out.emplace(expr ? key + " (expression)" : key, std::move(name));
        }
    return out;
}

// L'endroit d'un repere ("opening (expression)") sous le nom de l'inspecteur
// ("Ouverture (%) (expression)") ; une propriete qu'il ne montre pas prend son
// libelle general ; une action ou une alarme garde le sien ("action 1 (cible)").
std::string spotLabel(const Labels& labels, const std::string& where) {
    if (const auto it = labels.find(where); it != labels.end()) return it->second;
    static const std::string kExpr = " (expression)";
    const bool isExpr = where.size() > kExpr.size() && where.compare(where.size() - kExpr.size(), kExpr.size(), kExpr) == 0;
    const std::string key = isExpr ? where.substr(0, where.size() - kExpr.size()) : where;
    std::string label;
    if (const auto it = labels.find(key); it != labels.end()) label = it->second;
    else if (std::string help; !hmiPropertyInfo(key, label, help) || label.empty()) return where;
    return isExpr && label.find("(expression)") == std::string::npos ? label + kExpr : label;
}
// ---- fin 1.11 (T2-22) ----

// Un trait pointille (les cadres des copies dans l'apercu).
void dashed(const ui::PaintContext& ctx, gfx::Rect r, gfx::Color c) {
    const float d = 5.f, g = 3.f;
    for (float x = r.x; x < r.x + r.w; x += d + g) {
        const float w = std::min(d, r.x + r.w - x);
        ctx.r.fillRect({x, r.y, w, 1.5f}, c);
        ctx.r.fillRect({x, r.y + r.h - 1.5f, w, 1.5f}, c);
    }
    for (float y = r.y; y < r.y + r.h; y += d + g) {
        const float h = std::min(d, r.y + r.h - y);
        ctx.r.fillRect({r.x, y, 1.5f, h}, c);
        ctx.r.fillRect({r.x + r.w - 1.5f, y, 1.5f, h}, c);
    }
}

} // namespace

// ------------------------------------------------------------------ Grid ---
// Le tableau des copies : une ligne par copie (l'original en tete), une
// colonne par repere apres le nom. L'AVANT est le texte gris d'une case vide
// (le repere, le nom de l'original) ; l'APRES s'ecrit dedans.
class HmiDuplicateDialog::Grid final : public ui::Widget {
public:
    explicit Grid(HmiDuplicateDialog& d) : d_(d) {}
    std::vector<std::vector<ui::InputText*>> cells;   // [ligne][colonne]
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {600.f, 30.f + 32.f * static_cast<float>(cells.size())};
        h.minimum = h.preferred;
        h.stretchX = 1.f;
        return h;
    }
protected:
    // 1.11.1 (R1111-7) : chaque colonne a sa largeur (gridColumns), mesuree au
    // placement ; l'en-tete et les cases s'y tiennent.
    void onLayout() override {
        const auto b = bounds();
        widths_ = d_.gridColumns(b.w - 92.f);
        for (std::size_t r = 0; r < cells.size(); ++r) {
            float x = b.x + 88.f;
            for (std::size_t c = 0; c < cells[r].size(); ++c) {
                const float w = c < widths_.size() ? widths_[c] : 150.f;
                cells[r][c]->setBounds({x, b.y + 28.f + 32.f * static_cast<float>(r), w - 8.f, 28.f});
                x += w;
            }
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& col = ctx.theme.color;
        const auto names = d_.columns();
        if (widths_.size() != names.size()) widths_ = d_.gridColumns(b.w - 92.f);
        float x = b.x + 88.f;
        for (std::size_t c = 0; c < names.size(); ++c) {
            const float w = widths_[c];
            // L'en-tete ne deborde jamais sur la colonne suivante.
            ctx.r.pushClip({x, b.y, std::max(0.f, w - 8.f), 26.f});
            ctx.r.drawText({x, b.y + 4.f}, d_.gridHeader(c, w - 8.f), ctx.theme.font.uiBold, col.text);
            ctx.r.popClip();
            x += w;
        }
        for (std::size_t r = 0; r < cells.size(); ++r) {
            const float y = b.y + 28.f + 32.f * static_cast<float>(r);
            if (static_cast<int>(r) == d_.chosen_) ctx.r.fillRect({b.x, y - 2.f, b.w, 32.f}, gfx::Color{60, 130, 220, 28});
            const std::string label = r == 0 ? "1  original" : std::to_string(r + 1);
            ctx.r.drawText({b.x + 6.f, y + 6.f}, label, kSmall, r == 0 ? col.textMuted : col.text);
        }
        // Les cases a signaler : ambre (vide), rouge (ne mene a rien, hors bornes, nom).
        for (const auto& cc : d_.check_.cells) {
            const int c = d_.columnIndex(cc.column);
            if (cc.row < 0 || static_cast<std::size_t>(cc.row) >= cells.size() || c < 0 || static_cast<std::size_t>(c) >= cells[cc.row].size()) continue;
            auto rr = cells[static_cast<std::size_t>(cc.row)][static_cast<std::size_t>(c)]->bounds();
            rr = {rr.x - 2.f, rr.y - 2.f, rr.w + 4.f, rr.h + 4.f};
            const gfx::Color k = cc.state == dup::CellState::Empty ? kAmber : kRed;
            ctx.r.strokeRect(rr, cc.blocks ? k : gfx::Color{k.r, k.g, k.b, 110}, 2.f);
        }
    }
private:
    HmiDuplicateDialog& d_;
    std::vector<float>  widths_;
};

// ----------------------------------------------------------- CopiesPage ---
class HmiDuplicateDialog::CopiesPage final : public ui::Widget {
public:
    explicit CopiesPage(HmiDuplicateDialog& d) : d_(d) {}
    std::vector<ui::Widget*> toolbar;     // Copies, Remplir...
    std::vector<ui::Widget*> checks;      // suivre / garder / l'original
protected:
    void onLayout() override {
        const auto b = bounds();
        float y = b.y + 8.f + (d_.note_.empty() ? 3.f : 4.f) * 20.f;     // la selection, les reperes, les indices (la note)
        float x = b.x + 8.f;
        for (auto* w : checks) {
            const float cw = std::min(b.w - 16.f, ui::measureWidth(static_cast<ui::Checkbox*>(w)->label(), gfx::FontId{16}) + 40.f);
            if (x + cw > b.x + b.w - 8.f) { x = b.x + 8.f; y += 28.f; }
            w->setBounds({x, y, cw, 26.f});
            x += cw + 12.f;
        }
        y += checks.empty() ? 0.f : 34.f;
        toolbarY_ = y;
        x = b.x + 8.f + 58.f;   // "Copies"
        const float widths[] = {56.f, 140.f, 110.f, 56.f, 72.f, 118.f, 120.f, 64.f};   // 1.10.4 : Tableau...
        for (std::size_t i = 0; i < toolbar.size(); ++i) {
            if (i == 1) x += 70.f;   // "Remplir"
            if (i == 3) x += 22.f;   // "de"
            const float w = i < std::size(widths) ? widths[i] : 80.f;
            toolbar[i]->setBounds({x, y, w, 28.f});
            x += w + 8.f;
        }
        y += 38.f;
        const float baH = 7.f * 18.f + 26.f;
        if (d_.gridPanel_) {
            const float gh = std::max(80.f, b.y + b.h - y - baH - 6.f);
            d_.gridPanel_->setBounds({b.x + 4.f, y, b.w - 8.f, gh});
            if (d_.grid_)
                d_.grid_->setBounds({b.x + 4.f, y - d_.gridPanel_->scrollOffset().y, b.w - 20.f, d_.grid_->sizeHint().preferred.h});
            baY_ = y + gh + 8.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        const auto& s = d_.scan_;
        float y = b.y + 8.f;
        // La selection.
        std::string sel = "S\xC3\xA9lection : ";
        std::size_t shown = 0;
        if (const auto* v = d_.view())
            for (const hmi::Id id : s.units)
                if (const auto* o = v->object(id); o && shown < 4) { sel += (shown++ ? ", " : "") + o->name; }
        if (s.units.size() > shown) sel += ", \xE2\x80\xA6";
        sel += "  (" + plural(s.objects.size(), "objet", "objets") + ", " + std::to_string(static_cast<long>(s.box.w)) + " \xC3\x97 " + std::to_string(static_cast<long>(s.box.h)) + " px)";
        ctx.r.drawText({b.x + 8.f, y}, sel, kText, c.text);
        y += 20.f;
        // Les reperes et leurs endroits.
        std::string mk = s.markers.empty() ? std::string("Aucun rep\xC3\xA8re : les copies gardent les textes (la pose et les indices seulement).") : "Rep\xC3\xA8res : ";
        for (std::size_t i = 0; i < s.markers.size(); ++i) {
            const auto& m = s.markers[i];
            mk += (i ? "   " : "") + markerLabel(m.name) + " \xC3\x97 " + std::to_string(m.uses) + " (" + (m.variable ? "variable" : "texte");
            if (!m.members.empty()) {
                mk += " : ";
                for (std::size_t k = 0; k < m.members.size() && k < 3; ++k) mk += (k ? ", ." : ".") + m.members[k];
            }
            mk += ", " + plural(m.spots.size(), "endroit", "endroits") + ")";
        }
        ctx.r.drawText({b.x + 8.f, y}, mk, kSmall, s.markers.empty() ? c.textMuted : c.text);
        y += 20.f;
        std::string ix = s.indices.empty() ? std::string("Aucun indice de tableau.") : "Indices : ";
        for (std::size_t i = 0; i < s.indices.size(); ++i) {
            const auto& k = s.indices[i];
            ix += (i ? "   " : "") + k.label() + " \xC3\x97 " + std::to_string(k.uses);
            if (k.bounds) ix += " (" + std::to_string(k.bounds->first) + ".." + std::to_string(k.bounds->second) + ")";
            else ix += " (bornes inconnues)";
        }
        ctx.r.drawText({b.x + 8.f, y}, ix, kSmall, c.textMuted);
        // 1.10.4 : la colonne preremplie (V[0], V[1]...) - la note le dit.
        if (!d_.note_.empty()) {
            y += 20.f;
            ctx.r.drawText({b.x + 8.f, y}, d_.note_, kSmall, kBlue);
        }
        // La barre : Copies, Remplir.
        ctx.r.drawText({b.x + 8.f, toolbarY_ + 6.f}, "Copies", ctx.theme.font.uiBold, c.text);
        if (d_.copiesField_) {
            const auto r = d_.copiesField_->bounds();
            ctx.r.drawText({r.x + r.w + 12.f, toolbarY_ + 6.f}, "Remplir", ctx.theme.font.uiBold, c.text);
        }
        if (d_.fillFrom_) {
            const auto r = d_.fillFrom_->bounds();
            ctx.r.drawText({r.x - 22.f, toolbarY_ + 6.f}, "de", kSmall, c.textMuted);
        }
        // L'avant / apres de la ligne choisie.
        float by = baY_;
        ctx.r.drawText({b.x + 8.f, by}, "Avant / apr\xC3\xA8s, ligne " + std::to_string(d_.chosen_ + 1) + (d_.chosen_ == 0 ? " (l'original)" : ""),
                       ctx.theme.font.uiBold, c.text);
        by += 22.f;
        ctx.r.pushClip({b.x, by, b.w, b.y + b.h - by});
        for (const auto& line : d_.beforeAfter(d_.chosen_)) {
            ctx.r.drawText({b.x + 12.f, by}, line, kSmall, c.textMuted);
            by += 18.f;
        }
        ctx.r.popClip();
    }
private:
    HmiDuplicateDialog& d_;
    float               toolbarY_{0}, baY_{0};
};

// -------------------------------------------------------------- Preview ---
// L'apercu en direct : la vue a l'echelle, ses objets en gris, la selection en
// bleu, les copies en pointilles orange (rouge : hors de la vue ; ambre : sur
// un autre objet).
class HmiDuplicateDialog::Preview final : public ui::Widget {
public:
    explicit Preview(HmiDuplicateDialog& d) : d_(d) {}
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        const auto* v = d_.view();
        if (!v || v->width <= 0 || v->height <= 0) return;
        const float s = std::min((b.w - 16.f) / static_cast<float>(v->width), (b.h - 16.f) / static_cast<float>(v->height));
        const float ox = b.x + (b.w - s * static_cast<float>(v->width)) * 0.5f, oy = b.y + 8.f;
        const auto map = [&](const hmi::Box& x) {
            return gfx::Rect{ox + s * static_cast<float>(x.x), oy + s * static_cast<float>(x.y), std::max(2.f, s * static_cast<float>(x.w)), std::max(2.f, s * static_cast<float>(x.h))};
        };
        ctx.r.fillRect({ox, oy, s * static_cast<float>(v->width), s * static_cast<float>(v->height)}, c.inputBg);
        ctx.r.strokeRect({ox, oy, s * static_cast<float>(v->width), s * static_cast<float>(v->height)}, c.borderStrong, 1.f);
        ctx.r.pushClip(b);
        const std::set<hmi::Id> sel(d_.scan_.units.begin(), d_.scan_.units.end());
        for (const auto& o : v->objects) {
            if (o.parent != hmi::kNoId || sel.count(o.id)) continue;
            ctx.r.fillRect(map(hmi::edit::selectionBounds(*v, {o.id})), gfx::Color{128, 128, 128, 60});
        }
        const auto box = map(d_.scan_.box);
        ctx.r.fillRect(box, gfx::Color{60, 130, 220, 60});
        ctx.r.strokeRect(box, kBlue, 1.5f);
        const int n = d_.plan_.copies();
        const auto offs = dup::offsets(d_.scan_.box, n, d_.plan_.layout);
        const auto out = d_.overflow();
        const auto over = d_.overlap();
        for (int i = 0; i < static_cast<int>(offs.size()); ++i) {
            hmi::Box x = d_.scan_.box;
            x.x += offs[static_cast<std::size_t>(i)].dx;
            x.y += offs[static_cast<std::size_t>(i)].dy;
            const bool isOut = std::find(out.begin(), out.end(), i + 1) != out.end();
            const bool isOver = std::find(over.begin(), over.end(), i + 1) != over.end();
            const auto r = map(x);
            dashed(ctx, r, isOut ? kRed : isOver ? kAmber : kOrange);
            if (r.w > 14.f && r.h > 12.f) ctx.r.drawText({r.x + 3.f, r.y + 2.f}, std::to_string(i + 2), kSmall, isOut ? kRed : kOrange);
        }
        ctx.r.popClip();
    }
private:
    HmiDuplicateDialog& d_;
};

// ----------------------------------------------------------- LayoutPage ---
class HmiDuplicateDialog::LayoutPage final : public ui::Widget {
public:
    explicit LayoutPage(HmiDuplicateDialog& d) : d_(d) {}
    std::vector<ui::RadioButton*> radios;
    Preview*                      preview{nullptr};
protected:
    void onLayout() override {
        const auto b = bounds();
        float x = b.x + 8.f;
        for (auto* r : radios) {
            r->setBounds({x, b.y + 8.f, 130.f, 26.f});
            x += 140.f;
        }
        if (d_.columnsField_) d_.columnsField_->setBounds({x + 70.f, b.y + 8.f, 56.f, 28.f});
        if (d_.spacingField_) d_.spacingField_->setBounds({b.x + 130.f, b.y + 44.f, 70.f, 28.f});
        if (d_.fit_) d_.fit_->setBounds({b.x + 214.f, b.y + 44.f, 190.f, 28.f});
        if (preview) preview->setBounds({b.x + 4.f, b.y + 84.f + 3.f * 20.f, b.w - 8.f, std::max(60.f, b.h - 84.f - 3.f * 20.f - 4.f)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        if (d_.columnsField_) {
            const auto r = d_.columnsField_->bounds();
            ctx.r.drawText({r.x - 66.f, r.y + 6.f}, "Colonnes", kSmall, d_.plan_.layout.axis == dup::Axis::Grid ? c.text : c.textMuted);
        }
        ctx.r.drawText({b.x + 8.f, b.y + 50.f}, "Espacement (px)", ctx.theme.font.uiBold, c.text);
        float y = b.y + 84.f;
        const double step = dup::stepOf(d_.scan_.box, d_.plan_.layout);
        const bool vertical = d_.plan_.layout.axis == dup::Axis::Y;
        ctx.r.drawText({b.x + 8.f, y}, "Pas mesur\xC3\xA9 : " + std::to_string(static_cast<long>(std::lround(step))) + " px ("
                       + (vertical ? "hauteur " + std::to_string(static_cast<long>(d_.scan_.box.h)) : "largeur " + std::to_string(static_cast<long>(d_.scan_.box.w)))
                       + " + espacement " + std::to_string(static_cast<long>(d_.plan_.layout.spacing)) + ")", kSmall, c.textMuted);
        y += 20.f;
        const auto out = d_.overflow();
        if (out.empty()) ctx.r.drawText({b.x + 8.f, y}, "Toutes les copies tiennent dans la vue.", kSmall, c.textMuted);
        else ctx.r.drawText({b.x + 8.f, y}, plural(out.size(), "copie sort", "copies sortent") + " de la vue (en rouge) : \xC2\xAB Ajuster l'espacement \xC2\xBB ou une grille.", kSmall, kRed);
        y += 20.f;
        const auto over = d_.overlap();
        if (!over.empty()) ctx.r.drawText({b.x + 8.f, y}, plural(over.size(), "copie chevauche", "copies chevauchent") + " un autre objet de la vue (en ambre).", kSmall, kAmber);
    }
private:
    HmiDuplicateDialog& d_;
};

// ----------------------------------------------------------------- Body ---
class HmiDuplicateDialog::Body final : public ui::Widget {
public:
    explicit Body(HmiDuplicateDialog& d) : d_(d) {}
    std::vector<ui::Button*> buttons;
    gfx::Rect panel{};
protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(1000.f, r.w - 40.f), h = std::min(700.f, r.h - 40.f);
        panel = {std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h};
        if (d_.tabs_) d_.tabs_->setBounds({panel.x + 8.f, panel.y + 40.f, panel.w - 16.f, panel.h - 40.f - 56.f});
        float bx = panel.x + panel.w - 16.f;
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
            const float bw = std::max(110.f, ui::measureWidth((*it)->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            (*it)->setBounds({bx, panel.y + panel.h - 44.f, bw, 30.f});
            bx -= 10.f;
        }
        noteW_ = bx - panel.x - 24.f;
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel, c.panelBg);
        ctx.r.strokeRect(panel, c.borderStrong, 1.f);
        const float th = ctx.theme.metric.headerHeight;
        ctx.r.fillRect({panel.x, panel.y, panel.w, th}, c.headerBg);
        ctx.r.drawText({panel.x + 12.f, panel.y + (th - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, d_.title(), ctx.theme.font.uiBold, c.text);
        // Pourquoi "Dupliquer" est gris (ou ce qu'il va faire).
        const std::string note = d_.blockingText();
        if (!note.empty()) {
            ctx.r.pushClip({panel.x + 12.f, panel.y + panel.h - 50.f, noteW_, 44.f});
            ctx.r.drawText({panel.x + 16.f, panel.y + panel.h - 38.f}, note, kSmall, d_.check_.blocked() ? kRed : c.textMuted);
            ctx.r.popClip();
        }
    }
private:
    HmiDuplicateDialog& d_;
    float               noteW_{200.f};
};

// ===================================================================== ======
HmiDuplicateDialog::HmiDuplicateDialog(Spec spec) : menu::WidgetMenu("dialog.hmiDuplicate"), spec_(std::move(spec)) {
    const auto* v = view();
    if (!v) return;
    // 1.11.2 (SYM, decision 240 ; la capture du client « Value[0] n'existe pas ») : dans l'editeur
    // d'un symbole, ses parametres sont des chemins qui existent (Name, Value, Value[0]..Value[9],
    // les membres d'un parametre structure) ; leurs bornes sont celles du parametre. Hors d'un
    // symbole, `exists` et les bornes restent ce qu'ils etaient.
    if (hmi::isSymbolView(*v)) {
        spec_.exists = dup::symbolParamExists(*v, std::move(spec_.exists), symbolTypeMembers(spec_.doc));
        for (const auto& name : dup::symbolParamNames(*v)) candidates_.push_back(name);
    }
    scan_ = dup::scan(*v, spec_.selection, boundsOf(spec_.doc, v));   // 1.11.2 (D13) : API.V aussi ; (SYM) : les parametres
    plan_ = dup::defaultPlan(*v, scan_, std::max(0, spec_.copies));   // 1.10.4 : 0 - "Remplacer..." (l'original seulement)
    // 1.11 (REP-4, decision 72) : la case "Suivre" de chaque indice hors repere, cochee si aucun
    // repere ne porte d'indice (comme en 1.10.x), decochee des qu'un repere en porte un ; sans
    // copie ("Remplacer..."), le meme reglage, garde pour quand on en ajoute.
    for (const auto& k : scan_.indices)
        followed_.push_back(plan_.rows.size() > 1 ? plan_.rows[1].indices.count(dup::indexKey(k.path, k.value)) > 0
                                                  : dup::followByDefault(scan_) && dup::followFits(k, 1));
    for (const auto& var : spec_.doc->project.programs.variables) candidates_.push_back(var.name);
    for (const auto& o : v->objects) candidates_.push_back(o.name);
    chosen_ = plan_.rows.size() > 1 ? 1 : 0;
    // 1.10.4 (client-dupliquer.png) : l'objet suit V[0] ailleurs et son repere de
    // type variable est vide - la colonne se preremplit avec V[0], V[1]... ; la
    // note le dit, et "Vider" la remet a blanc.
    if ((prefilled_ = dup::prefill(scan_, plan_))) {
        const auto values = dup::arrayColumn(prefilled_->array, prefilled_->from, plan_.rows.size());
        const std::string key = dup::markerKey(prefilled_->marker);
        for (std::size_t r = 0; r < values.size() && r < plan_.rows.size(); ++r) plan_.rows[r].markers[key] = values[r];
        const std::string first = prefilled_->array + "[" + std::to_string(prefilled_->from) + "]";
        note_ = markerLabel(prefilled_->marker) + " est pr\xC3\xA9rempli avec " + first + ", " + prefilled_->array + "["
              + std::to_string(prefilled_->from + 1) + "]\xE2\x80\xA6 : l'objet suit " + first
              + " ailleurs. \xC2\xAB Vider \xC2\xBB le remet \xC3\xA0 blanc, \xC2\xAB Tableau\xE2\x80\xA6 \xC2\xBB en choisit un autre.";
    } else {
        // 1.11 (REP) : un repere a indices ($V[1].Ouv$) est prerempli par son morceau, ses
        // indices decales du numero de la copie ; les copies gardent ses $.
        for (const auto& m : scan_.markers) {
            if (!dup::hasIndices(m.name)) continue;
            note_ = markerLabel(m.name) + " est pr\xC3\xA9rempli : copie 1 " + dup::shiftIndices(m.name, 1) + ", copie 2 "
                  + dup::shiftIndices(m.name, 2) + "\xE2\x80\xA6 Les copies gardent les $ ; ce qui n'est pas entre $ ne change pas";
            // 1.11 (REP-4) : la case "Suivre V[0]" (decochee ici) fait varier aussi l'indice hors repere.
            note_ += scan_.indices.empty() ? std::string(".")
                                           : " (\xC2\xAB Suivre " + scan_.indices.front().label() + " \xC2\xBB le fait varier).";
            break;
        }
    }
    validate();
}

// 1.10.4 : les bornes des tableaux (V[64]) ; 1.11 (R111) : puis les membres.
void HmiDuplicateDialog::validate() {
    const auto* v = view();
    if (!v) return;
    dup::ValidateOptions opt;
    opt.exists = spec_.exists;             // 1.11.2 (SYM) : dans un symbole, enveloppe de ses parametres (le constructeur)
    opt.bounds = boundsOf(spec_.doc, v);   // 1.10.4 : V[64] hors des bornes ; 1.11.2 (D13) : API.V[64] aussi ; (SYM) : Value[10]
    opt.candidates = candidates_;
    opt.keepColumns = keep_;
    check_ = dup::validate(*v, scan_, plan_, opt);
    // 1.11 (R111) : une case rouge qui vise un membre (Armoires[0].sortie.V8) :
    // "veux-tu dire Armoires[0].sorties.V8 ?", comme pour une variable.
    static constexpr std::string_view kMissing = " n'existe pas";
    for (auto& c : check_.cells) {
        if (c.state != dup::CellState::Unknown || !c.suggestions.empty()) continue;
        const std::string value = cell(c.row, c.column);
        if (value.find('.') == std::string::npos || c.message != value + std::string(kMissing)) continue;
        c.suggestions = memberSuggestions(value, spec_.exists, spec_.assist, candidates_);
    }
}

namespace {
// Les morceaux d'un chemin, coupes aux points hors crochets : Armoires[0], sortie, V8.
std::vector<std::string> pathParts(std::string_view path) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (const char ch : path) {
        if (ch == '[') ++depth;
        else if (ch == ']' && depth > 0) --depth;
        if (ch == '.' && depth == 0) {
            out.push_back(std::move(cur));
            cur.clear();
            continue;
        }
        cur += ch;
    }
    out.push_back(std::move(cur));
    return out;
}
// "sorties[2]" : "sorties" et "[2]".
std::pair<std::string, std::string> splitIndex(const std::string& part) {
    const auto b = part.find('[');
    if (b == std::string::npos) return {part, std::string{}};
    return {part.substr(0, b), part.substr(b)};
}
} // namespace

std::vector<std::string> HmiDuplicateDialog::memberSuggestions(std::string_view path, const dup::PathExists& exists,
                                                               const ui::InputText::Assist& assist,
                                                               const std::vector<std::string>& variables) {
    std::vector<std::string> out;
    if (!exists) return out;
    const auto parts = pathParts(path);
    if (parts.size() < 2 || std::any_of(parts.begin(), parts.end(), [](const std::string& p) { return p.empty(); })) return out;
    // Les membres d'un chemin qui existe : ce que l'aide a la saisie propose apres "chemin.".
    const auto membersOf = [&](const std::string& at) {
        std::vector<std::string> names;
        if (!assist) return names;
        std::vector<ui::InputText::Suggestion> list;
        std::size_t from = 0;
        assist(at + ".", from, list);
        for (const auto& s : list)
            if (!s.text.empty()) names.push_back(s.text);
        return names;
    };
    // Le debut : le premier morceau s'il existe, sinon les variables les plus proches.
    std::vector<std::string> paths;
    if (exists(parts[0])) paths.push_back(parts[0]);
    else {
        const auto [base, index] = splitIndex(parts[0]);
        for (const auto& near : dup::didYouMean(base, variables))
            if (exists(near + index)) paths.push_back(near + index);
    }
    // Chaque morceau suivant : tel quel s'il existe, sinon les membres les plus proches.
    for (std::size_t i = 1; i < parts.size() && !paths.empty(); ++i) {
        std::vector<std::string> next;
        for (const auto& at : paths) {
            if (exists(at + "." + parts[i])) {
                next.push_back(at + "." + parts[i]);
                continue;
            }
            const auto [base, index] = splitIndex(parts[i]);
            for (const auto& near : dup::didYouMean(base, membersOf(at)))
                if (exists(at + "." + near + index)) next.push_back(at + "." + near + index);
        }
        if (next.size() > 6) next.resize(6);
        paths = std::move(next);
    }
    for (auto& p : paths)
        if (p != path && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(std::move(p));
    if (out.size() > 3) out.resize(3);
    return out;
}

const hmi::View* HmiDuplicateDialog::view() const {
    return spec_.doc ? spec_.doc->project.view(spec_.view) : nullptr;
}

menu::MenuTraits HmiDuplicateDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = false;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string HmiDuplicateDialog::title() const {
    std::string t = "Dupliquer\xE2\x80\xA6";
    if (const auto* v = view(); v && !scan_.units.empty())
        if (const auto* o = v->object(scan_.units.front()))
            t += "  " + o->name + (scan_.units.size() > 1 ? " (+" + std::to_string(scan_.units.size() - 1) + ")" : std::string{});
    return t;
}

std::vector<std::string> HmiDuplicateDialog::columns() const {
    std::vector<std::string> c{"Nom"};
    for (const auto& m : scan_.markers) c.push_back(markerLabel(m.name));
    return c;
}

// 1.11.1 (R1111-7) : un repere long ($API.Tempon_TOR16_I4_OUT[1]$) debordait sur
// l'en-tete suivant, et ses cases (de largeur egale) montraient la fin coupee a
// gauche. Chaque colonne prend ce que demandent son en-tete et ses cases.
std::vector<float> HmiDuplicateDialog::fitColumns(const std::vector<float>& need, float width, float minimum) {
    const std::size_t n = need.size();
    std::vector<float> out(n, minimum);
    if (n == 0) return out;
    std::vector<float> want(n);
    float sum = 0.f;
    for (std::size_t i = 0; i < n; ++i) {
        want[i] = std::max(minimum, need[i]);
        sum += want[i];
    }
    if (sum <= width) {   // tout tient : le reste se partage egalement
        const float extra = (width - sum) / static_cast<float>(n);
        for (std::size_t i = 0; i < n; ++i) out[i] = want[i] + extra;
        return out;
    }
    if (minimum * static_cast<float>(n) >= width) return out;   // rien ne tient : chacune son minimum
    // Les plus larges cedent : un plafond commun, tel que la somme fasse `width`.
    std::vector<float> sorted = want;
    std::sort(sorted.begin(), sorted.end());
    float rest = width, cap = sorted.back();
    for (std::size_t i = 0; i < n; ++i) {
        const float share = rest / static_cast<float>(n - i);
        if (sorted[i] > share) {
            cap = share;
            break;
        }
        rest -= sorted[i];
    }
    for (std::size_t i = 0; i < n; ++i) out[i] = std::min(want[i], cap);
    return out;
}

namespace {
const gfx::FontId kGridFont{16};   // ui et uiBold : la meme taille (pas de face grasse)
}

std::vector<float> HmiDuplicateDialog::gridColumns(float width) const {
    const auto names = columns();
    std::vector<float> need(names.size(), 0.f);
    for (std::size_t c = 0; c < names.size(); ++c) {
        std::string head = names[c];
        if (c > 0 && c - 1 < scan_.markers.size()) head += scan_.markers[c - 1].variable ? "  variable" : "  texte";
        float w = ui::measureWidth(head, kGridFont) + 16.f;   // l'en-tete, et l'ecart avant la suivante
        const std::string key = c > 0 && c - 1 < scan_.markers.size() ? dup::markerKey(scan_.markers[c - 1].name) : std::string{};
        for (const auto& row : plan_.rows) {
            std::string t;
            if (c == 0) t = row.name;
            else if (const auto it = row.markers.find(key); it != row.markers.end()) t = it->second;
            if (t.empty() && c > 0) t = names[c];   // l'AVANT en gris : le repere
            w = std::max(w, ui::measureWidth(t, kGridFont) + 32.f);   // la case : ses marges, le curseur, l'ecart
        }
        need[c] = w;
    }
    return fitColumns(need, width, 120.f);
}

std::string HmiDuplicateDialog::gridHeader(std::size_t c, float width) const {
    const auto names = columns();
    if (c >= names.size()) return {};
    const std::string& name = names[c];
    if (c > 0 && c - 1 < scan_.markers.size()) {
        const std::string full = name + (scan_.markers[c - 1].variable ? "  variable" : "  texte");
        if (ui::measureWidth(full, kGridFont) <= width) return full;
    }
    if (ui::measureWidth(name, kGridFont) <= width) return name;
    // Coupe au milieu, par caracteres entiers : le debut (la variable) et la fin
    // (l'indice qui varie) restent lisibles.
    std::vector<std::string> glyphs;
    for (std::size_t i = 0; i < name.size();) {
        std::size_t j = i + 1;
        while (j < name.size() && (static_cast<unsigned char>(name[j]) & 0xC0) == 0x80) ++j;
        glyphs.push_back(name.substr(i, j - i));
        i = j;
    }
    for (std::size_t keep = glyphs.size(); keep-- > 0;) {
        std::string s;
        const std::size_t left = (keep + 1) / 2, right = keep / 2;
        for (std::size_t i = 0; i < left; ++i) s += glyphs[i];
        s += "\xE2\x80\xA6";
        for (std::size_t i = glyphs.size() - right; i < glyphs.size(); ++i) s += glyphs[i];
        if (ui::measureWidth(s, kGridFont) <= width) return s;
    }
    return "\xE2\x80\xA6";
}

int HmiDuplicateDialog::columnIndex(std::string_view column) const {
    const auto cols = columns();
    for (std::size_t i = 0; i < cols.size(); ++i)
        if (dup::markerKey(cols[i]) == dup::markerKey(column)) return static_cast<int>(i);
    // "Vanne" sans les $ : la colonne du repere.
    for (std::size_t i = 0; i < scan_.markers.size(); ++i)
        if (dup::markerKey(scan_.markers[i].name) == dup::markerKey(column)) return static_cast<int>(i + 1);
    return -1;
}

std::string HmiDuplicateDialog::cell(int row, std::string_view column) const {
    const int c = columnIndex(column);
    if (row < 0 || static_cast<std::size_t>(row) >= plan_.rows.size() || c < 0) return {};
    const auto& r = plan_.rows[static_cast<std::size_t>(row)];
    if (c == 0) return r.name;
    const auto it = r.markers.find(dup::markerKey(scan_.markers[static_cast<std::size_t>(c - 1)].name));
    return it == r.markers.end() ? std::string{} : it->second;
}

bool HmiDuplicateDialog::setCell(int row, std::string_view column, std::string value) {
    const int c = columnIndex(column);
    if (row < 0 || static_cast<std::size_t>(row) >= plan_.rows.size() || c < 0) return false;
    auto& r = plan_.rows[static_cast<std::size_t>(row)];
    if (c == 0) r.name = value;
    else r.markers[dup::markerKey(scan_.markers[static_cast<std::size_t>(c - 1)].name)] = value;
    if (grid_ && static_cast<std::size_t>(row) < grid_->cells.size() && static_cast<std::size_t>(c) < grid_->cells[static_cast<std::size_t>(row)].size()) {
        syncing_ = true;
        grid_->cells[static_cast<std::size_t>(row)][static_cast<std::size_t>(c)]->setText(std::move(value));
        syncing_ = false;
    }
    chosen_ = row;
    refresh();
    return true;
}

void HmiDuplicateDialog::setCopies(int copies) {
    const auto* v = view();
    if (!v) return;
    copies = std::clamp(copies, 0, 500);           // 1.10.4 : 0 - les reperes de l'original seulement
    if (copies == plan_.copies()) return;
    // Les indices suivis, tels qu'ils sont regles (1.11, REP-4 : les cases "Suivre", meme sans copie).
    const std::vector<bool> following = followed_;
    const auto fresh = dup::defaultPlan(*v, scan_, copies);
    plan_.rows.resize(static_cast<std::size_t>(copies) + 1);
    for (std::size_t r = 1; r < plan_.rows.size(); ++r) {
        auto& row = plan_.rows[r];
        if (row.name.empty() && r < fresh.rows.size()) row.name = fresh.rows[r].name;
        row.indices.clear();
        for (std::size_t i = 0; i < scan_.indices.size(); ++i)
            if (i < following.size() && following[i]) row.indices[dup::indexKey(scan_.indices[i].path, scan_.indices[i].value)] = scan_.indices[i].value + static_cast<long long>(r);
        // 1.10.4 : la colonne preremplie (ou remplie par Tableau...) suit le nombre de copies.
        if (prefilled_) {
            auto& cellValue = row.markers[dup::markerKey(prefilled_->marker)];
            if (cellValue.empty())
                cellValue = prefilled_->array + "[" + std::to_string(prefilled_->from + static_cast<long long>(r)) + "]" + prefilled_->rest;
        }
        // 1.11 (REP) : un repere a indices, son morceau decale (une case vide seulement).
        if (r < fresh.rows.size())
            for (const auto& [key, value] : fresh.rows[r].markers)
                if (auto& cellValue = row.markers[key]; cellValue.empty()) cellValue = value;
    }
    if (chosen_ > copies) chosen_ = copies;
    if (copiesField_ && copiesField_->text() != std::to_string(copies)) {
        syncing_ = true;
        copiesField_->setText(std::to_string(copies));
        syncing_ = false;
    }
    rebuildGrid();
    refresh();
}

bool HmiDuplicateDialog::fillSeries(std::string_view column, std::string_view pattern, std::string_view from, bool fromOriginal) {
    if (columnIndex(column) < 0 || pattern.empty()) return false;
    // 1.10.4 : la colonne preremplie, remplie autrement : la note s'en va.
    if (const int pc = columnIndex(column); pc > 0 && prefilled_
        && dup::markerKey(prefilled_->marker) == dup::markerKey(scan_.markers[static_cast<std::size_t>(pc - 1)].name)) {
        prefilled_.reset();
        note_.clear();
    }
    const std::size_t first = fromOriginal ? 0 : 1;
    const auto values = dup::seriesN(pattern, from.empty() ? std::string_view("1") : from, plan_.rows.size() - first);
    for (std::size_t i = 0; i < values.size(); ++i) setCell(static_cast<int>(first + i), column, values[i]);
    return !values.empty();
}

bool HmiDuplicateDialog::fillList(std::string_view column, std::string_view pasted, bool fromOriginal) {
    if (columnIndex(column) < 0) return false;
    // 1.10.4 : la colonne preremplie, remplie autrement : la note s'en va.
    if (const int pc = columnIndex(column); pc > 0 && prefilled_
        && dup::markerKey(prefilled_->marker) == dup::markerKey(scan_.markers[static_cast<std::size_t>(pc - 1)].name)) {
        prefilled_.reset();
        note_.clear();
    }
    const auto values = dup::pastedList(pasted);
    if (values.empty()) return false;
    const std::size_t first = fromOriginal ? 0 : 1;
    // Le nombre de copies suit la liste.
    setCopies(static_cast<int>(values.size() + first) - 1);
    for (std::size_t i = 0; i < values.size(); ++i) setCell(static_cast<int>(first + i), column, values[i]);
    return true;
}

std::vector<std::string> HmiDuplicateDialog::arrays() const {
    std::vector<std::string> out;
    if (!spec_.doc) return out;
    // 1.11.2 (SYM) : dans un symbole, ses parametres tableaux en tete (Value (0..9)).
    if (const auto* v = view(); v && hmi::isSymbolView(*v))
        for (const auto& [name, b] : dup::symbolParamArrays(*v))
            out.push_back(name + " (" + std::to_string(b.first) + ".." + std::to_string(b.second) + ")");
    for (const auto& [name, b] : dup::projectArrays(spec_.doc->project))
        out.push_back(name + " (" + std::to_string(b.first) + ".." + std::to_string(b.second) + ")");
    return out;
}

bool HmiDuplicateDialog::fillArray(std::string_view column, std::string_view array) {
    const int c = columnIndex(column);
    if (c <= 0 || array.empty() || !spec_.doc) return false;      // pas la colonne "Nom"
    // "V (0..63)" (la liste) ou "V".
    std::string name(array.substr(0, array.find(" (")));
    // Depuis l'indice de l'original : celui que l'objet suit, sinon la borne basse.
    long long from = 0;
    bool known = false;
    // 1.11 (REP) : un repere qui est un element de ce tableau ($V[1].Ouv$) : depuis son
    // indice, et sa suite (.Ouv) gardee.
    std::string rest;
    if (const auto e = dup::elementRef(scan_.markers[static_cast<std::size_t>(c - 1)].name);
        e && dup::markerKey(e->array) == dup::markerKey(name)) {
        from = e->value;
        rest = e->rest;
        known = true;
    }
    if (!known)
        for (const auto& k : scan_.indices)
            if (dup::markerKey(k.path) == dup::markerKey(name)) { from = k.value; known = true; break; }
    const auto* v = view();
    const auto bounds = boundsOf(spec_.doc, v)(name);                // 1.11.2 (D13) : API.V (0..63) aussi ; (SYM) : Value (0..9)
    if (!known && bounds) from = bounds->first;
    if (!known && !bounds && !(spec_.exists && spec_.exists(name))) return false;   // pas un tableau connu
    if (bounds) {
        bool written = false;
        // 1.11.2 (SYM) : un parametre du symbole s'ecrit comme dans le symbole.
        if (v && hmi::isSymbolView(*v))
            for (const auto& [n, b] : dup::symbolParamArrays(*v))
                if (dup::markerKey(n) == dup::markerKey(name)) { name = n; written = true; break; }
        if (!written)
            for (const auto& [n, b] : dup::projectArrays(spec_.doc->project))
                if (dup::markerKey(n) == dup::markerKey(name)) name = n;       // ecrit comme dans le projet
    }
    const auto values = dup::arrayColumn(name, from, plan_.rows.size());
    for (std::size_t i = 0; i < values.size(); ++i) setCell(static_cast<int>(i), column, values[i] + rest);
    prefilled_ = dup::Prefill{scan_.markers[static_cast<std::size_t>(c - 1)].name, name, from, rest};
    note_.clear();
    refresh();
    return true;
}

bool HmiDuplicateDialog::clearColumn(std::string_view column) {
    const int c = columnIndex(column);
    if (c < 0) return false;
    // 1.10.4 : la colonne preremplie se vide entiere (l'original compris), et sa note s'en va.
    const bool prefilledColumn = c > 0 && prefilled_ && dup::markerKey(prefilled_->marker) == dup::markerKey(scan_.markers[static_cast<std::size_t>(c - 1)].name);
    if (prefilledColumn) {
        prefilled_.reset();
        note_.clear();
        if (!plan_.rows.empty()) setCell(0, column, {});
    }
    for (std::size_t r = 1; r < plan_.rows.size(); ++r) setCell(static_cast<int>(r), column, {});
    return true;
}

void HmiDuplicateDialog::setKeep(std::string_view marker, bool keep) {
    const int c = columnIndex(marker);
    if (c <= 0) return;
    const std::string key = dup::markerKey(scan_.markers[static_cast<std::size_t>(c - 1)].name);
    keep_.erase(std::remove(keep_.begin(), keep_.end(), key), keep_.end());
    if (keep) keep_.push_back(key);
    const auto i = static_cast<std::size_t>(c - 1);
    if (i < keepBoxes_.size() && keepBoxes_[i]->isChecked() != keep)
        keepBoxes_[i]->setState(keep ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    refresh();
}

void HmiDuplicateDialog::setFollow(std::size_t index, bool follow) {
    if (index >= scan_.indices.size()) return;
    const auto& k = scan_.indices[index];
    const std::string key = dup::indexKey(k.path, k.value);
    if (index < followed_.size()) followed_[index] = follow;   // 1.11 (REP-4)
    for (std::size_t r = 1; r < plan_.rows.size(); ++r) {
        if (follow) plan_.rows[r].indices[key] = k.value + static_cast<long long>(r);
        else plan_.rows[r].indices.erase(key);
    }
    if (index < follow_.size() && follow_[index]->isChecked() != follow)
        follow_[index]->setState(follow ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    refresh();
}

void HmiDuplicateDialog::setReplaceOriginal(bool on) {
    plan_.replaceOriginal = on;
    if (replaceOriginal_ && replaceOriginal_->isChecked() != on)
        replaceOriginal_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    refresh();
}

void HmiDuplicateDialog::setLayout(const hmi::dup::Layout& layout) {
    plan_.layout = layout;
    plan_.layout.columns = std::max(1, plan_.layout.columns);
    plan_.layout.spacing = std::max(0.0, plan_.layout.spacing);
    syncing_ = true;
    if (axis_) axis_->setValue(static_cast<int>(plan_.layout.axis));
    if (columnsField_) columnsField_->setText(std::to_string(plan_.layout.columns));
    if (spacingField_) spacingField_->setText(std::to_string(static_cast<long>(std::lround(plan_.layout.spacing))));
    syncing_ = false;
    refresh();
}

bool HmiDuplicateDialog::fitSpacing() {
    const auto* v = view();
    if (!v) return false;
    const auto s = dup::fittingSpacing(*v, scan_.box, plan_.copies(), plan_.layout);
    if (!s) return false;
    auto l = plan_.layout;
    l.spacing = *s;
    setLayout(l);
    return true;
}

std::vector<int> HmiDuplicateDialog::overflow() const {
    const auto* v = view();
    return v ? dup::overflowing(*v, scan_.box, plan_.copies(), plan_.layout) : std::vector<int>{};
}

std::vector<int> HmiDuplicateDialog::overlap() const {
    const auto* v = view();
    return v ? dup::overlapping(*v, scan_.units, plan_.copies(), plan_.layout) : std::vector<int>{};
}

void HmiDuplicateDialog::showTab(std::size_t index) {
    if (tabs_) tabs_->setCurrentIndex(index);
}

std::size_t HmiDuplicateDialog::currentTab() const { return tabs_ ? tabs_->currentIndex() : 0; }

void HmiDuplicateDialog::chooseRow(int row) {
    chosen_ = std::clamp(row, 0, std::max(0, plan_.copies()));
    if (copiesPage_) copiesPage_->invalidate();
    if (grid_) grid_->invalidate();
}

std::vector<std::string> HmiDuplicateDialog::beforeAfter(int row) const {
    std::vector<std::string> out;
    if (row < 0 || static_cast<std::size_t>(row) >= plan_.rows.size()) return out;
    const auto& r = plan_.rows[static_cast<std::size_t>(row)];
    std::set<std::pair<hmi::Id, std::string>> seen;
    std::map<hmi::Id, Labels> labels;     // 1.11 (T2-22) : les libelles de l'inspecteur, une fois par objet
    const hmi::View* v = view();
    const hmi::Project* project = spec_.doc ? &spec_.doc->project : nullptr;
    const auto add = [&](const dup::Spot& s) {
        if (!seen.insert({s.object, s.where}).second || out.size() >= 7) return;
        std::string after = s.text;
        if (row > 0 || plan_.replaceOriginal)
            after = dup::replaceMarkers(dup::followIndices(s.text, r.indices), r.markers, nullptr, nullptr, s.expression);
        auto it = labels.find(s.object);
        if (it == labels.end()) it = labels.emplace(s.object, v ? inspectorLabels(*v, s.object, project) : Labels{}).first;
        out.push_back(s.objectName + " \xC2\xB7 " + spotLabel(it->second, s.where) + " :  " + s.text + "   \xE2\x86\x92   " + after);
    };
    for (const auto& m : scan_.markers)
        for (const auto& s : m.spots) add(s);
    for (const auto& k : scan_.indices)
        for (const auto& s : k.spots) add(s);
    if (out.empty()) out.push_back(row == 0 ? "L'original ne change pas." : "Une copie de l'original, pos\xC3\xA9" "e \xC3\xA0 c\xC3\xB4t\xC3\xA9.");
    return out;
}

bool HmiDuplicateDialog::canConfirm() const {
    // 1.10.4 : 0 copie ("Remplacer..." de Compiler) - les reperes de l'original.
    const bool something = plan_.copies() > 0 || (plan_.replaceOriginal && !scan_.markers.empty());
    return !done_ && something && !check_.blocked();
}

std::string HmiDuplicateDialog::confirmLabel() const {
    const int n = plan_.copies();
    if (n == 0) return "Remplacer dans l'original";      // 1.10.4
    return "Dupliquer (" + std::to_string(n) + (n > 1 ? " copies)" : " copie)");
}

std::string HmiDuplicateDialog::blockingText() const {
    if (!check_.blocked()) {
        std::size_t reps = 0;
        for (const auto& m : scan_.markers) reps += m.uses;
        if (plan_.copies() == 0)      // 1.10.4 : "Remplacer..." de Compiler
            return plan_.replaceOriginal ? "Aucune copie : " + plural(reps, "rep\xC3\xA8re remplac\xC3\xA9", "rep\xC3\xA8res remplac\xC3\xA9s")
                                               + " dans l'original. Ctrl+Z le rend."
                                         : std::string("Aucune copie, et l'original garde ses rep\xC3\xA8res : coche \xC2\xAB Remplacer aussi dans l'original \xC2\xBB.");
        return plural(static_cast<std::size_t>(plan_.copies()), "copie", "copies") + ", " + plural(reps, "rep\xC3\xA8re", "rep\xC3\xA8res")
             + " par copie. Ctrl+Z retire tout.";
    }
    const std::size_t empty = check_.count(dup::CellState::Empty);
    const std::size_t bad = check_.cells.size() - empty;
    std::string s;
    if (empty) s += plural(empty, "case vide", "cases vides");
    if (bad) s += (s.empty() ? "" : ", ") + plural(bad, "case en rouge", "cases en rouge");
    for (const auto& c : check_.cells) {
        if (!c.blocks) continue;
        s += " \xE2\x80\x94 ligne " + std::to_string(c.row + 1) + ", " + c.column + " : " + c.message;
        if (!c.suggestions.empty()) s += " ; veux-tu dire " + c.suggestions.front() + " ?";
        break;
    }
    return s;
}

void HmiDuplicateDialog::refresh() {
    validate();
    if (ok_) {
        const std::string label = confirmLabel();
        if (label != ok_->text()) ok_->setText(label);
        ok_->setEnabled(canConfirm());
    }
    if (fit_) fit_->setEnabled(!overflow().empty());
    if (body_) {
        body_->invalidateLayout();
        body_->invalidate();
    }
    if (copiesPage_) copiesPage_->invalidate();
    if (layoutPage_) layoutPage_->invalidate();
    if (grid_) grid_->invalidateLayout();   // 1.11.1 (R1111-7) : les colonnes suivent ce qu'on ecrit
}

void HmiDuplicateDialog::rebuildGrid() {
    if (!gridPanel_) return;
    // La case qui a le focus va disparaitre (Coller, le nombre de copies) : le
    // focus ne doit pas garder un pointeur sur elle.
    if (grid_)
        for (const auto& line : grid_->cells)
            for (const auto* cell : line)
                if (cell == focus().current()) focus().clear();
    gridLinks_.clear();
    auto g = std::make_unique<Grid>(*this);
    grid_ = g.get();
    const auto cols = columns();
    const auto* v = view();
    std::string originalName;
    if (v && !scan_.units.empty())
        if (const auto* o = v->object(scan_.units.front())) originalName = o->name;
    for (std::size_t r = 0; r < plan_.rows.size(); ++r) {
        std::vector<ui::InputText*> line;
        for (std::size_t c = 0; c < cols.size(); ++c) {
            auto f = std::make_unique<ui::InputText>("dialog.hmiDuplicate.cell." + std::to_string(r) + "." + std::to_string(c));
            f->setText(cell(static_cast<int>(r), cols[c]));
            // L'AVANT en gris : le nom de l'original, le repere.
            f->setPlaceholder(c == 0 ? originalName : cols[c]);
            if (c > 0 && spec_.assist) f->setAssist(spec_.assist);
            auto* raw = &static_cast<ui::InputText&>(g->addChild(std::move(f)));
            gridLinks_ += raw->textChanged->connect([this, r, c](const std::string& t) {
                if (syncing_) return;
                auto& row = plan_.rows[r];
                if (c == 0) row.name = t;
                else row.markers[dup::markerKey(scan_.markers[c - 1].name)] = t;
                chosen_ = static_cast<int>(r);
                refresh();
            });
            line.push_back(raw);
        }
        g->cells.push_back(std::move(line));
    }
    gridPanel_->setContent(std::move(g));
    if (copiesPage_) copiesPage_->invalidateLayout();
}

core::Status HmiDuplicateDialog::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    const std::string base = "dialog.hmiDuplicate";
    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");

    // ---- "Les copies et leurs reperes" ----
    auto copies = std::make_unique<CopiesPage>(*this);
    copiesPage_ = copies.get();
    for (std::size_t i = 0; i < scan_.indices.size(); ++i) {
        const auto& k = scan_.indices[i];
        auto box = std::make_unique<ui::Checkbox>("Suivre " + k.label() + " (sinon garder)", base + ".follow" + std::to_string(i));
        const bool on = following(i);   // 1.11 (REP-4) : cochee si aucun repere ne porte d'indice
        box->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        auto* raw = &static_cast<ui::Checkbox&>(copies->addChild(std::move(box)));
        follow_.push_back(raw);
        copies->checks.push_back(raw);
        links_ += raw->stateChanged->connect([this, i](ui::Checkbox::State s) { if (!syncing_) setFollow(i, s == ui::Checkbox::State::Checked); });
    }
    for (std::size_t i = 0; i < scan_.markers.size(); ++i) {
        auto box = std::make_unique<ui::Checkbox>("Vide : garder " + markerLabel(scan_.markers[i].name), base + ".keep" + std::to_string(i));
        auto* raw = &static_cast<ui::Checkbox&>(copies->addChild(std::move(box)));
        keepBoxes_.push_back(raw);
        copies->checks.push_back(raw);
        const std::string name = scan_.markers[i].name;
        links_ += raw->stateChanged->connect([this, name](ui::Checkbox::State s) { if (!syncing_) setKeep(name, s == ui::Checkbox::State::Checked); });
    }
    if (!scan_.markers.empty() || !scan_.indices.empty()) {
        auto box = std::make_unique<ui::Checkbox>("Remplacer aussi dans l'original", base + ".original");
        box->setState(plan_.replaceOriginal ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        replaceOriginal_ = &static_cast<ui::Checkbox&>(copies->addChild(std::move(box)));
        copies->checks.push_back(replaceOriginal_);
        links_ += replaceOriginal_->stateChanged->connect([this](ui::Checkbox::State s) { if (!syncing_) setReplaceOriginal(s == ui::Checkbox::State::Checked); });
    }
    {
        auto f = std::make_unique<ui::InputText>(base + ".copies");
        f->setText(std::to_string(plan_.copies()));
        copiesField_ = &static_cast<ui::InputText&>(copies->addChild(std::move(f)));
        copies->toolbar.push_back(copiesField_);
        links_ += copiesField_->textChanged->connect([this](const std::string& t) {
            if (syncing_) return;
            const int n = std::atoi(t.c_str());
            if (n >= 1 || t == "0") setCopies(n);      // 1.10.4 : 0 - l'original seulement
        });
        auto dd = std::make_unique<ui::DropDown>(base + ".fillColumn");
        std::vector<ui::DropDown::Item> items;
        for (const auto& c : columns()) items.push_back({c, c, {}, true});
        dd->setItems(std::move(items));
        dd->setSelectedIndex(scan_.markers.empty() ? 0 : 1);
        fillColumn_ = &static_cast<ui::DropDown&>(copies->addChild(std::move(dd)));
        copies->toolbar.push_back(fillColumn_);
        auto p = std::make_unique<ui::InputText>(base + ".fillPattern");
        p->setPlaceholder("V1{n}");
        fillPattern_ = &static_cast<ui::InputText&>(copies->addChild(std::move(p)));
        copies->toolbar.push_back(fillPattern_);
        auto fr = std::make_unique<ui::InputText>(base + ".fillFrom");
        fr->setText("02");
        fillFrom_ = &static_cast<ui::InputText&>(copies->addChild(std::move(fr)));
        copies->toolbar.push_back(fillFrom_);
        const auto column = [this] {
            const auto* it = fillColumn_ ? fillColumn_->selectedItem() : nullptr;
            return it ? it->value : std::string("Nom");
        };
        auto* series = &static_cast<ui::Button&>(copies->addChild(std::make_unique<ui::Button>("S\xC3\xA9rie", base + ".fillSeries")));
        copies->toolbar.push_back(series);
        links_ += series->clicked->connect([this, column] { fillSeries(column(), fillPattern_->text(), fillFrom_->text()); });
        // 1.10.4 : Remplir > Tableau... - un tableau du projet : V[0], V[1]... dans la colonne.
        auto ta = std::make_unique<ui::DropDown>(base + ".fillArray");
        std::vector<ui::DropDown::Item> arrayItems{{"Tableau\xE2\x80\xA6", "", {}, true}};
        for (const auto& a : arrays()) arrayItems.push_back({a, a, {}, true});
        ta->setItems(std::move(arrayItems));
        ta->setSelectedIndex(0);
        fillArray_ = &static_cast<ui::DropDown&>(copies->addChild(std::move(ta)));
        copies->toolbar.push_back(fillArray_);
        links_ += fillArray_->selectionChanged->connect([this, column](int i) {
            if (syncing_ || i <= 0) return;
            const auto* it = fillArray_->selectedItem();
            const std::string chosen = it ? it->value : std::string{};
            if (!chosen.empty()) fillArray(column(), chosen);
            syncing_ = true;
            fillArray_->setSelectedIndex(0);
            syncing_ = false;
        });
        auto* paste = &static_cast<ui::Button&>(copies->addChild(std::make_unique<ui::Button>("Coller (Excel)", base + ".fillPaste")));
        copies->toolbar.push_back(paste);
        links_ += paste->clicked->connect([this, column] { fillList(column(), ui::clipboardText()); });
        auto* clear = &static_cast<ui::Button&>(copies->addChild(std::make_unique<ui::Button>("Vider", base + ".fillClear")));
        copies->toolbar.push_back(clear);
        links_ += clear->clicked->connect([this, column] { clearColumn(column()); });
    }
    {
        auto panel = std::make_unique<ui::ScrollablePanel>(base + ".grid");
        panel->setScrollPolicy(false, true);
        gridPanel_ = &static_cast<ui::ScrollablePanel&>(copies->addChild(std::move(panel)));
        rebuildGrid();
    }
    tabs->addTab(ui::TabControl::Tab{"Les copies et leurs rep\xC3\xA8res"}, std::move(copies));

    // ---- "La disposition" ----
    auto lay = std::make_unique<LayoutPage>(*this);
    layoutPage_ = lay.get();
    axis_ = std::make_shared<ui::RadioGroup>();
    const char* axes[] = {"Sur X", "Sur Y", "En grille"};
    for (int i = 0; i < 3; ++i)
        lay->radios.push_back(&static_cast<ui::RadioButton&>(lay->addChild(std::make_unique<ui::RadioButton>(axes[i], axis_, i, base + ".axis" + std::to_string(i)))));
    axis_->setValue(static_cast<int>(plan_.layout.axis));
    links_ += axis_->valueChanged->connect([this](int v) {
        if (syncing_) return;
        auto l = plan_.layout;
        l.axis = static_cast<dup::Axis>(std::clamp(v, 0, 2));
        setLayout(l);
    });
    {
        auto f = std::make_unique<ui::InputText>(base + ".columns");
        f->setText(std::to_string(plan_.layout.columns));
        columnsField_ = &static_cast<ui::InputText&>(lay->addChild(std::move(f)));
        links_ += columnsField_->textChanged->connect([this](const std::string& t) {
            if (syncing_) return;
            const int n = std::atoi(t.c_str());
            if (n < 1) return;
            auto l = plan_.layout;
            l.columns = n;
            setLayout(l);
        });
        auto s = std::make_unique<ui::InputText>(base + ".spacing");
        s->setText(std::to_string(static_cast<long>(std::lround(plan_.layout.spacing))));
        spacingField_ = &static_cast<ui::InputText&>(lay->addChild(std::move(s)));
        links_ += spacingField_->textChanged->connect([this](const std::string& t) {
            if (syncing_ || t.empty()) return;
            auto l = plan_.layout;
            l.spacing = std::atof(t.c_str());
            setLayout(l);
        });
        fit_ = &static_cast<ui::Button&>(lay->addChild(std::make_unique<ui::Button>("Ajuster l'espacement", base + ".fit")));
        links_ += fit_->clicked->connect([this] { fitSpacing(); });
    }
    lay->preview = &static_cast<Preview&>(lay->addChild(std::make_unique<Preview>(*this)));
    tabs->addTab(ui::TabControl::Tab{"La disposition"}, std::move(lay));
    tabs_ = &static_cast<ui::TabControl&>(body->addChild(std::move(tabs)));

    // ---- Annuler, Dupliquer ----
    auto* cancel = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", base + ".cancel")));
    body->buttons.push_back(cancel);
    links_ += cancel->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(confirmLabel(), base + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    body->buttons.push_back(ok_);
    links_ += ok_->clicked->connect([this] { confirm(); });
    setRoot(std::move(body));
    refresh();
    return core::ok();
}

ui::EventResult HmiDuplicateDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // Ctrl+Entree : "Dupliquer" (Entree choisit dans l'aide d'une case).
        if (k->key == ui::Key::Return && k->mods.ctrl) {
            confirm();
            return ui::EventResult::Consumed;
        }
    }
    const auto r = menu::WidgetMenu::HandleEvent(ev);
    if (r == ui::EventResult::Consumed) return r;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        finish(false);
        return ui::EventResult::Consumed;
    }
    return r;
}

bool HmiDuplicateDialog::confirm() {
    if (!canConfirm()) return false;
    finish(true);
    return true;
}

void HmiDuplicateDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    if (ok && spec_.apply) {
        const auto plan = plan_;    // la fenetre se ferme ensuite
        spec_.apply(plan);
    }
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, {}});
}

} // namespace app
