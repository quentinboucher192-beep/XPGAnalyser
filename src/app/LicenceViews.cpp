// =============================================================================
//  app/LicenceViews.cpp - 1.45.0 : des vues de la fenetre Licence, a part
//  (LicenceViews.hpp) : la capture qu'on agrandit et ses bulles, la capture en
//  grand (ses fleches, ses legendes), les licences de ce PC.
// =============================================================================
#include "LicenceViews.hpp"

#include "LicenceService.hpp"
#include "hmi/HmiImages.hpp"

#include "../hmi/HmiMedia.hpp"
#include "../licensing/Licensing.hpp"
#include "../licensing/Time.hpp"
#include "../ui/DialogFrame.hpp"
#include "../ui/Shapes.hpp"
#include "../ui/Theme.hpp"
#include "../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace app::licence {

namespace {

constexpr gfx::FontId kTitle{17};
constexpr gfx::FontId kSmall{14};
constexpr gfx::FontId kValue{17};

std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();   // un caractere entier
        if (!t.empty()) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

// Des lignes coupees a la largeur (les mots entiers).
std::vector<std::string> wrapText(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && r.measure(tryLine, f).width > w) {
            out.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') flush();
        else if (c == '\n') {
            flush();
            out.push_back(line);
            line.clear();
        } else word += c;
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

const char* const kDot = " \xC2\xB7 ";

} // namespace

// ============================================================ les captures ==
hmi::Resource capturePng(const std::string& path, const std::string& name) {
    hmi::Resource res;
    if (path.empty()) return res;
    std::error_code ec;
    const auto p = std::filesystem::path(std::u8string(path.begin(), path.end()));
    const auto size = std::filesystem::file_size(p, ec);
    // 1.45.0 : 32 Mo (une capture 2x, 3200 x 2000, en couleurs vraies) ; avant : 8 Mo
    if (ec || size == 0 || size >= 32u * 1024u * 1024u) return res;
    std::ifstream in(p, std::ios::binary);
    hmi::Bytes bytes(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) return res;
    const auto info = hmi::inspectMedia(bytes, name);   // sa taille, sans la decoder (le repere des bulles)
    res.name = name;
    res.format = "PNG";
    res.width = info.width;
    res.height = info.height;
    res.data = std::make_shared<const hmi::Bytes>(std::move(bytes));
    return res;
}

std::string captureName(const std::string& feature, std::size_t n) {
    return feature + (n == 0 ? std::string(".png") : "-" + std::to_string(n + 1) + ".png");
}

CaptureView::CaptureView(std::string id) : ui::Widget(std::move(id)), empty_("Pas encore de capture pour cette fonctionnalit\xC3\xA9.") {}

void CaptureView::setImage(hmi::Resource res) {
    image_ = std::move(res);
    iw_ = ih_ = 0.f;
    if (image_.data) {
        int w = image_.width, h = image_.height;
        if (w <= 0 || h <= 0) {
            const auto info = hmi::inspectMedia(*image_.data, image_.name);
            w = info.width;
            h = info.height;
        }
        if (w > 0 && h > 0) {
            iw_ = static_cast<float>(w);
            ih_ = static_cast<float>(h);
        }
    }
    hoverSpot_ = -1;
    fitAgain();
}

void CaptureView::fitAgain() {
    zoom_ = 1.f;
    ox_ = oy_ = 0.f;
    invalidate();
}

void CaptureView::setSpots(std::vector<CaptureSpot> s, float refW, float refH) {
    spots_ = std::move(s);
    refW_ = refW > 0.f && refH > 0.f ? refW : 0.f;
    refH_ = refW > 0.f && refH > 0.f ? refH : 0.f;
    hoverSpot_ = -1;
    invalidate();
}

gfx::Rect CaptureView::imageRect() const noexcept {
    const auto box = bounds();
    if (iw_ <= 0.f || ih_ <= 0.f || box.w <= 0.f || box.h <= 0.f) return {};
    const float k = std::min(box.w / iw_, box.h / ih_) * zoom_;
    const float w = iw_ * k, h = ih_ * k;
    // 1.45.0 : le coin sur un pixel entier (une image a la taille de sa copie : nette, sans demi-pixel de flou)
    return {std::round(box.x + (box.w - w) * 0.5f + ox_), std::round(box.y + (box.h - h) * 0.5f + oy_), w, h};
}

std::optional<gfx::Rect> CaptureView::spotRect(std::size_t n) const {
    if (n >= spots_.size() || iw_ <= 0.f || ih_ <= 0.f) return std::nullopt;
    const gfx::Rect ir = imageRect();
    if (ir.w <= 0.f || ir.h <= 0.f) return std::nullopt;
    // Le repere des bulles : celui de descriptions.json (une capture refaite en 2x garde ses bulles), sinon l'image.
    const float kx = ir.w / (refW_ > 0.f ? refW_ : iw_), ky = ir.h / (refH_ > 0.f ? refH_ : ih_);
    const auto& s = spots_[n];
    return gfx::Rect{ir.x + s.x * kx, ir.y + s.y * ky, s.w * kx, s.h * ky};
}

std::optional<gfx::Point> CaptureView::spotCenter(std::size_t n) const {
    const auto r = spotRect(n);
    if (!r) return std::nullopt;
    return gfx::Point{r->x + r->w * 0.5f, r->y + r->h * 0.5f};
}

int CaptureView::spotAt(gfx::Point p) const {
    int at = -1;
    if (!bounds().contains(p)) return at;
    for (std::size_t i = 0; i < spots_.size(); ++i)
        if (const auto r = spotRect(i); r && r->contains(p)) at = static_cast<int>(i);   // la derniere (la plus haute) gagne
    return at;
}

void CaptureView::clampOffset() {
    const auto box = bounds();
    if (iw_ <= 0.f || ih_ <= 0.f) return;
    const float k = std::min(box.w / iw_, box.h / ih_) * zoom_;
    const float dw = iw_ * k, dh = ih_ * k;
    const float mx = std::max(0.f, (dw - box.w) * 0.5f), my = std::max(0.f, (dh - box.h) * 0.5f);
    ox_ = std::clamp(ox_, -mx, mx);
    oy_ = std::clamp(oy_, -my, my);
}

void CaptureView::wheel(gfx::Point at, float dy) {
    if (!hasImage() || dy == 0.f) return;
    const auto box = bounds();
    const float before = zoom_;
    zoom_ = std::clamp(zoom_ * (dy > 0.f ? 1.25f : 0.8f), 1.f, 4.f);
    if (zoom_ == before) return;
    // Le point sous la souris reste sous la souris.
    const float cx = box.x + box.w * 0.5f + ox_, cy = box.y + box.h * 0.5f + oy_;
    const float f = zoom_ / before;
    ox_ += (at.x - cx) * (1.f - f);
    oy_ += (at.y - cy) * (1.f - f);
    if (zoom_ <= 1.f) ox_ = oy_ = 0.f;
    clampOffset();
    hoverSpot_ = spotAt(at);   // la bulle sous la souris apres le zoom
    invalidate();
}

ui::EventResult CaptureView::onEvent(const ui::InputEvent& ev) {
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (!hasImage()) return ui::EventResult::Ignored;
        wheel(w->pos, w->dy);
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && hasImage()) {
        if (d->clickCount >= 2) {
            fitAgain();
            dragging_ = false;
            return ui::EventResult::Consumed;
        }
        dragging_ = true;
        moved_ = false;
        last_ = d->pos;
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev); m && !dragging_ && !spots_.empty()) {
        // la bulle sous la souris (au zoom et au deplacement du moment)
        if (const int at = spotAt(m->pos); at != hoverSpot_) {
            hoverSpot_ = at;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev); m && dragging_) {
        const float dx = m->pos.x - last_.x, dy = m->pos.y - last_.y;
        if (std::abs(dx) + std::abs(dy) > 2.f) moved_ = true;
        last_ = m->pos;
        if (zoom_ > 1.f) {
            ox_ += dx;
            oy_ += dy;
            clampOffset();
            invalidate();
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev); u && dragging_) {
        dragging_ = false;
        if (!spots_.empty()) hoverSpot_ = spotAt(u->pos);   // 1.45.0 : apres un deplacement, la bulle sous la souris
        if (!moved_ && zoom_ <= 1.f && onOpen) onOpen();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

void CaptureView::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto box = bounds();
    r.fillRect(box, c.inputBg);
    bool drawn = false;
    if (image_.data) {
        auto& cache = HmiImageCache::instance();
        const auto base = cache.resource(r, image_);
        if (base.valid()) {
            iw_ = static_cast<float>(base.width);
            ih_ = static_cast<float>(base.height);
            clampOffset();
            const gfx::Rect ir = imageRect();
            // 1.45.0 : la copie reduite la plus proche de la taille dessinee (lissee) : nette en petit comme en grand
            const auto img = cache.smooth(r, image_, ir.w, ir.h);
            r.pushClip(box);
            r.drawImage(img.tex, ir);
            // les zones qui ont une bulle, au survol de la capture ; celle sous la souris, plus marquee
            if (!spots_.empty() && hovered())
                for (std::size_t i = 0; i < spots_.size(); ++i) {
                    const auto sr = spotRect(i);
                    if (!sr) continue;
                    const bool on = static_cast<int>(i) == hoverSpot_;
                    ui::shapes::strokeRoundedRect(r, *sr, c.accent.withAlpha(on ? 255 : 120), 5.f, on ? 2.f : 1.f);
                }
            r.popClip();
            if (hoverSpot_ >= 0 && hoverSpot_ < static_cast<int>(spots_.size()) && hovered())
                if (const auto sr = spotRect(static_cast<std::size_t>(hoverSpot_))) {
                    const auto& s = spots_[static_cast<std::size_t>(hoverSpot_)];
                    const float bw = std::min(box.w - 16.f, 300.f);
                    const auto lines = wrapText(r, s.tip, kSmall, bw - 20.f);
                    const float bh = static_cast<float>(std::min<std::size_t>(lines.size(), 6)) * 18.f + 14.f;
                    // la bulle sous sa zone ; sinon au-dessus ; toujours dans la case (une zone hors de la vue, au zoom :
                    // la bulle reste au bord le plus proche)
                    const float bx = std::clamp(sr->x, box.x + 8.f, std::max(box.x + 8.f, box.right() - bw - 8.f));
                    float by = sr->bottom() + 6.f;
                    if (by + bh > box.bottom() - 4.f) by = sr->y - bh - 6.f;
                    by = std::clamp(by, box.y + 4.f, std::max(box.y + 4.f, box.bottom() - bh - 4.f));
                    r.fillRoundedRect({bx, by, bw, bh}, gfx::Color::rgb(0x111827), 8.f);
                    float ty = by + 7.f;
                    int n = 0;
                    for (const auto& l : lines) {
                        if (++n > 6) break;
                        r.drawText({bx + 10.f, ty}, fit(r, l, kSmall, bw - 20.f), kSmall, gfx::Color::rgb(0xFFFFFF));
                        ty += 18.f;
                    }
                }
            drawn = true;
        }
    }
    if (!drawn)
        r.drawText({box.x + 12.f, box.y + (box.h - r.lineHeight(kSmall)) * 0.5f}, fit(r, empty_, kSmall, box.w - 24.f), kSmall, c.textMuted);
    else if (hint_) {
        std::string t = zoom_ > 1.f ? "\xC3\x97" + std::to_string(static_cast<int>(std::lround(zoom_ * 100.f))) + " % \xC2\xB7 glisser : d\xC3\xA9placer \xC2\xB7 double clic : ajuster"
                                    : onOpen ? "Molette : zoom \xC2\xB7 clic : en grand" : "Molette : zoom \xC2\xB7 glisser \xC2\xB7 double clic : ajuster";
        if (!spots_.empty()) t = "Survole les zones : ce qu'on y voit \xC2\xB7 " + t;   // 1.45.0 : au zoom aussi
        const float tw = std::min(box.w - 8.f, r.measure(t, kSmall).width + 12.f);
        const gfx::Rect tag{box.right() - tw - 4.f, box.bottom() - r.lineHeight(kSmall) - 8.f, tw, r.lineHeight(kSmall) + 4.f};
        r.fillRect(tag, c.panelBg.withAlpha(215));
        r.drawText({tag.x + 6.f, tag.y + 2.f}, fit(r, t, kSmall, tw - 12.f), kSmall, c.textMuted);
    }
    r.strokeRect(box, c.border, 1.f);
}

