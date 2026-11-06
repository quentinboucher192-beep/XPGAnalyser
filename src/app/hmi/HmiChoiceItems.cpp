// =============================================================================
//  app/hmi/HmiChoiceItems.cpp - 1.26.1 : les listes longues de l'inspecteur
// =============================================================================
#include "HmiChoiceItems.hpp"

#include "HmiImages.hpp"      // une ligne ecrite dans sa police (HmiImageCache::fontText)
#include "HmiPainter.hpp"     // parseColor
#include "../../hmi/HmiFontLibrary.hpp"
#include "../../hmi/HmiStyles.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <string>

namespace app {

namespace {

// `text` ecrit dans la police `data`, centre dans la hauteur de `r` ; sans image (une police illisible, un renderer de
// test) : dans celle de l'interface.
void writeInFont(const ui::PaintContext& ctx, const hmi::BlobPtr& data, const std::string& format, gfx::Rect r, gfx::Color ink,
                 std::string_view text) {
    const float px = std::max(10.f, std::min(17.f, r.h * 0.66f));
    if (data && !text.empty()) {
        hmi::Resource res;
        res.format = format.empty() ? std::string("TTF") : format;
        res.data = data;
        const auto img = HmiImageCache::instance().fontText(ctx.r, res, text, px);
        if (img.valid()) {
            const float h = static_cast<float>(img.height);
            ctx.r.drawImage(img.tex, {r.x, r.y + (r.h - h) * 0.5f, static_cast<float>(img.width), h}, 0.f, false, false, ink);
            return;
        }
    }
    ctx.r.drawText({r.x, r.y + (r.h - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f}, text, ctx.theme.font.ui, ink);
}

std::string valueOf(const std::vector<hmi::Prop>& props, std::initializer_list<const char*> keys) {
    for (const char* k : keys)
        for (const auto& p : props)
            if (p.key == k && !p.value.empty()) return p.value;
    return {};
}

} // namespace

std::vector<ui::DropDown::Item> hmiFontChoiceItems(const hmi::Project& p, bool theme) {
    std::vector<ui::DropDown::Item> out;
    for (const auto& c : hmi::fontlib::fieldChoices(p, theme)) {
        ui::DropDown::Item it;
        it.label = c.shortLabel.empty() ? c.label : c.shortLabel;
        it.value = c.label;                     // ce que la case ecrit (son commit relit l'etiquette)
        it.group = c.section;
        it.row = c.family;
        it.variant = c.family.empty() ? std::string{} : c.style;
        it.note = c.note;
        it.keywords = c.label + " " + c.family + " " + c.style + (c.libraryId.empty() ? std::string{} : std::string(" importer biblioth\xC3\xA8que"));
        if (c.data) {
            const hmi::BlobPtr data = c.data;
            const std::string format = c.format;
            it.paint = [data, format](const ui::PaintContext& ctx, gfx::Rect r, gfx::Color ink, std::string_view text) {
                writeInFont(ctx, data, format, r, ink, text);
            };
        }
        out.push_back(std::move(it));
    }
    return out;
}

std::vector<ui::DropDown::Item> hmiStyleChoiceItems(const hmi::Project& p, const hmi::Object* forObject) {
    std::vector<ui::DropDown::Item> out;
    {
        ui::DropDown::Item none;
        none.label = "(aucun)";
        none.value = "";
        none.keywords = "aucun sans style";
        out.push_back(std::move(none));
    }
    const auto styles = hmi::styles::all(p);
    // Du projet d'abord (par dossier), puis les fournis.
    std::vector<const hmi::Style*> mine, given;
    for (const auto* st : styles) (st->builtin ? given : mine).push_back(st);
    std::stable_sort(mine.begin(), mine.end(), [](const hmi::Style* a, const hmi::Style* b) { return a->folder < b->folder; });
    const auto add = [&](const hmi::Style& st, std::string group) {
        ui::DropDown::Item it;
        it.label = st.name;
        it.value = st.name;
        it.group = std::move(group);
        it.keywords = st.description + " " + st.folder;
        if (const std::size_t n = hmi::styles::users(p, st.name); n > 0) it.note = std::to_string(n) + (n > 1 ? " objets" : " objet");
        // L'apercu : ce que le style donne a l'objet choisi (sinon a tous, ou a son premier type).
        std::vector<hmi::Prop> values = forObject ? hmi::styles::valuesFor(p, st, *forObject) : st.props;
        if (!forObject && valueOf(values, {"fill", "background"}).empty() && !st.targets.empty())
            for (const auto& t : st.targets) {
                values.insert(values.end(), t.props.begin(), t.props.end());
                if (!valueOf(values, {"fill", "background"}).empty()) break;
            }
        const std::string fill = valueOf(values, {"fill", "background", "colorOff"});
        const std::string stroke = valueOf(values, {"stroke", "border", "borderColor"});
        const std::string text = valueOf(values, {"textColor", "color", "foreground"});
        it.paint = [fill, stroke, text](const ui::PaintContext& ctx, gfx::Rect r, gfx::Color ink, std::string_view label) {
            const gfx::Rect sw{r.x, r.y + 4.f, 30.f, r.h - 8.f};
            const auto& c = ctx.theme.color;
            const gfx::Color f = fill.empty() ? c.inputBg : parseColor(fill, c.inputBg);
            ctx.r.fillRoundedRect(sw, f, 3.f);
            ctx.r.strokeRect(sw, stroke.empty() ? c.border : parseColor(stroke, c.border), 1.f);
            if (!text.empty()) {
                const gfx::Color t = parseColor(text, c.text);
                const std::string_view aa = "Aa";
                const float w = ctx.r.measure(aa, ctx.theme.font.smallUi).width;
                ctx.r.drawText({sw.x + (sw.w - w) * 0.5f, sw.y + (sw.h - ctx.r.lineHeight(ctx.theme.font.smallUi)) * 0.5f}, aa,
                               ctx.theme.font.smallUi, t);
            }
            ctx.r.drawText({sw.right() + 8.f, r.y + (r.h - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f}, label, ctx.theme.font.ui, ink);
        };
        out.push_back(std::move(it));
    };
    for (const auto* st : mine) add(*st, st->folder.empty() ? std::string("Du projet") : "Du projet \xE2\x80\xBA " + st->folder);
    for (const auto* st : given) add(*st, "Fournis (verrouill\xC3\xA9s)");
    return out;
}

} // namespace app
