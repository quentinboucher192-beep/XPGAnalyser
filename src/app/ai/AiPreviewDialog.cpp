// =============================================================================
//  app/ai/AiPreviewDialog.cpp - voir AiPreviewDialog.hpp
// =============================================================================
#include "AiPreviewDialog.hpp"

#include "AiPanel.hpp"
#include "../hmi/HmiPainter.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/ScrollBar.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

namespace app::ai {

namespace {

const gfx::FontId kTitleFont{20};
const gfx::FontId kText{14};
const gfx::FontId kSmall{13};
constexpr float kMaxW = 1240.f, kMaxH = 800.f;

gfx::Color alpha(gfx::Color c, int a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(a, 0, 255))); }

std::string elide(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float width) {
    if (width <= 8.f) return {};
    if (r.measure(text, f).width <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    const std::size_t n = r.fitCharacters(text, f, std::max(0.f, width - r.measure(dots, f).width));
    return text.substr(0, n) + dots;
}

// Un texte coupe aux mots pour tenir dans `width` (les avertissements et les erreurs se lisent en entier).
std::vector<std::string> wrap(const std::string& text, gfx::FontId f, float width) {
    std::vector<std::string> out;
    std::string line, word;
    const auto push = [&] {
        if (word.empty()) return;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(tryLine, f) > width) {
            out.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ') push();
        else word += c;
    }
    push();
    if (!line.empty()) out.push_back(line);
    if (out.empty()) out.emplace_back();
    return out;
}

// 1.16.1 : une fleche (un chevron) : 0 vers la droite, 1 vers le bas, 2 vers la gauche.
void chevron(gfx::IRenderer& r, gfx::Rect b, int dir, gfx::Color col) {
    const float cx = b.x + b.w / 2.f, cy = b.y + b.h / 2.f, s = std::min(b.w, b.h) * 0.3f;
    if (dir == 0) {
        r.line({cx - s / 2.f, cy - s}, {cx + s / 2.f, cy}, col, 1.6f);
        r.line({cx + s / 2.f, cy}, {cx - s / 2.f, cy + s}, col, 1.6f);
    } else if (dir == 1) {
        r.line({cx - s, cy - s / 2.f}, {cx, cy + s / 2.f}, col, 1.6f);
        r.line({cx, cy + s / 2.f}, {cx + s, cy - s / 2.f}, col, 1.6f);
    } else {
        r.line({cx + s / 2.f, cy - s}, {cx - s / 2.f, cy}, col, 1.6f);
        r.line({cx - s / 2.f, cy}, {cx + s / 2.f, cy + s}, col, 1.6f);
    }
}

// 1.16.1 : la cle d'un groupe de la liste - son titre sans son compte ("OBJETS CONCERNES (3)" -> "OBJETS CONCERNES").
std::string groupKey(const std::string& heading) {
    std::size_t cut = heading.find(" (");
    const std::size_t dash = heading.find(" \xE2\x80\x94");
    if (dash != std::string::npos && (cut == std::string::npos || dash < cut)) cut = dash;
    return cut == std::string::npos ? heading : heading.substr(0, cut);
}

// Une ligne de la liste de gauche.
struct Row {
    enum class Kind : std::uint8_t { Heading, Item, Change, Note, Warning, Error } kind{Kind::Note};
    std::string text, right;
    int         what{0};      // un objet : 0 creer, 1 modifier, 2 supprimer
};

} // namespace

// Le corps : le cadre, la liste a gauche, l'apercu a droite, les boutons.
class AiPreviewBody final : public ui::Widget {
public:
    explicit AiPreviewBody(AiPreviewDialog& d) : ui::Widget("ai.apercu.corps"), d_(d) {}
    std::vector<Row> rows;
    float            scroll{0.f};
    // 1.16.1 : LE ZOOM DE L'APERCU (1 : la vue entiere, jusqu'a 10), le decalage (glisser), la liste de gauche
    // (ses groupes replies, elle-meme masquee).
    float                 zoom{1.f};
    gfx::Point            pan{0.f, 0.f};
    std::set<std::string> folded;
    bool                  listHidden{false};