// ============================================================ la capture en grand ==
CaptureZoomBody::CaptureZoomBody(CaptureSet set, std::size_t shot, std::function<void()> close)
    : ui::Widget("dialog.capture.corps"), set_(std::move(set)), close_(std::move(close)) {
    image_ = &static_cast<CaptureView&>(addChild(std::make_unique<CaptureView>("dialog.capture.image")));
    image_->setHint(true);
    prev_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("\xE2\x80\xB9", "dialog.capture.precedente")));
    prev_->setTooltip("La capture pr\xC3\xA9" "c\xC3\xA9" "dente (fl\xC3\xA8" "che gauche)");
    next_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("\xE2\x80\xBA", "dialog.capture.suivante")));
    next_->setTooltip("La capture suivante (fl\xC3\xA8" "che droite)");
    closeButton_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Fermer", "dialog.capture.fermer")));
    closeButton_->setStyle(ui::Button::Style::Primary);
    links_ += prev_->clicked->connect([this] { step(-1); });
    links_ += next_->clicked->connect([this] { step(1); });
    links_ += closeButton_->clicked->connect([this] {
        if (close_) close_();
    });
    showShot(set_.shots.empty() ? 0 : std::min(shot, set_.shots.size() - 1));
}

void CaptureZoomBody::showShot(std::size_t n) {
    const bool several = set_.shots.size() > 1;
    prev_->setVisibility(several ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    next_->setVisibility(several ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (set_.shots.empty()) {
        image_->setImage({});
        image_->setSpots({});
        return;
    }
    shot_ = n % set_.shots.size();
    const auto& s = set_.shots[shot_];
    // lue une fois (l'apercu l'avait peut-etre deja lue) : une fleche qui revient ne relit rien
    if (set_.images.size() < set_.shots.size()) set_.images.resize(set_.shots.size());
    if (!set_.images[shot_].data) set_.images[shot_] = capturePng(s.imagePath, captureName(set_.feature, shot_));
    image_->setImage(set_.images[shot_]);
    image_->setSpots(s.spots, s.refW, s.refH);
    invalidateLayout();
    invalidate();
}

void CaptureZoomBody::step(int delta) {
    const std::size_t n = set_.shots.size();
    if (n < 2) return;
    const auto k = static_cast<long long>(n);
    showShot(static_cast<std::size_t>(((static_cast<long long>(shot_) + delta) % k + k) % k));
}

std::string CaptureZoomBody::navText() const {
    if (set_.shots.size() < 2) return {};
    const std::string& caption = set_.shots[shot_].caption;
    return "Capture " + std::to_string(shot_ + 1) + "/" + std::to_string(set_.shots.size()) + (caption.empty() ? std::string() : " : " + caption);
}

std::string CaptureZoomBody::text() const {
    const std::string caption = shot_ < set_.shots.size() ? set_.shots[shot_].caption : std::string();
    if (caption.empty()) return set_.description;
    return set_.description.empty() ? caption : caption + " \xE2\x80\x94 " + set_.description;
}

void CaptureZoomBody::onLayout() {
    const auto r = bounds();
    const float w = std::min(1500.f, std::max(480.f, r.w - 40.f)), h = std::min(980.f, std::max(360.f, r.h - 40.f));
    panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
    closeBox_ = {panel_.right() - 36.f, panel_.y + 9.f, 24.f, 24.f};
    ui::dialogframe::keep(*this, closeBox_);
    const bool several = set_.shots.size() > 1;
    const float top = panel_.y + 52.f;
    const float navH = several ? 36.f : 0.f;
    const float textH = 2.f * 19.f + 10.f;
    const float bottom = panel_.bottom() - 52.f;   // la rangee du bouton Fermer
    const float ih = std::max(80.f, bottom - textH - navH - top);
    image_->setBounds({panel_.x + 16.f, top, panel_.w - 32.f, ih});
    navY_ = top + ih + 6.f;
    if (several) {
        prev_->setBounds({panel_.x + 16.f, navY_, 34.f, 28.f});
        next_->setBounds({panel_.right() - 16.f - 34.f, navY_, 34.f, 28.f});
    }
    textY_ = navY_ + navH + (several ? 0.f : 2.f);
    const float cw = std::max(110.f, closeButton_->sizeHint().preferred.w);
    closeButton_->setBounds({panel_.right() - 16.f - cw, panel_.bottom() - 44.f, cw, 32.f});
}

void CaptureZoomBody::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    r.fillRect(panel_, c.panelBg);
    r.strokeRect(panel_, c.borderStrong, 1.f);
    r.fillRect({panel_.x, panel_.y, panel_.w, 42.f}, c.headerBg);
    r.drawText({panel_.x + 16.f, panel_.y + (42.f - r.lineHeight(kTitle)) * 0.5f}, fit(r, set_.title, kTitle, panel_.w - 70.f), kTitle, c.text);
    r.drawText({closeBox_.x + 7.f, closeBox_.y + 2.f}, "\xC3\x97", kTitle, c.textMuted);
    r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);
    const float W = panel_.w - 32.f;
    // << Capture 2/3 : sa legende >>, entre les fleches
    if (const std::string nav = navText(); !nav.empty()) {
        const float nx = panel_.x + 16.f + 44.f, nw = W - 2.f * 44.f;
        const std::string t = fit(r, nav, gfx::semibold(kSmall), nw);
        const float tw = r.measure(t, gfx::semibold(kSmall)).width;
        r.drawText({nx + std::max(0.f, (nw - tw) * 0.5f), navY_ + (28.f - r.lineHeight(kSmall)) * 0.5f}, t, gfx::semibold(kSmall), c.text);
    }
    int n = 0;
    for (const auto& l : wrapText(r, set_.description, kSmall, W)) {
        if (++n > 2) break;
        r.drawText({panel_.x + 16.f, textY_ + static_cast<float>(n - 1) * 19.f}, fit(r, l, kSmall, W), kSmall, c.text);
    }
}