    [[nodiscard]] bool hidden(std::size_t i) const {   // une ligne d'un groupe replie
        for (std::size_t k = i + 1; k-- > 0;)
            if (rows[k].kind == Row::Kind::Heading) return k != i && folded.count(groupKey(rows[k].text)) > 0;
        return false;
    }
    // La page de la vue dans l'apercu, a ce zoom et ce decalage (vide : pas d'apercu).
    [[nodiscard]] bool pageGeometry(HmiViewport& vp, gfx::Rect& page) const {
        const Preview& pv = d_.preview_;
        if (!pv.ok || pv.view.width <= 0 || pv.view.height <= 0) return false;
        const float vw = static_cast<float>(pv.view.width), vh = static_cast<float>(pv.view.height);
        const float fit = std::min(area_.w / vw, area_.h / vh);
        const float z = fit * zoom;
        vp = HmiViewport{area_.x + (area_.w - vw * z) / 2.f + pan.x, area_.y + (area_.h - vh * z) / 2.f + pan.y, z};
        page = {vp.originX, vp.originY, vw * z, vh * z};
        return true;
    }
    void clampPan() {
        const Preview& pv = d_.preview_;
        if (!pv.ok || pv.view.width <= 0 || pv.view.height <= 0 || zoom <= 1.f) {
            pan = {0.f, 0.f};
            return;
        }
        const float fit = std::min(area_.w / static_cast<float>(pv.view.width), area_.h / static_cast<float>(pv.view.height));
        const float pw = static_cast<float>(pv.view.width) * fit * zoom, ph = static_cast<float>(pv.view.height) * fit * zoom;
        // La page se deplace tant qu'il en reste 40 points a voir (zoomer sous la souris garde l'objet sous elle).
        const float mx = std::max(0.f, (pw + area_.w) / 2.f - 40.f), my = std::max(0.f, (ph + area_.h) / 2.f - 40.f);
        pan.x = std::clamp(pan.x, -mx, mx);
        pan.y = std::clamp(pan.y, -my, my);
    }
    // Zoomer en gardant `at` (un point de l'ecran) sous la souris ; sans point : le centre de l'apercu.
    void setZoom(float z, std::optional<gfx::Point> at = std::nullopt) {
        z = std::clamp(z, 1.f, 10.f);
        HmiViewport vp;
        gfx::Rect page;
        if (!pageGeometry(vp, page)) {
            zoom = z;
            return;
        }
        const gfx::Point s = at.value_or(gfx::Point{area_.x + area_.w / 2.f, area_.y + area_.h / 2.f});
        const float vx = (s.x - vp.originX) / vp.zoom, vy = (s.y - vp.originY) / vp.zoom;
        zoom = z;
        pan = {0.f, 0.f};
        HmiViewport centered;
        if (pageGeometry(centered, page)) pan = {s.x - vx * centered.zoom - centered.originX, s.y - vy * centered.zoom - centered.originY};
        clampPan();
        invalidate();
    }
    // L'objet de la vue sous ce point de l'ecran (le dernier dessine) ; nul : aucun.
    [[nodiscard]] const hmi::Object* objectAt(gfx::Point at) const {
        HmiViewport vp;
        gfx::Rect page;
        if (!pageGeometry(vp, page) || !area_.contains(at) || !page.contains(at)) return nullptr;
        const double x = (at.x - vp.originX) / vp.zoom, y = (at.y - vp.originY) / vp.zoom;
        const auto& objects = d_.preview_.view.objects;
        for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
            const auto b = hmi::edit::rotatedBounds(*it);
            if (x >= b.x && x <= b.right() && y >= b.y && y <= b.bottom()) return &*it;
        }
        return nullptr;
    }
    [[nodiscard]] std::string objectTip(const hmi::Object& o) const {
        const Preview& pv = d_.preview_;
        const auto in = [&](const std::vector<hmi::Id>& ids) { return std::find(ids.begin(), ids.end(), o.id) != ids.end(); };
        const bool made = in(pv.touched.created), changed = in(pv.touched.modified), gone = in(pv.touched.removed);
        std::string tip = (o.name.empty() ? std::string("(sans nom)") : o.name) + " \xE2\x80\x94 " + std::string(hmi::kindLabel(o.kind));
        tip += made ? "\nNOUVEAU (cr\xC3\xA9\xC3\xA9 par la proposition)" : changed ? "\nMODIFI\xC3\x89 par la proposition"
             : gone ? "\nSUPPRIM\xC3\x89 par la proposition" : "\ninchang\xC3\xA9";
        for (const auto& it : d_.check_.items) {
            if (it.name != o.name) continue;
            std::size_t shown = 0;
            for (const auto& ch : it.changes) {
                if (made && (ch.key == "x" || ch.key == "y" || ch.key == "w" || ch.key == "h")) continue;
                if (shown++ >= 8) {
                    tip += "\n\xE2\x80\xA6";
                    break;
                }
                tip += "\n" + (made ? ch.key + " = " + ch.after
                                     : ch.key + " : " + (ch.before.empty() ? std::string("(vide)") : ch.before) + " \xE2\x86\x92 " + ch.after);
            }
            break;
        }
        const auto b = hmi::edit::rotatedBounds(o);
        tip += "\nx " + std::to_string(static_cast<long long>(std::lround(b.x))) + ", y " + std::to_string(static_cast<long long>(std::lround(b.y))) + " \xC2\xB7 "
             + std::to_string(static_cast<long long>(std::lround(b.w))) + " \xC3\x97 " + std::to_string(static_cast<long long>(std::lround(b.h)));
        return tip;
    }
    [[nodiscard]] gfx::Rect previewArea() const noexcept { return area_; }
    [[nodiscard]] gfx::Rect toggleRect() const noexcept { return toggle_; }
    [[nodiscard]] gfx::Rect zoomInRect() const noexcept { return zoomIn_; }
    [[nodiscard]] gfx::Rect listRect() const noexcept { return list_; }
    [[nodiscard]] int headingAt(gfx::Point at) const {   // la ligne de titre sous ce point (-1 : aucune)
        for (const auto& [r, i] : hits_)
            if (r.contains(at) && i < rows.size() && rows[i].kind == Row::Kind::Heading) return static_cast<int>(i);
        return -1;
    }
    [[nodiscard]] gfx::Rect headingRect(const std::string& key) const {
        for (const auto& [r, i] : hits_)
            if (i < rows.size() && rows[i].kind == Row::Kind::Heading && groupKey(rows[i].text) == key) return r;
        return {};
    }

    bool hasTooltip() const override { return true; }
    std::string liveTooltip(gfx::Point mouse) const override {
        if (zoomOut_.contains(mouse)) return "Zoom arri\xC3\xA8re (Ctrl+moins, ou la molette sur l'aper\xC3\xA7u)";
        if (zoomFit_.contains(mouse)) return "Ajuster : la vue enti\xC3\xA8re (Ctrl+0, ou un double-clic sur l'aper\xC3\xA7u)";
        if (zoomIn_.contains(mouse)) return "Zoom avant (Ctrl+plus, ou la molette sur l'aper\xC3\xA7u)";
        if (toggle_.contains(mouse))
            return listHidden ? "Montrer la liste (ce que fait la proposition)" : "Masquer la liste : l'aper\xC3\xA7u prend toute la largeur";
        if (list_.contains(mouse))
            for (const auto& [r, i] : hits_) {
                if (!r.contains(mouse) || i >= rows.size()) continue;
                const auto& row = rows[i];
                if (row.kind == Row::Kind::Heading)
                    return folded.count(groupKey(row.text)) ? "Clic : d\xC3\xA9plier ce groupe" : "Clic : replier ce groupe";
                return row.text + (row.right.empty() ? std::string{} : " \xE2\x80\x94 " + row.right);
            }
        if (area_.contains(mouse)) {
            if (const auto* o = objectAt(mouse)) return objectTip(*o);
            return "Molette : zoomer \xC2\xB7 glisser : d\xC3\xA9placer (zoom\xC3\xA9) \xC2\xB7 double-clic : ajuster";
        }
        return {};
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(kMaxW, std::max(560.f, r.w - 40.f));
        const float h = std::min(kMaxH, std::max(420.f, r.h - 40.f));
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        const float x = panel_.x + 16.f, right = panel_.right() - 16.f;
        close_ = {panel_.right() - 38.f, panel_.y + 6.f, 28.f, 24.f};
        counts_ = {x, panel_.y + 58.f, right - x, 30.f};
        const float top = counts_.bottom() + 12.f;
        const float bottom = panel_.bottom() - 56.f;
        const float leftW = listHidden ? 0.f : std::clamp((right - x) * 0.42f, 300.f, 520.f);
        float listTop = top, previewTop = top;
        const bool choice = d_.variable_ && d_.variable_->visibility() == ui::Visibility::Visible;
        if (choice) {
            // 1.16.1 : la liste masquee - le choix de la variable passe au-dessus de l'apercu.
            const float cw = listHidden ? std::min(520.f, right - x - 20.f) : leftW;
            const float cx = listHidden ? x + 20.f : x;
            varLabel_ = {cx, top, cw, 20.f};
            d_.variable_->setBounds({cx, top + 22.f, cw, 28.f});
            listTop = top + 60.f;
            if (listHidden) previewTop = top + 60.f;
        }
        list_ = {x, listTop, leftW, std::max(60.f, bottom - listTop)};
        if (listHidden) list_.w = 0.f;
        // 1.16.1 : le bouton qui masque / montre la liste (entre elle et l'apercu).
        toggle_ = listHidden ? gfx::Rect{x, previewTop, 14.f, 48.f} : gfx::Rect{x + leftW + 1.f, previewTop, 14.f, 48.f};
        const float px = listHidden ? x + 20.f : x + leftW + 16.f;
        preview_ = {px, previewTop, right - px, std::max(80.f, bottom - previewTop - 24.f)};
        legend_ = {preview_.x, preview_.bottom() + 6.f, preview_.w, 18.f};
        area_ = {preview_.x + 8.f, preview_.y + 8.f, preview_.w - 16.f, preview_.h - 16.f};
        zoomIn_ = {preview_.right() - 34.f, preview_.y + 6.f, 28.f, 24.f};
        zoomFit_ = {zoomIn_.x - 66.f, zoomIn_.y, 64.f, 24.f};
        zoomOut_ = {zoomFit_.x - 30.f, zoomIn_.y, 28.f, 24.f};
        clampPan();
        float bx = right;
        for (ui::Button* b : {d_.apply_, d_.cancel_}) {
            if (!b) continue;
            const float bw = std::max(110.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 34.f);
            bx -= bw;
            b->setBounds({bx, panel_.bottom() - 44.f, bw, 30.f});
            bx -= 10.f;
        }
        if (d_.edit_) d_.edit_->setBounds({x, panel_.bottom() - 44.f, ui::measureWidth(d_.edit_->text(), gfx::FontId{16}) + 34.f, 30.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRoundedRect(panel_, c.panelBg, 8.f);
        ctx.r.strokeRect(panel_, c.border, 1.f);
        const float x = panel_.x + 16.f;
        const auto& a = d_.panel_.assistant();
        const Proposal* p = a.proposal(d_.message_);
        // Le titre, la croix.
        ui::drawIcon(ctx.r, ui::Icon::Assistant, {x, panel_.y + 12.f, 20.f, 20.f}, c.accent);
        ctx.r.drawText({x + 28.f, panel_.y + 10.f}, "Pr\xC3\xA9visualisation des modifications", kTitleFont, c.text);
        ui::drawIcon(ctx.r, ui::Icon::Close, {close_.x + 6.f, close_.y + 4.f, 16.f, 16.f}, c.textMuted);
        if (!p) return;
        // 1.16.0 : une vue neuve, ou le projet seul (des variables, des types).
        const std::string where = createsView(*p) ? "Vue neuve \xC2\xAB " + newViewName(*p) + " \xC2\xBB"
                                : usesOpenView(*p) ? "Vue \xC2\xAB " + p->view + " \xC2\xBB"
                                                   : std::string("Le projet (variables, types IHM)");
        const std::string sub = where + " \xC2\xB7 " + (p->summary.empty() ? std::string("proposition") : p->summary)
                                + " \xC2\xB7 rien ne change avant Appliquer";
        ctx.r.drawText({x, panel_.y + 38.f}, elide(ctx.r, sub, kSmall, panel_.w - 32.f), kSmall, c.textMuted);
        // Les comptes : trois tuiles. Perimee (la vue a change, ou n'existe plus) : ceux de la
        // proposition (son texte les annonce), pas ceux d'une validation qui n'a rien pu lire.
        const Check& ck = d_.check_;
        const bool asProposed = ck.stale || ck.viewMissing;
        const struct { const char* label; int n; gfx::Color col; } tiles[] = {
            {"\xC3\x80 cr\xC3\xA9" "er", asProposed ? p->create : ck.create, c.ok},
            {"\xC3\x80 modifier", asProposed ? p->modify : ck.modify, c.info},
            {"\xC3\x80 supprimer", asProposed ? p->remove : ck.remove, c.error}};
        float tx = counts_.x;
        for (const auto& t : tiles) {
            const std::string n = std::to_string(t.n);
            const float tw = ctx.r.measure(n, kTitleFont).width + ctx.r.measure(t.label, kSmall).width + 30.f;
            const gfx::Rect tile{tx, counts_.y, tw, counts_.h};
            ctx.r.fillRoundedRect(tile, alpha(t.n > 0 ? t.col : c.textDisabled, 30), 5.f);
            ctx.r.fillRect({tile.x, tile.y + 4.f, 3.f, tile.h - 8.f}, t.n > 0 ? t.col : c.textDisabled);
            ctx.r.drawText({tile.x + 12.f, tile.y + (tile.h - ctx.r.lineHeight(kTitleFont)) / 2.f}, n, kTitleFont, c.text);
            ctx.r.drawText({tile.x + 18.f + ctx.r.measure(n, kTitleFont).width, tile.y + (tile.h - ctx.r.lineHeight(kSmall)) / 2.f}, t.label, kSmall,
                           c.textMuted);
            tx += tw + 10.f;
        }
        if (const std::string made = creationSummary(*p); !made.empty() && !(ck.stale || ck.viewMissing))
            ctx.r.drawText({tx + 8.f, counts_.y + (counts_.h - ctx.r.lineHeight(kSmall)) / 2.f},
                           elide(ctx.r, "+ " + made, kSmall, counts_.right() - tx - 8.f), kSmall, c.ok);
        if (ck.stale || ck.viewMissing) {
            ctx.r.drawText({tx + 8.f, counts_.y + (counts_.h - ctx.r.lineHeight(kSmall)) / 2.f},
                           elide(ctx.r, "P\xC3\xA9rim\xC3\xA9" "e : redemande-la (Modifier la demande).", kSmall, counts_.right() - tx - 8.f), kSmall,
                           c.warning);
        }
        if (d_.variable_ && d_.variable_->visibility() == ui::Visibility::Visible) {
            // Le nom introuvable reste dit apres le choix (la validation ne le cite plus : il est remplace).
            const std::string name = "\xC2\xAB " + d_.missingName_ + " \xC2\xBB n'existe pas";
            const bool chosen = !p->variable.use.empty() || p->variable.create;
            const std::string label = !p->variable.use.empty() ? name + " : remplac\xC3\xA9" "e par " + p->variable.use + "."
                                    : p->variable.create    ? name + " : elle sera cr\xC3\xA9\xC3\xA9" "e (" + p->variable.type + ")."
                                                            : name + " : quelle variable utiliser ?";
            ctx.r.drawText({varLabel_.x, varLabel_.y + 2.f}, elide(ctx.r, label, kSmall, varLabel_.w), kSmall, chosen ? c.textMuted : c.error);
        }
        paintList(ctx);
        paintPreview(ctx);
    }

    void paintList(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(list_, c.inputBg);
        ctx.r.strokeRect(list_, c.border, 1.f);
        const float lh = ctx.r.lineHeight(kText) + 6.f;
        const float lhSmall = ctx.r.lineHeight(kSmall) + 3.f;
        // Les hauteurs d'abord (un avertissement, une erreur : sur plusieurs lignes).
        const float w0 = list_.w - 24.f;
        std::vector<std::vector<std::string>> wrapped(rows.size());
        float content = 8.f;
        hits_.clear();
        if (list_.w <= 1.f) return;   // 1.16.1 : la liste masquee
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (hidden(i)) continue;   // 1.16.1 : un groupe replie
            const bool multi = rows[i].kind == Row::Kind::Warning || rows[i].kind == Row::Kind::Error;
            wrapped[i] = multi ? wrap(rows[i].text, kSmall, w0 - 28.f) : std::vector<std::string>{rows[i].text};
            content += multi ? std::max(lh, static_cast<float>(wrapped[i].size()) * lhSmall + 6.f) : lh;
        }
        content_ = content;
        scroll = std::clamp(scroll, 0.f, std::max(0.f, content - list_.h));
        const float space = bar_.space(list_, content, list_.h);
        ctx.r.pushClip(list_);
        float y = list_.y + 4.f - scroll;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (hidden(i)) continue;
            const auto& row = rows[i];
            const bool multi = row.kind == Row::Kind::Warning || row.kind == Row::Kind::Error;
            const float rh = multi ? std::max(lh, static_cast<float>(wrapped[i].size()) * lhSmall + 6.f) : lh;
            const gfx::Rect rowRect{list_.x, y, list_.w - space, rh};
            if (rowRect.bottom() > list_.y && rowRect.y < list_.bottom()) hits_.emplace_back(rowRect, i);
            if (y + rh >= list_.y && y <= list_.bottom()) {
                const float ty = y + 3.f;
                const float w = list_.w - space - 12.f;
                switch (row.kind) {
                    case Row::Kind::Heading: {
                        // 1.16.1 : un groupe se replie (sa fleche ; le clic sur son titre).
                        const bool shut = folded.count(groupKey(row.text)) > 0;
                        ctx.r.fillRect({list_.x + 1.f, y, list_.w - 2.f, lh}, c.rowAltBg);
                        chevron(ctx.r, {list_.x + 5.f, y + (lh - 14.f) / 2.f, 14.f, 14.f}, shut ? 0 : 1, c.textMuted);
                        ctx.r.drawText({list_.x + 22.f, ty}, elide(ctx.r, row.text, kSmall, w - 14.f), kSmall, c.textMuted);
                        break;
                    }
                    case Row::Kind::Item: {
                        const gfx::Color col = row.what == 0 ? c.ok : row.what == 1 ? c.info : c.error;
                        const char* tag = row.what == 0 ? "+" : row.what == 1 ? "~" : "\xE2\x88\x92";
                        ctx.r.fillRoundedRect({list_.x + 8.f, y + 4.f, 16.f, lh - 8.f}, alpha(col, 50), 3.f);
                        ctx.r.drawText({list_.x + 12.f, ty}, tag, kText, col);
                        const float rw = ctx.r.measure(row.right, kSmall).width;
                        ctx.r.drawText({list_.x + w - rw, ty + 1.f}, row.right, kSmall, c.textMuted);
                        ctx.r.drawText({list_.x + 30.f, ty}, elide(ctx.r, row.text, kText, w - rw - 34.f), kText, c.text);
                        break;
                    }
                    case Row::Kind::Change:
                        ctx.r.drawText({list_.x + 30.f, ty}, elide(ctx.r, row.text, kSmall, w - 24.f), kSmall, c.textMuted);
                        break;
                    case Row::Kind::Note:
                        ctx.r.drawText({list_.x + 12.f, ty}, elide(ctx.r, row.text, kSmall, w), kSmall, c.text);
                        break;
                    case Row::Kind::Warning:
                    case Row::Kind::Error: {
                        const bool err = row.kind == Row::Kind::Error;
                        ui::drawIcon(ctx.r, err ? ui::Icon::Error : ui::Icon::Warning, {list_.x + 8.f, y + 4.f, 14.f, 14.f}, err ? c.error : c.warning);
                        float ly = ty;
                        for (const auto& l : wrapped[i]) {
                            ctx.r.drawText({list_.x + 28.f, ly}, l, kSmall, c.text);
                            ly += lhSmall;
                        }
                        break;
                    }
                }
            }
            y += rh;
        }
        ctx.r.popClip();
        bar_.paint(ctx, list_, content, list_.h, scroll);
    }

    void paintPreview(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(preview_, c.windowBg);
        ctx.r.strokeRect(preview_, c.border, 1.f);
        const Preview& pv = d_.preview_;
        if (!pv.ok || pv.view.width <= 0 || pv.view.height <= 0) {
            ctx.r.drawText({preview_.x + 12.f, preview_.y + 12.f},
                           elide(ctx.r, pv.why.empty() ? std::string("Pas d'aper\xC3\xA7u.") : "Pas d'aper\xC3\xA7u : " + pv.why, kText, preview_.w - 24.f),
                           kText, c.textMuted);
        } else {
            const auto& v = pv.view;
            // 1.16.1 : le zoom et le decalage de l'apercu (pageGeometry) ; tout se dessine dans sa zone.
            HmiViewport vp;
            gfx::Rect page;
            (void)pageGeometry(vp, page);
            const float z = vp.zoom;
            ctx.r.pushClip(area_);
            ctx.r.fillRect(page, parseColor(v.background, gfx::Color::rgb(0x20242B)));
            HmiPaintOptions opt;
            const auto doc = d_.panel_.assistant().host().document ? d_.panel_.assistant().host().document() : nullptr;
            if (doc) {
                opt.project = &doc->project;
                opt.assets = &doc->project.assets;
            }
            ctx.r.pushClip(page);
            paintHmiView(ctx.r, v, vp, HmiPropertySource{}, ctx.theme, opt);
            const auto box = [&](hmi::Id id, gfx::Rect& out) {
                for (const auto& o : v.objects)
                    if (o.id == id) {
                        const auto bx = hmi::edit::rotatedBounds(o);
                        const auto p0 = vp.toScreen(bx.x, bx.y);
                        out = {p0.x - 2.f, p0.y - 2.f, static_cast<float>(bx.w) * z + 4.f, static_cast<float>(bx.h) * z + 4.f};
                        return true;
                    }
                return false;
            };
            gfx::Rect r{};
            for (const auto id : pv.touched.created)
                if (box(id, r)) ctx.r.strokeRect(r, c.ok, 2.f);
            for (const auto id : pv.touched.modified)
                if (box(id, r)) ctx.r.strokeRect(r, c.info, 2.f);
            for (const auto id : pv.touched.removed)
                if (box(id, r)) {
                    ctx.r.fillRect(r, alpha(c.error, 40));
                    ctx.r.strokeRect(r, c.error, 2.f);
                    ctx.r.line({r.x, r.y}, {r.right(), r.bottom()}, c.error, 2.f);
                    ctx.r.line({r.right(), r.y}, {r.x, r.bottom()}, c.error, 2.f);
                }
            ctx.r.popClip();
            ctx.r.strokeRect(page, c.border, 1.f);
            ctx.r.popClip();
            // 1.16.1 : les boutons du zoom (en haut a droite de l'apercu) et le zoom en %.
            const auto button = [&](const gfx::Rect& r, const std::string& text) {
                ctx.r.fillRoundedRect(r, alpha(c.panelBg, 230), 4.f);
                ctx.r.strokeRect(r, c.border, 1.f);
                const float tw = ctx.r.measure(text, kSmall).width;
                ctx.r.drawText({r.x + (r.w - tw) / 2.f, r.y + (r.h - ctx.r.lineHeight(kSmall)) / 2.f}, text, kSmall, c.text);
            };
            button(zoomOut_, "\xE2\x88\x92");
            button(zoomFit_, "Ajuster");
            button(zoomIn_, "+");
            const std::string pct = std::to_string(static_cast<int>(std::lround(z * 100.f))) + " %";
            const float pw = ctx.r.measure(pct, kSmall).width + 12.f;
            const gfx::Rect pr{zoomOut_.x - pw - 4.f, zoomOut_.y, pw, zoomOut_.h};
            ctx.r.fillRoundedRect(pr, alpha(c.panelBg, 230), 4.f);
            ctx.r.drawText({pr.x + 6.f, pr.y + (pr.h - ctx.r.lineHeight(kSmall)) / 2.f}, pct, kSmall, c.textMuted);
        }
        // 1.16.1 : le bouton de la liste (la masquer, la montrer).
        ctx.r.fillRoundedRect(toggle_, c.rowAltBg, 3.f);
        ctx.r.strokeRect(toggle_, c.border, 1.f);
        chevron(ctx.r, {toggle_.x, toggle_.y + (toggle_.h - 14.f) / 2.f, 14.f, 14.f}, listHidden ? 0 : 2, c.textMuted);
        // La legende.
        float lx = legend_.x;
        const auto mark = [&](gfx::Color col, const char* text, bool cross) {
            ctx.r.strokeRect({lx, legend_.y + 3.f, 14.f, 11.f}, col, 2.f);
            if (cross) ctx.r.line({lx, legend_.y + 3.f}, {lx + 14.f, legend_.y + 14.f}, col, 1.5f);
            ctx.r.drawText({lx + 20.f, legend_.y}, text, kSmall, c.textMuted);
            lx += 20.f + ctx.r.measure(text, kSmall).width + 18.f;
        };
        mark(c.ok, "nouveau", false);
        mark(c.info, "modifi\xC3\xA9", false);
        mark(c.error, "supprim\xC3\xA9", true);
        ctx.r.drawText({lx, legend_.y}, elide(ctx.r, "Aper\xC3\xA7u : une copie de la vue \xC2\xB7 molette : zoom \xC2\xB7 survol : l'objet", kSmall,
                                              legend_.right() - lx), kSmall, c.textMuted);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        const float lh = ui::lineHeight(kText) + 6.f;
        const float content = content_;
        if (list_.w > 1.f && bar_.handle(*this, ev, list_, content, list_.h, scroll)) return ui::EventResult::Consumed;
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && list_.contains(w->pos)) {
            scroll = std::clamp(scroll - w->dy * 3.f * lh, 0.f, std::max(0.f, content - list_.h));
            invalidate();
            return ui::EventResult::Consumed;
        }
        // 1.16.1 : la molette sur l'apercu zoome (autour de la souris).
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && area_.contains(w->pos) && w->dy != 0.f) {
            setZoom(zoom * (w->dy > 0.f ? 1.25f : 0.8f), w->pos);
            return ui::EventResult::Consumed;
        }
        // Le glisser de l'apercu zoome.
        if (const auto* m = std::get_if<ui::MouseMove>(&ev); m && dragging_) {
            pan = {pan0_.x + m->pos.x - dragFrom_.x, pan0_.y + m->pos.y - dragFrom_.y};
            clampPan();
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* u = std::get_if<ui::MouseUp>(&ev); u && dragging_) {
            dragging_ = false;
            releaseMouse();
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
            if (close_.contains(d->pos)) {
                d_.close();
                return ui::EventResult::Consumed;
            }
            if (zoomOut_.contains(d->pos) || zoomIn_.contains(d->pos) || zoomFit_.contains(d->pos)) {
                if (zoomFit_.contains(d->pos)) setZoom(1.f);
                else setZoom(zoom * (zoomIn_.contains(d->pos) ? 1.25f : 0.8f));
                return ui::EventResult::Consumed;
            }
            if (toggle_.contains(d->pos)) {
                listHidden = !listHidden;
                invalidateLayout();
                invalidate();
                return ui::EventResult::Consumed;
            }
            if (const int h = headingAt(d->pos); h >= 0) {
                const std::string key = groupKey(rows[static_cast<std::size_t>(h)].text);
                if (!folded.erase(key)) folded.insert(key);
                invalidate();
                return ui::EventResult::Consumed;
            }
            if (area_.contains(d->pos)) {
                if (d->clickCount >= 2) {
                    setZoom(1.f);
                } else if (zoom > 1.f) {
                    dragging_ = true;
                    dragFrom_ = d->pos;
                    pan0_ = pan;
                    captureMouse();
                }
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    AiPreviewDialog&  d_;
    float             content_{0.f};
    gfx::Rect         panel_{}, close_{}, counts_{}, list_{}, preview_{}, legend_{}, varLabel_{};
    gfx::Rect         area_{}, toggle_{}, zoomIn_{}, zoomOut_{}, zoomFit_{};   // 1.16.1
    std::vector<std::pair<gfx::Rect, std::size_t>> hits_;                    // 1.16.1 : les lignes montrees de la liste
    bool              dragging_{false};
    gfx::Point        dragFrom_{}, pan0_{};
    ui::EdgeScrollBar bar_;
};

// ---- 1.16.1 : le zoom, la liste repliable, les infobulles (les gestes, pour les essais et les scripts) ----
void AiPreviewDialog::setPreviewZoom(float z) {
    if (auto* b = dynamic_cast<AiPreviewBody*>(widgetRoot())) b->setZoom(z);
}
float AiPreviewDialog::previewZoom() const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b ? b->zoom : 1.f;
}
bool AiPreviewDialog::foldGroup(const std::string& heading, bool folded) {
    auto* b = dynamic_cast<AiPreviewBody*>(widgetRoot());
    if (!b) return false;
    const bool known = std::any_of(b->rows.begin(), b->rows.end(),
                                   [&](const Row& r) { return r.kind == Row::Kind::Heading && groupKey(r.text) == heading; });
    if (!known) return false;
    if (folded) b->folded.insert(heading);
    else b->folded.erase(heading);
    b->invalidate();
    return true;
}
bool AiPreviewDialog::groupFolded(const std::string& heading) const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b && b->folded.count(heading) > 0;
}
std::size_t AiPreviewDialog::shownRows() const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    if (!b) return 0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < b->rows.size(); ++i)
        if (!b->hidden(i)) ++n;
    return n;
}
void AiPreviewDialog::setListHidden(bool hidden) {
    auto* b = dynamic_cast<AiPreviewBody*>(widgetRoot());
    if (!b || b->listHidden == hidden) return;
    b->listHidden = hidden;
    b->invalidateLayout();
    b->invalidate();
}
bool AiPreviewDialog::listHidden() const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b && b->listHidden;
}
std::string AiPreviewDialog::previewTooltip(gfx::Point at) const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b ? b->liveTooltip(at) : std::string{};
}
gfx::Rect AiPreviewDialog::previewArea() const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b ? b->previewArea() : gfx::Rect{};
}
gfx::Rect AiPreviewDialog::listArea() const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b ? b->listRect() : gfx::Rect{};
}
gfx::Rect AiPreviewDialog::groupRect(const std::string& heading) const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    return b ? b->headingRect(heading) : gfx::Rect{};
}
gfx::Point AiPreviewDialog::objectPoint(const std::string& name) const {
    const auto* b = dynamic_cast<const AiPreviewBody*>(const_cast<AiPreviewDialog*>(this)->widgetRoot());
    if (!b) return {-1.f, -1.f};
    HmiViewport vp;
    gfx::Rect page;
    if (!b->pageGeometry(vp, page)) return {-1.f, -1.f};
    for (const auto& o : preview_.view.objects)
        if (o.name == name) {
            const auto bx = hmi::edit::rotatedBounds(o);
            return vp.toScreen(bx.x + bx.w / 2.0, bx.y + bx.h / 2.0);
        }
    return {-1.f, -1.f};
}

AiPreviewDialog::AiPreviewDialog(AiPanel& panel, int message)
    : menu::WidgetMenu("dialog.ai.apercu"), panel_(panel), message_(message) {}

menu::MenuTraits AiPreviewDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status AiPreviewDialog::buildUi() {
    const std::string base = id();
    auto body = std::make_unique<AiPreviewBody>(*this);
    auto* b = body.get();
    variable_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".variable")));
    variable_->setVisibility(ui::Visibility::Collapsed);
    links_ += variable_->selectionChanged->connect([this](int i) { if (!syncing_) (void)chooseVariable(i); });
    edit_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Modifier la demande", base + ".modifier")));
    edit_->setTooltip("Remettre la demande dans la saisie pour la reformuler (cette proposition reste \xC3\xA0 d\xC3\xA9" "cider, sur sa carte).");
    links_ += edit_->clicked->connect([this] { editRequest(); });
    // 1.16.1 : << Refuser... >> (une question d'abord) - plus << Annuler >>, qu'on prenait pour << fermer >>.
    cancel_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Refuser\xE2\x80\xA6", base + ".refuser")));
    cancel_->setStyle(ui::Button::Style::Flat);
    cancel_->setTooltip("Refuser la proposition (une question d'abord) : le projet ne change pas ; sa carte pourra revenir sur ce refus. "
                        "\xC3\x89" "chap ferme cette fen\xC3\xAAtre SANS d\xC3\xA9" "cider.");
    links_ += cancel_->clicked->connect([this] { cancel(); });
    apply_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Appliquer", base + ".appliquer")));
    apply_->setStyle(ui::Button::Style::Primary);
    apply_->setTooltip("Modifier le projet, en une seule \xC3\xA9tape d'annulation (Ctrl+Z).");
    links_ += apply_->clicked->connect([this] { (void)apply(); });
    setRoot(std::move(body));
    refresh();
    return core::ok();
}