ui::EventResult CaptureZoomBody::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && closeBox_.contains(d->pos)) {
        if (close_) close_();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ============================================================ les licences de ce PC ==
namespace {

std::string afterRelease(const licensing::LicenseState& st, licensing::Seconds now) {
    if (st.evaluationStartedAt > 0 && !st.evaluationOver && st.evaluationEndsAt > now) {
        const auto days = static_cast<int>((st.evaluationEndsAt - now + licensing::kDay - 1) / licensing::kDay);
        return "Ce PC repasse en D\xC3\xA9" "couverte jusqu'au " + dayOf(st.evaluationEndsAt) + " (" + std::to_string(days) + (days > 1 ? " jours" : " jour") +
               ") : les fonctions de la D\xC3\xA9" "couverte, puis plus de licence.";
    }
    return "Ce PC n'aura plus de licence (sa D\xC3\xA9" "couverte est finie) : aucun projet ne s'y ouvrira ni ne s'y cr\xC3\xA9" "era. Ton travail "
           "ouvert reste ouvert ; Enregistrer et Exporter restent permis.";
}

std::string who(const PcLicence& l) {
    std::string t = l.id;
    if (!l.edition.empty()) t += ", " + l.edition;
    if (!l.customer.empty()) t += ", " + l.customer;
    return t;
}

} // namespace

std::vector<PcLicence> pcLicences(const licensing::LicenseState& st, const service::Record& rec, bool leaseHeldBack, licensing::Seconds now) {
    namespace ls = service;
    using S = licensing::LicenseStatus;
    std::vector<PcLicence> out;
    if (st.status == S::NotConfigured) return out;   // les licences ne sont pas actives dans ce programme
    const std::string edition = !st.licensedEdition.empty() ? st.licensedEdition : st.edition;
    if (!st.licenseId.empty()) {
        PcLicence l;
        l.id = l.title = st.licenseId;
        l.edition = edition;
        l.customer = st.customer;
        l.until = st.expiresAt ? "jusqu'au " + dayOf(st.expiresAt) : std::string("sans fin");
        const std::string status = licensing::statusLabel(st.status);
        if (st.online) {
            if (st.status == S::LeaseExpired || !st.activationUntil) {
                l.kind = PcLicence::Kind::Renew;
                l.state = "En ligne" + std::string(kDot) + "bail expir\xC3\xA9 : connecte ce PC \xC3\xA0 Internet, il se renouvelle tout seul";
                l.tone = 3;
                l.tab = kTabActivation;
            } else {
                l.kind = PcLicence::Kind::Online;
                l.state = "En ligne" + std::string(kDot) + status + kDot + "bail jusqu'au " + ls::dateText(st.activationUntil) + " (" +
                          std::to_string(std::max(0, st.activationDaysLeft)) + " j)";
                l.tone = st.status == S::Valid ? (st.activationWarning ? 2 : 1) : st.status == S::Grace ? 2 : 3;
            }
        } else {
            l.kind = PcLicence::Kind::File;
            l.state = "Li\xC3\xA9" "e \xC3\xA0 ce PC (fichier de l'\xC3\xA9" "diteur)" + std::string(kDot) + status;
            l.tone = st.status == S::Valid ? 1 : st.status == S::Grace ? 2 : 3;
        }
        out.push_back(l);
        // le poste pris d'une licence flottante
        if (st.floating && st.leaseUntil > 0 && !leaseHeldBack && st.status != S::NoFloatingSeat) {
            PcLicence f = l;
            f.kind = PcLicence::Kind::Floating;
            f.title = "Poste flottant";
            const std::string at = ls::dateText(st.leaseUntil);
            f.state = "Poste pris jusqu'\xC3\xA0 " + (at.size() > 11 ? at.substr(11) : at) + " (renouvel\xC3\xA9 toutes les 5 minutes tant que le programme tourne)";
            f.tone = 1;
            f.tab = kTabActivation;
            out.push_back(std::move(f));
        }
        // ses options en cours ou a venir
        for (const auto& o : st.options) {
            const licensing::Seconds end = o.until ? o.until : st.expiresAt;
            if (end && end <= now) continue;   // finie : dans Mes achats
            PcLicence p = l;
            p.kind = PcLicence::Kind::Option;
            p.title = "Option " + (o.label.empty() ? o.id : o.label);
            const bool on = o.activeAt(now, st.expiresAt);
            p.state = on ? std::string("Option active") : "Option \xC3\xA0 venir (d\xC3\xA8s le " + dayOf(o.from) + ")";
            if (o.gift) p.state += std::string(kDot) + "offerte";
            p.until = o.until ? "jusqu'au " + dayOf(o.until - 1) : std::string("jusqu'\xC3\xA0 la fin de la licence");
            p.tone = on ? 1 : 0;
            p.tab = kTabOptions;
            out.push_back(std::move(p));
        }
    }
    // une licence en ligne activee sur ce PC dont l'etat ne dit plus rien (effacee, a renouveler) : sa place est encore prise
    if (!rec.activatedLicense.empty() && rec.activatedLicense != st.licenseId) {
        PcLicence r;
        r.kind = PcLicence::Kind::Renew;
        r.id = r.title = rec.activatedLicense;
        r.state = "En ligne" + std::string(kDot) + "\xC3\xA0 renouveler : elle n'est plus active sur ce PC, mais sa place y est encore prise";
        r.tone = 2;
        r.tab = kTabActivation;
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<PcLicence> pcLicences() {
    service::Record rec;
    if (!service::loadRecord(rec, nullptr)) rec = service::Record{};
    return pcLicences(licensing::getLicenseState(), rec, service::leaseHeldBack(), licensing::systemNow());
}

ReleasePlan releasePlan(const PcLicence& l, const licensing::LicenseState& st, licensing::Seconds now) {
    ReleasePlan p;
    switch (l.kind) {
        case PcLicence::Kind::Online:
        case PcLicence::Kind::Renew:
            p.action = ReleasePlan::Action::Release;
            p.title = "Lib\xC3\xA9rer ce poste";
            p.text = "Ce PC rend sa place dans la licence " + who(l) + " : un autre PC pourra la prendre (tout de suite, ou \xC3\xA0 la fin du bail de "
                     "ce PC si la licence a d\xC3\xA9j\xC3\xA0 chang\xC3\xA9 de PC deux fois ce mois-ci, ou autant que de postes). Les fonctions de la "
                     "licence se ferment ici.\n\n" + afterRelease(st, now) +
                     "\n\nPour la retrouver plus tard sur ce PC : son code, s'il reste un poste libre.";
            p.confirm = "Lib\xC3\xA9rer ce poste";
            p.cancel = "Garder la licence ici";
            break;
        case PcLicence::Kind::Floating:
            p.action = ReleasePlan::Action::ReturnSeat;
            p.title = "Rendre le poste flottant";
            p.text = "Ce PC rend son poste de la licence flottante " + who(l) + " : un coll\xC3\xA8gue peut le prendre tout de suite. Ce PC ne le "
                     "reprend plus tout seul (Licence\xE2\x80\xA6 \xE2\x80\xBA Activation \xE2\x80\xBA Prendre un poste).\n\nSans poste, les fonctions "
                     "de la licence se ferment ici : ce PC est comme sans licence jusqu'\xC3\xA0 ce qu'il en reprenne un. Ton travail ouvert reste "
                     "ouvert ; Enregistrer et Exporter restent permis.";
            p.confirm = "Rendre le poste";
            p.cancel = "Garder le poste";
            break;
        case PcLicence::Kind::File:
            p.title = "Une licence li\xC3\xA9" "e \xC3\xA0 ce PC";
            p.text = "La licence " + who(l) + " est li\xC3\xA9" "e \xC3\xA0 ce PC dans le fichier sign\xC3\xA9 par l'\xC3\xA9" "diteur : elle ne se "
                     "lib\xC3\xA8re pas d'ici. Pour la mettre sur un autre PC, demande \xC3\xA0 l'\xC3\xA9" "diteur de retirer ce PC de la licence "
                     "(Demander ma licence, ou son contact) : il t'envoie une licence \xC3\xA0 jour.";
            break;
        case PcLicence::Kind::Option:
            p.title = "Une option de ta licence";
            p.text = "L'" + (l.title.rfind("Option ", 0) == 0 ? "option " + l.title.substr(7) : l.title) + " suit la licence " + l.id +
                     " : elle ne se lib\xC3\xA8re pas seule. Lib\xC3\xA9rer la licence (sa ligne) lib\xC3\xA8re aussi ses options.";
            break;
    }
    return p;
}

std::string pcLicencesEmptyText(const licensing::LicenseState& st) {
    if (st.status == licensing::LicenseStatus::NotConfigured) return "Les licences ne sont pas actives dans ce programme : tout est ouvert.";
    std::string t = "Aucune licence sur ce PC pour l'instant.";
    if (const std::string d = discoveryText(st); !d.empty()) t += " " + d;
    return t;
}

PcLicencesView::PcLicencesView(std::string id) : ui::Widget(std::move(id)) {
    for (std::size_t i = 0; i < kMaxRows; ++i) {
        auto look = std::make_unique<ui::Button>("Regarder", this->id() + ".regarder." + std::to_string(i + 1));
        auto release = std::make_unique<ui::Button>("Lib\xC3\xA9rer\xE2\x80\xA6", this->id() + ".liberer." + std::to_string(i + 1));
        release->setStyle(ui::Button::Style::Danger);
        look_[i] = &static_cast<ui::Button&>(addChild(std::move(look)));
        release_[i] = &static_cast<ui::Button&>(addChild(std::move(release)));
        look_[i]->setVisibility(ui::Visibility::Collapsed);
        release_[i]->setVisibility(ui::Visibility::Collapsed);
        // une copie de la ligne : ce que fait le rappel (une fenetre, un onglet) peut relire les lignes
        links_ += look_[i]->clicked->connect([this, i] {
            if (i >= rows_.size() || !onLook) return;
            const PcLicence row = rows_[i];
            onLook(row);
        });
        links_ += release_[i]->clicked->connect([this, i] {
            if (i >= rows_.size() || !onRelease) return;
            const PcLicence row = rows_[i];
            onRelease(row);
        });
    }
}

void PcLicencesView::setRows(std::vector<PcLicence> rows, std::string emptyText) {
    if (rows.size() > kMaxRows) rows.resize(kMaxRows);
    rows_ = std::move(rows);
    empty_ = std::move(emptyText);
    for (std::size_t i = 0; i < kMaxRows; ++i) {
        const bool on = i < rows_.size();
        look_[i]->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        release_[i]->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        if (!on) continue;
        const auto k = rows_[i].kind;
        look_[i]->setTooltip(k == PcLicence::Kind::Option     ? "L'onglet Options : cette option, ses dates"
                             : k == PcLicence::Kind::Floating ? "L'onglet Activation : le poste flottant"
                             : k == PcLicence::Kind::Renew    ? "L'onglet Activation : le bail, Renouveler maintenant"
                                                              : "L'onglet Ma licence : ce que dit cette licence");
        release_[i]->setTooltip(k == PcLicence::Kind::Floating ? "Rend le poste flottant (une question d'abord)"
                                : k == PcLicence::Kind::File   ? "Une licence par fichier se lib\xC3\xA8re chez l'\xC3\xA9" "diteur : ce qu'il faut faire"
                                : k == PcLicence::Kind::Option ? "Une option suit sa licence"
                                                               : "Rend la place de ce PC dans la licence (une question d'abord)");
    }
    invalidateLayout();
    invalidate();
}

float PcLicencesView::wantedHeight() const noexcept {
    return kHeader + (rows_.empty() ? kEmpty : static_cast<float>(rows_.size()) * (kRow + kGap));
}

void PcLicencesView::onLayout() {
    const auto b = bounds();
    boxes_.clear();
    float y = b.y + kHeader;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const gfx::Rect box{b.x, y, b.w, kRow};
        boxes_.push_back(box);
        const float rw = std::max(96.f, std::ceil(release_[i]->sizeHint().preferred.w));
        const float lw = std::max(96.f, std::ceil(look_[i]->sizeHint().preferred.w));
        const float by = box.y + (kRow - 28.f) * 0.5f;
        release_[i]->setBounds({box.right() - 10.f - rw, by, rw, 28.f});
        look_[i]->setBounds({box.right() - 10.f - rw - 8.f - lw, by, lw, 28.f});
        y += kRow + kGap;
    }
}

void PcLicencesView::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    const std::string head = "Les licences de ce PC";
    r.drawText({b.x, b.y + 2.f}, fit(r, head, gfx::semibold(kValue), b.w), gfx::semibold(kValue), c.text);
    if (!rows_.empty()) {
        const float hw = r.measure(head, gfx::semibold(kValue)).width;
        r.drawText({b.x + hw + 10.f, b.y + 4.f}, "(" + std::to_string(rows_.size()) + ")", kSmall, c.textMuted);
    }
    if (rows_.empty()) {
        r.drawText({b.x, b.y + kHeader}, fit(r, empty_, kSmall, b.w), kSmall, c.textMuted);
        return;
    }
    const gfx::Color good = ctx.theme.tone(ui::Tone::Ok, c.text), warn = ctx.theme.tone(ui::Tone::Warning, c.text),
                     bad = ctx.theme.tone(ui::Tone::Error, c.text);
    for (std::size_t i = 0; i < rows_.size() && i < boxes_.size(); ++i) {
        const auto& l = rows_[i];
        const auto& box = boxes_[i];
        r.fillRect(box, c.inputBg);
        r.strokeRect(box, c.border, 1.f);
        const gfx::Color tone = l.tone == 1 ? good : l.tone == 2 ? warn : l.tone == 3 ? bad : c.textMuted;
        r.fillRect({box.x, box.y, 3.f, box.h}, tone);
        const float tx = box.x + 12.f, tw = std::max(40.f, look_[i]->bounds().x - 12.f - tx);
        std::string line1 = l.title;
        if (l.kind == PcLicence::Kind::Floating || l.kind == PcLicence::Kind::Option) line1 += kDot + l.id;
        if (!l.edition.empty()) line1 += kDot + l.edition;
        if (!l.customer.empty()) line1 += kDot + l.customer;
        r.drawText({tx, box.y + 8.f}, fit(r, line1, gfx::semibold(kSmall), tw), gfx::semibold(kSmall), c.text);
        std::string line2 = l.state;
        if (!l.until.empty()) line2 += kDot + l.until;
        r.drawText({tx, box.y + 30.f}, fit(r, line2, kSmall, tw), kSmall, l.tone == 0 ? c.textMuted : tone);
    }
}

} // namespace app::licence