void AiPreviewDialog::refresh() {
    auto& a = panel_.assistant();
    const Proposal* p = a.proposal(message_);
    if (!p) return;
    check_ = a.check(message_);
    preview_ = a.previewOf(message_);
    auto* body = dynamic_cast<AiPreviewBody*>(widgetRoot());
    // La liste de gauche.
    std::vector<Row> rows;
    const auto heading = [&](std::string t) { rows.push_back({Row::Kind::Heading, std::move(t), {}, 0}); };
    if (!check_.errors.empty()) {
        heading("ERREURS (" + std::to_string(check_.errors.size()) + ") \xE2\x80\x94 rien ne s'applique tant qu'il y en a");
        for (const auto& e : check_.errors) rows.push_back({Row::Kind::Error, e, {}, 0});
    }
    if (!check_.warnings.empty()) {
        heading("AVERTISSEMENTS (" + std::to_string(check_.warnings.size()) + ")");
        for (const auto& w : check_.warnings) rows.push_back({Row::Kind::Warning, w, {}, 0});
    }
    // 1.16.0 : CE QUI SERA CREE en plus des objets - la vue neuve, les variables, les types IHM (jamais en silence).
    if (!check_.newView.empty()) {
        heading("VUE \xC3\x80 CR\xC3\x89" "ER (elle s'ouvre apr\xC3\xA8s Appliquer)");
        std::string size;
        if (preview_.view.width > 0) size = std::to_string(preview_.view.width) + " \xC3\x97 " + std::to_string(preview_.view.height);
        rows.push_back({Row::Kind::Item, check_.newView, size, 0});
        if (!preview_.view.description.empty()) rows.push_back({Row::Kind::Change, preview_.view.description, {}, 0});
    }
    if (!check_.newTypes.empty()) {
        heading("TYPES IHM \xC3\x80 CR\xC3\x89" "ER (" + std::to_string(check_.newTypes.size()) + ")");
        for (const auto& [name, members] : check_.newTypes) {
            rows.push_back({Row::Kind::Item, name, std::to_string(members.size()) + (members.size() > 1 ? " membres" : " membre"), 0});
            std::size_t shown = 0;
            for (const auto& m : members) {
                if (shown++ >= 8) {
                    rows.push_back({Row::Kind::Change, "\xE2\x80\xA6 et " + std::to_string(members.size() - 8) + " autres", {}, 0});
                    break;
                }
                rows.push_back({Row::Kind::Change, m.name + " : " + m.type + (m.description.empty() ? std::string{} : " \xE2\x80\x94 " + m.description), {}, 0});
            }
        }
    }
    if (!check_.newVariables.empty()) {
        heading("VARIABLES IHM \xC3\x80 CR\xC3\x89" "ER (" + std::to_string(check_.newVariables.size()) + ")");
        for (const auto& v : check_.newVariables) {
            rows.push_back({Row::Kind::Item, v.name, v.type, 0});
            std::string detail = v.initial.empty() ? std::string{} : "valeur initiale " + v.initial;
            if (!v.description.empty()) detail += (detail.empty() ? "" : " \xE2\x80\x94 ") + v.description;
            if (!detail.empty()) rows.push_back({Row::Kind::Change, detail, {}, 0});
        }
    }
    heading("VARIABLES ASSOCI\xC3\x89" "ES");
    if (check_.bindings.empty()) rows.push_back({Row::Kind::Note, "Aucune (l'Assistant n'invente jamais une variable).", {}, 0});
    for (const auto& bnd : check_.bindings) rows.push_back({Row::Kind::Note, bnd, {}, 0});
    if (!check_.missingVariable.empty())
        rows.push_back({Row::Kind::Note, "Introuvable : " + check_.missingVariable + " \xE2\x80\x94 choisis au-dessus.", {}, 0});
    heading("OBJETS CONCERN\xC3\x89S (" + std::to_string(check_.items.size()) + ")");
    for (const auto& it : check_.items) {
        const int what = it.what == Item::What::Create ? 0 : it.what == Item::What::Modify ? 1 : 2;
        rows.push_back({Row::Kind::Item, it.name, std::string(hmi::kindLabel(it.kind)), what});
        std::size_t shown = 0;
        for (const auto& ch : it.changes) {
            if (it.what == Item::What::Create && (ch.key == "x" || ch.key == "y" || ch.key == "w" || ch.key == "h")) continue;
            if (shown++ >= 6) {
                rows.push_back({Row::Kind::Change, "\xE2\x80\xA6", {}, 0});
                break;
            }
            const std::string text = it.what == Item::What::Create ? ch.key + " = " + ch.after
                                                                   : ch.key + " : " + (ch.before.empty() ? std::string("(vide)") : ch.before) + " \xE2\x86\x92 " + ch.after;
            rows.push_back({Row::Kind::Change, text, {}, 0});
        }
    }
    if (body) body->rows = std::move(rows);
    // Le choix de la variable (introuvable) : les plus proches d'abord, puis les autres, puis Creer.
    const bool needChoice = !check_.missingVariable.empty() || !p->variable.use.empty() || p->variable.create;
    if (variable_) {
        syncing_ = true;
        if (needChoice) {
            std::string missing = check_.missingVariable;
            for (const auto& op : p->ops)
                if (op.action == Operation::Action::Bind) missing = op.variable;
            missingName_ = missing;
            variableNames_.clear();
            std::vector<ui::DropDown::Item> items;
            std::set<std::string> seen;
            items.push_back({"Choisir une variable\xE2\x80\xA6", "?"});   // l'invite : rien n'est choisi a la place de l'utilisateur
            variableNames_.push_back("?");
            const auto doc = a.host().document ? a.host().document() : nullptr;
            if (doc) {
                for (const auto& n : nearVariables(doc->project, missing)) {
                    items.push_back({n + "  (proche)", n});
                    variableNames_.push_back(n);
                    seen.insert(n);
                }
                std::vector<std::string> all;
                for (const auto& v : doc->project.programs.variables)
                    if (!seen.count(v.name)) all.push_back(v.name);
                std::sort(all.begin(), all.end());
                for (const auto& n : all) {
                    items.push_back({n, n});
                    variableNames_.push_back(n);
                }
            }
            items.push_back({"Cr\xC3\xA9" "er \xC2\xAB " + missing + " \xC2\xBB (BOOL) dans les variables de l'IHM", "+"});
            variableNames_.push_back({});
            variable_->setItems(std::move(items));
            int sel = 0;
            if (p->variable.create) sel = static_cast<int>(variableNames_.size()) - 1;
            for (std::size_t i = 1; i + 1 < variableNames_.size(); ++i)
                if (!p->variable.use.empty() && variableNames_[i] == p->variable.use) sel = static_cast<int>(i);
            variable_->setSelectedIndex(sel);
            variable_->setVisibility(ui::Visibility::Visible);
        } else {
            variable_->setVisibility(ui::Visibility::Collapsed);
        }
        syncing_ = false;
    }
    // 1.16.1 : << Appliquer malgre les erreurs >> (l'option des parametres) - le bouton le dit, la confirmation les liste.
    const bool anyway = !check_.ok() && !check_.stale && !check_.viewMissing && a.settings().applyWithErrors;
    if (apply_) {
        const std::size_t n = check_.errors.size();
        apply_->setText(anyway ? "Appliquer malgr\xC3\xA9 " + std::to_string(n) + (n > 1 ? " erreurs\xE2\x80\xA6" : " erreur\xE2\x80\xA6")
                               : std::string("Appliquer"));
        apply_->setTooltip(anyway ? "Ce qui est valide s'applique ; ce qui est en erreur est laiss\xC3\xA9 de c\xC3\xB4t\xC3\xA9 et not\xC3\xA9 dans le journal "
                                    "(une confirmation les liste d'abord). Un seul Ctrl+Z reprend tout."
                                  : "Modifier le projet, en une seule \xC3\xA9tape d'annulation (Ctrl+Z).");
        apply_->setEnabled(p->state == "proposee" && (check_.ok() || anyway));
    }
    if (cancel_) cancel_->setEnabled(p->state == "proposee");
    key_ = p->id + "|" + p->state + "|" + p->variable.use + (p->variable.create ? "+" : "-");
    if (widgetRoot()) widgetRoot()->invalidateLayout();
}

std::vector<std::string> AiPreviewDialog::variableItems() const {
    std::vector<std::string> out;
    if (variable_)
        for (const auto& it : variable_->items()) out.push_back(it.label);
    return out;
}

bool AiPreviewDialog::chooseVariable(int index) {
    if (index <= 0 || index >= static_cast<int>(variableNames_.size())) return false;   // 0 : l'invite
    VariableChoice choice;
    if (variableNames_[static_cast<std::size_t>(index)].empty()) choice.create = true;
    else choice.use = variableNames_[static_cast<std::size_t>(index)];
    const bool ok = panel_.assistant().chooseVariable(message_, choice);
    panel_.sync();
    refresh();
    return ok;
}

void AiPreviewDialog::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    const Proposal* p = panel_.assistant().proposal(message_);
    // Appliquee (apres la confirmation d'une suppression), refusee, ou partie : la fenetre se ferme.
    if (!p || p->state != "proposee") {
        finish(p && p->state == "appliquee");
        return;
    }
    const std::string key = p->id + "|" + p->state + "|" + p->variable.use + (p->variable.create ? "+" : "-");
    if (key != key_) refresh();
}

bool AiPreviewDialog::apply() {
    if (done_) return false;
    const bool ok = panel_.applyProposal(message_);   // une suppression multiple : la confirmation d'abord
    if (ok) finish(true);
    else refresh();
    return ok;
}

void AiPreviewDialog::cancel() {
    // 1.16.1 : la question d'abord ; refusee, la fenetre se ferme seule (Update voit l'etat changer).
    panel_.askRefuse(message_);
}

void AiPreviewDialog::editRequest() {
    panel_.editRequest(message_);
    finish(false);
}

void AiPreviewDialog::close() { finish(false); }

void AiPreviewDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, {}});
}

ui::EventResult AiPreviewDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape && !(variable_ && variable_->isOpen())) {
        close();
        return ui::EventResult::Consumed;
    }
    // 1.16.1 : Ctrl+plus, Ctrl+moins, Ctrl+0 zooment l'apercu (pas l'appli derriere).
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (const int step = ui::zoomKeyStep(*k); step != ui::kNotZoomKey) {
            setPreviewZoom(step == 0 ? 1.f : previewZoom() * (step > 0 ? 1.25f : 0.8f));
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app::ai
