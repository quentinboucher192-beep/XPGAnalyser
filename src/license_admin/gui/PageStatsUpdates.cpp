// =============================================================================
//  license_admin/gui/PageStatsUpdates.cpp - 1.47.0 : STATISTIQUES > MISES A
//  JOUR (la maquette validee, planche 09)
// -----------------------------------------------------------------------------
//  La vue de l'onglet << Mises a jour >> de la page Statistiques (PageStats.cpp :
//  la periode et << les PC : tous, les Studios, les Admins >> sont dans sa
//  barre). GET /admin/stats/mises-a-jour (docs/STATISTIQUES.md, par. 6), par
//  runQuiet ; les noms des admins : GET /admin/stats/admins.
//    - cinq tuiles : les PC a jour (contre la version d'avant au meme jour), la
//      version actuelle, le delai median avant la mise a jour, les PC pas a jour
//      (combien ont 2 versions de retard ou plus), les Admins a jour ;
//    - << L'adoption de chaque version >> : une courbe par version, ALIGNEES SUR
//      LEUR JOUR DE PUBLICATION (J0, J+7...) - la vitesse de chaque version, a
//      jour egal ; chaque courbe s'arrete a la version suivante ;
//    - << Les PC a jour >> : la part sur la derniere version, chaque jour (les
//      Studios, les Admins), les publications reperees ;
//    - << Delai avant la mise a jour >> : la mediane et la moyenne par version ;
//    - << Les versions publiees >> : les PC dessus aujourd'hui (Studios, Admins,
//      leur part) ;
//    - << PC pas a jour >> : les plus en retard d'abord, trois filtres (tous, 2
//      versions de retard ou plus, pas vus depuis 30 j) ; un clic : le PC dans
//      Activations ; << Voir les 119 dans Activations >>.
// =============================================================================
#include "StatsKit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui::statskit {

namespace {

using gfx::Color;
using gfx::FontId;
using gfx::Rect;

constexpr Color kInk = Color::rgb(0x0F172A), kInk2 = Color::rgb(0x334155), kMuted = Color::rgb(0x64748B), kFaint = Color::rgb(0x94A3B8);
constexpr Color kLine = Color::rgb(0xE3E8EF), kSunken = Color::rgb(0xEEF2F7), kAmber = Color::rgb(0xD97706), kGreen = Color::rgb(0x16A34A);
constexpr Color kViolet = Color::rgb(0x6D4AFF);
const std::string kDash = "\xE2\x80\x94";

// Les versions publiees, de la plus ancienne a la plus recente.
std::vector<stats::VersionRow> publishedOldestFirst(const stats::UpdatesReply& r) {
    std::vector<stats::VersionRow> v;
    for (const auto& x : r.versions)
        if (x.published > 0) v.push_back(x);
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.published < b.published; });
    return v;
}
std::string daysText(double seconds) {
    if (!stats::isNumber(seconds)) return kDash;
    const double d = seconds / 86400.0;
    return ch::fr::decimal(d, d < 10 ? 1 : 0) + "\xC2\xA0j";
}
std::string ageText(Seconds now, Seconds at) {
    if (at <= 0) return {};
    const long d = static_cast<long>((now - at) / 86400);
    return d <= 0 ? std::string("aujourd'hui") : "il y a " + std::to_string(d) + "\xC2\xA0j";
}

// ---- une carte : son titre, sa precision, une note (une icone d'info), et son contenu dessous ----
class Panel final : public ui::Widget {
public:
    Panel(std::string id, std::string title, std::string small) : ui::Widget(std::move(id)), title_(std::move(title)), small_(std::move(small)) {}
    template <class T>
    T& setBody(std::unique_ptr<T> w) {
        body_ = &addChild(std::move(w));
        invalidateLayout();
        return static_cast<T&>(*body_);
    }
    void setSmall(std::string s) {
        small_ = std::move(s);
        invalidate();
    }
    // Une note en tete (sous le titre) ou en pied.
    void setNote(std::string note, bool top) {
        note_ = std::move(note);
        noteTop_ = top;
        invalidateLayout();
    }
    void setAction(ui::Button* b) {
        action_ = b;
        invalidateLayout();
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        float top = b.y + 44.f, bottom = b.bottom() - 10.f;
        const float nh = noteHeight(b.w - 32.f);
        if (!note_.empty()) {
            if (noteTop_) top += nh + 6.f;
            else bottom -= nh + 8.f;
        }
        if (action_) {
            const float w = action_->sizeHint().preferred.w + 12.f;
            action_->setBounds({b.right() - 14.f - w, b.y + 10.f, w, 30.f});
        }
        if (body_) body_->setBounds({b.x + 16.f, top, b.w - 32.f, std::max(20.f, bottom - top)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        paintCard(ctx.r, ctx.theme, b);
        const FontId tf = gfx::semibold(FontId{14});
        ctx.r.drawText({b.x + 16.f, b.y + 15.f}, title_, tf, kInk);
        if (!small_.empty()) {
            const float x = b.x + 16.f + ctx.r.measure(title_, tf).width + 9.f;
            const float room = (action_ ? action_->bounds().x - 8.f : b.right() - 16.f) - x;
            ctx.r.drawText({x, b.y + 17.f}, ch::detail::fit(ctx.r, small_, FontId{12}, room), FontId{12}, kFaint);
        }
        if (!note_.empty()) {
            const float nh = noteHeight(b.w - 32.f);
            const float y = noteTop_ ? b.y + 42.f : b.bottom() - 10.f - nh;
            ui::drawIcon(ctx.r, ui::Icon::Info, {b.x + 16.f, y + 2.f, 14.f, 14.f}, kViolet);
            float ly = y;
            for (const auto& l : wrap(note_, b.w - 56.f)) {
                ctx.r.drawText({b.x + 38.f, ly}, l, FontId{12}, kMuted);
                ly += 17.f;
            }
        }
    }

private:
    static std::vector<std::string> wrap(const std::string& text, float w) {
        std::vector<std::string> lines;
        std::string line, word;
        const auto flush = [&] {
            if (word.empty()) return;
            const std::string t = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui::measureWidth(t, FontId{12}) > w) {
                lines.push_back(line);
                line = word;
            } else {
                line = t;
            }
            word.clear();
        };
        for (const char c : text) {
            if (c == ' ') flush();
            else word += c;
        }
        flush();
        if (!line.empty()) lines.push_back(line);
        return lines;
    }
    [[nodiscard]] float noteHeight(float w) const { return note_.empty() ? 0.f : static_cast<float>(wrap(note_, w - 24.f).size()) * 17.f; }
    std::string title_, small_, note_;
    bool        noteTop_{true};
    ui::Widget* body_{nullptr};
    ui::Button* action_{nullptr};
};

// ---- les cinq tuiles ----
struct Tile {
    ui::Icon    icon{ui::Icon::None};
    std::string label, value, valueSmall, sub;
    int         subTone{0};   // 0 discret, 1 bien (vert, une fleche), -1 attention (ambre, un triangle)
    std::string subStrong;     // le debut de la ligne en gras (<< +6 pts >>)
};
class Tiles final : public ui::Widget {
public:
    explicit Tiles(std::string id) : ui::Widget(std::move(id)) {}
    void set(std::vector<Tile> t) {
        tiles_ = std::move(t);
        invalidate();
    }
    [[nodiscard]] const std::vector<Tile>& tiles() const noexcept { return tiles_; }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        const std::size_t n = std::max<std::size_t>(1, tiles_.size());
        const float gap = 12.f;
        const float w = (b.w - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
        for (std::size_t i = 0; i < tiles_.size(); ++i) {
            const Tile& t = tiles_[i];
            const Rect r{b.x + static_cast<float>(i) * (w + gap), b.y, w, b.h};
            paintCard(ctx.r, ctx.theme, r);
            float x = r.x + 16.f;
            ui::drawIcon(ctx.r, t.icon, {x, r.y + 13.f, 14.f, 14.f}, kMuted);
            ctx.r.drawText({x + 20.f, r.y + 12.f}, ch::detail::fit(ctx.r, t.label, FontId{13}, r.w - 52.f), FontId{13}, kInk2);
            const FontId vf = gfx::semibold(FontId{24});
            const float vy = r.y + 34.f;
            ctx.r.drawText({x, vy}, t.value, vf, kInk);
            if (!t.valueSmall.empty()) {
                const float vx = x + ctx.r.measure(t.value, vf).width + 8.f;
                ctx.r.drawText({vx, vy + 9.f}, ch::detail::fit(ctx.r, t.valueSmall, FontId{14}, r.right() - 12.f - vx), FontId{14}, kInk2);
            }
            const float sy = r.y + r.h - 26.f;
            float sx = x;
            if (t.subTone < 0) {
                ui::drawIcon(ctx.r, ui::Icon::Warning, {sx, sy + 1.f, 13.f, 13.f}, kAmber);
                sx += 18.f;
            }
            if (!t.subStrong.empty()) {
                const Color c = t.subTone > 0 ? kGreen : t.subTone < 0 ? kAmber : kInk2;
                ctx.r.drawText({sx, sy}, t.subStrong, gfx::semibold(FontId{12}), c);
                sx += ctx.r.measure(t.subStrong, gfx::semibold(FontId{12})).width + 5.f;
            }
            const Color sc = t.subTone < 0 ? Color::rgb(0xB45309) : kMuted;
            ctx.r.drawText({sx, sy}, ch::detail::fit(ctx.r, t.sub, t.subTone < 0 ? gfx::semibold(FontId{12}) : FontId{12}, r.right() - 10.f - sx),
                           t.subTone < 0 ? gfx::semibold(FontId{12}) : FontId{12}, sc);
        }
    }

private:
    std::vector<Tile> tiles_;
};

// Un badge de version (chasse fixe ; la derniere en vert, une en retard en ambre, sinon gris).
void versionBadge(gfx::IRenderer& r, float x, float cy, const std::string& v, int tone) {
    const FontId f = FontId{12};
    const float w = r.measure(v, f).width + 14.f;
    const Color bg = tone > 0 ? Color::rgb(0xE8F6EE) : tone < 0 ? Color::rgb(0xFEF3E2) : kSunken;
    const Color fg = tone > 0 ? Color::rgb(0x15803D) : tone < 0 ? Color::rgb(0xB45309) : kInk2;
    r.fillRoundedRect({x, cy - 11.f, w, 22.f}, bg, 6.f);
    r.drawText({x + 7.f, cy - r.lineHeight(f) * 0.5f}, v, f, fg);
}

// ---- les versions publiees (un tableau) ----
class VersionsTable final : public ui::Widget {
public:
    explicit VersionsTable(std::string id) : ui::Widget(std::move(id)) {}
    void set(const stats::UpdatesReply& r, Seconds now) {
        reply_ = r;
        now_ = now;
        invalidate();
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        const char* heads[] = {"VERSION", "PUBLI\xC3\x89" "E LE", "STUDIOS", "ADMINS", "PART DES PC"};
        const float cx[] = {0.f, 0.27f, 0.47f, 0.59f, 0.70f};
        for (int i = 0; i < 5; ++i) ctx.r.drawText({b.x + b.w * cx[i], b.y}, heads[i], gfx::semibold(FontId{10}), kFaint);
        ctx.r.fillRect({b.x, b.y + 20.f, b.w, 1.f}, kLine);
        int seen = 0;
        for (const auto& v : reply_.versions) seen += v.studios + v.admins;
        if (reply_.versions.empty()) {
            ctx.r.drawText({b.x, b.y + 32.f}, "Aucun PC vu sur la p\xC3\xA9riode.", FontId{12}, kMuted);
            return;
        }
        const float rowH = std::clamp((b.h - 24.f) / static_cast<float>(reply_.versions.size()), 24.f, 46.f);
        float y = b.y + 24.f;
        const auto published = publishedOldestFirst(reply_);
        for (std::size_t i = 0; i < reply_.versions.size(); ++i) {
            if (y + rowH > b.bottom() + 1.f) break;
            const auto& v = reply_.versions[i];
            const float cy = y + rowH * 0.5f;
            int behind = 0;
            for (const auto& p : published)
                if (p.published > v.published) ++behind;
            const bool latest = v.version == reply_.latest;
            versionBadge(ctx.r, b.x, cy, v.version, latest ? 1 : (behind >= 2 || v.published == 0) ? -1 : 0);
            const float bx = b.x + ctx.r.measure(v.version, FontId{12}).width + 20.f;
            std::string tag;
            if (latest) tag = "actuelle";
            else if (v.published == 0 || i + 1 == reply_.versions.size()) tag = "et avant";
            else if (v.version.size() > 2 && v.version.substr(v.version.rfind('.') + 1) != "0") tag = "correctif";
            if (!tag.empty() && bx + 50.f < b.x + b.w * cx[1]) ctx.r.drawText({bx, cy - 7.f}, tag, FontId{12}, kMuted);
            if (v.published > 0) {
                const bool two = rowH >= 38.f && (latest || i < 2);
                ctx.r.drawText({b.x + b.w * cx[1], two ? cy - 16.f : cy - 8.f}, dateFr(v.published), FontId{13}, kInk);
                if (two) ctx.r.drawText({b.x + b.w * cx[1], cy + 2.f}, ageText(now_, v.published), FontId{12}, kMuted);
            } else {
                ctx.r.drawText({b.x + b.w * cx[1], cy - 8.f}, kDash, FontId{13}, kFaint);
            }
            const auto num = [&](float at, int n) {
                const std::string t = std::to_string(n);
                const FontId f = gfx::semibold(FontId{13});
                ctx.r.drawText({b.x + b.w * at - ctx.r.measure(t, f).width, cy - 8.f}, t, f, n ? kInk : kFaint);
            };
            num(cx[2] + 0.07f, v.studios);
            num(cx[3] + 0.06f, v.admins);
            const double part = seen > 0 ? (v.studios + v.admins) * 100.0 / seen : 0.0;
            const float px = b.x + b.w * cx[4];
            const float pw = b.w * 0.19f;
            ch::paintInlineBar(ctx.r, ctx.theme, {px, cy - 3.f, pw, 6.f}, part / 100.0, ch::colors::kOne);
            ctx.r.drawText({px + pw + 10.f, cy - 8.f}, ch::fr::percent(part), gfx::semibold(FontId{13}), kInk);
            if (i + 1 < reply_.versions.size()) ctx.r.fillRect({b.x, y + rowH - 0.5f, b.w, 1.f}, Color::rgb(0xF1F4F8));
            y += rowH;
        }
    }

private:
    stats::UpdatesReply reply_;
    Seconds             now_{0};
};

// ---- les PC pas a jour (une liste) ----
class BehindList final : public ui::Widget {
public:
    BehindList(std::string id, ShellHost host) : ui::Widget(id), host_(std::move(host)) {
        filter_ = &static_cast<SegGroup&>(addChild(std::make_unique<SegGroup>(id + ".filtre", std::vector<SegGroup::Item>{{"tous", "Tous"}, {"retard", "2 versions de retard ou plus"}, {"absents", "pas vus depuis 30 j"}})));
        filter_->onPick = [this](const std::string&) {
            invalidate();
        };
        all_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Voir dans Activations", id + ".voir")));
        all_->setStyle(ui::Button::Style::Flat);
        links_ += all_->clicked->connect([this] {
            ListFilter f;
            f.page = "activations";
            f.what = "Activations : les PC pas \xC3\xA0 jour ont une version plus ancienne que " + reply_.latest + " (colonne Logiciel)";
            const std::string said = openList(host_, *this, f, now_);
            if (host_.flash && !said.empty()) host_.flash(said, Tone::Info);
        });
    }
    void set(const stats::UpdatesReply& r, Seconds now, std::function<std::string(const std::string&)> adminName) {
        reply_ = r;
        now_ = now;
        adminName_ = std::move(adminName);
        int two = 0, away = 0;
        for (const auto& p : r.behind) {
            two += p.behind >= 2 ? 1 : 0;
            away += now - p.seen > 30 * 86400 ? 1 : 0;
        }
        counts_ = {r.behindTotal, two, away};
        filter_->setLabel("tous", "Tous \xC2\xB7 " + std::to_string(r.behindTotal));
        filter_->setLabel("retard", "2 versions de retard ou plus \xC2\xB7 " + std::to_string(two));
        filter_->setLabel("absents", "pas vus depuis 30 j \xC2\xB7 " + std::to_string(away));
        all_->setText("Voir les " + std::to_string(r.behindTotal) + " dans Activations \xE2\x80\xBA");
        invalidateLayout();
        invalidate();
    }
    [[nodiscard]] std::vector<const stats::BehindPc*> shown() const {
        std::vector<const stats::BehindPc*> out;
        const std::string k = filter_->current();
        for (const auto& p : reply_.behind) {
            if (k == "retard" && p.behind < 2) continue;
            if (k == "absents" && now_ - p.seen <= 30 * 86400) continue;
            out.push_back(&p);
        }
        return out;
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        filter_->setBounds({b.x, b.y, std::min(b.w, filter_->preferredWidth() + 60.f), 32.f});
        const float w = all_->sizeHint().preferred.w + 8.f;
        all_->setBounds({b.right() - w, b.bottom() - 26.f, w, 26.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        const float top = b.y + 42.f;
        const char* heads[] = {"PC", "CLIENT", "VERSION", "VU"};
        const float cx[] = {0.f, 0.36f, 0.66f, 0.84f};
        for (int i = 0; i < 4; ++i) ctx.r.drawText({b.x + b.w * cx[i], top}, heads[i], gfx::semibold(FontId{10}), kFaint);
        ctx.r.fillRect({b.x, top + 18.f, b.w, 1.f}, kLine);
        const auto rows = shown();
        const float rowH = 30.f;
        float y = top + 22.f;
        std::size_t drawn = 0;
        rowTops_.clear();
        for (const auto* p : rows) {
            if (y + rowH > b.bottom() - 30.f) break;
            const float cy = y + rowH * 0.5f;
            ctx.r.drawText({b.x, cy - 8.f}, ch::detail::fit(ctx.r, p->name.empty() ? p->key.substr(0, 12) : p->name, gfx::semibold(FontId{12}), b.w * 0.35f),
                           gfx::semibold(FontId{12}), kInk);
            std::string who = p->customer;
            if (!p->admin.empty()) who = adminName_ ? adminName_(p->admin) : p->admin;
            float wx = b.x + b.w * cx[1];
            if (!p->admin.empty()) {
                ctx.r.fillRoundedRect({wx, cy - 8.f, 16.f, 16.f}, ch::colors::kAdmins[2], 4.f);
                ctx.r.drawText({wx + 4.f, cy - 7.f}, who.substr(0, 1), gfx::semibold(FontId{10}), Color::rgb(0xFFFFFF));
                wx += 22.f;
            }
            ctx.r.drawText({wx, cy - 8.f}, ch::detail::fit(ctx.r, who, FontId{13}, b.x + b.w * cx[2] - 8.f - wx), FontId{13}, kInk);
            versionBadge(ctx.r, b.x + b.w * cx[2], cy, p->version, p->behind >= 2 ? -1 : 0);
            ctx.r.drawText({b.x + b.w * cx[3], cy - 8.f}, ch::detail::fit(ctx.r, seenText(now_, p->seen), FontId{12}, b.w * 0.16f), FontId{12}, kMuted);
            rowTops_.push_back(y);
            ctx.r.fillRect({b.x, y + rowH - 0.5f, b.w, 1.f}, Color::rgb(0xF1F4F8));
            y += rowH;
            ++drawn;
        }
        const int total = filter_->current() == "tous" ? reply_.behindTotal : static_cast<int>(rows.size());
        if (rows.empty()) ctx.r.drawText({b.x, top + 30.f}, "Tous les PC vus sont \xC3\xA0 jour.", FontId{12}, kMuted);
        else if (static_cast<int>(drawn) < total) ctx.r.drawText({b.x, b.bottom() - 21.f}, "et " + std::to_string(total - static_cast<int>(drawn)) + " de plus", FontId{12}, kMuted);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
            const auto rows = shown();
            for (std::size_t i = 0; i < rowTops_.size() && i < rows.size(); ++i)
                if (m->pos.y >= rowTops_[i] && m->pos.y < rowTops_[i] + 30.f && m->pos.x >= bounds().x && m->pos.x <= bounds().right()) {
                    ListFilter f;
                    f.page = "activations";
                    f.search = rows[i]->name;
                    f.what = "Activations \xC2\xB7 " + rows[i]->name;
                    const std::string said = openList(host_, *this, f, now_);
                    if (host_.flash && !said.empty()) host_.flash(said, Tone::Info);
                    return ui::EventResult::Consumed;
                }
        }
        return ui::EventResult::Ignored;
    }

private:
    ShellHost                                  host_;
    SegGroup*                                  filter_{nullptr};
    ui::Button*                                all_{nullptr};
    stats::UpdatesReply                        reply_;
    Seconds                                    now_{0};
    std::vector<int>                           counts_;
    std::vector<float>                         rowTops_;
    std::function<std::string(const std::string&)> adminName_;
    core::ConnectionScope                      links_;
};

} // namespace

// ============================================================================ la vue
class UpdatesView final : public ui::Widget, public Reloadable {
public:
    UpdatesView(ShellHost host, std::function<Choice()> getChoice, std::function<std::string()> getSoftware)
        : ui::Widget("stats.maj"), host_(std::move(host)), getChoice_(std::move(getChoice)), getSoftware_(std::move(getSoftware)) {
        tiles_ = &static_cast<Tiles&>(addChild(std::make_unique<Tiles>("stats.maj.tuiles")));
        adoptionPanel_ = &static_cast<Panel&>(addChild(std::make_unique<Panel>("stats.maj.adoption.carte", "L'adoption de chaque version",
                                                                                 "les PC qui l'ont install\xC3\xA9" "e, jour apr\xC3\xA8s jour")));
        adoptionPanel_->setNote("Align\xC3\xA9" "es sur leur jour de publication (et pas sur le calendrier) : on compare la vitesse de chaque version, \xC3\xA0 jour \xC3\xA9gal. "
                                "Chaque courbe s'arr\xC3\xAAte \xC3\xA0 la version suivante. Le calendrier : \xC3\xA0 droite.",
                                true);
        adoption_ = &adoptionPanel_->setBody(std::make_unique<ch::TimeChart>("stats.maj.adoption"));
        sharePanel_ = &static_cast<Panel&>(addChild(std::make_unique<Panel>("stats.maj.ajour.carte", "Les PC \xC3\xA0 jour", "sur la derni\xC3\xA8re version, chaque jour")));
        share_ = &sharePanel_->setBody(std::make_unique<ch::TimeChart>("stats.maj.ajour"));
        delayPanel_ = &static_cast<Panel&>(addChild(std::make_unique<Panel>("stats.maj.delai.carte", "D\xC3\xA9lai avant la mise \xC3\xA0 jour", "")));
        delayPanel_->setNote("Le moyen est tir\xC3\xA9 par les PC rarement allum\xC3\xA9s ; la derni\xC3\xA8re : sur les PC d\xC3\xA9j\xC3\xA0 \xC3\xA0 jour seulement.", false);
        delays_ = &delayPanel_->setBody(std::make_unique<ch::BarList>("stats.maj.delai"));
        versionsPanel_ = &static_cast<Panel&>(addChild(std::make_unique<Panel>("stats.maj.versions.carte", "Les versions publi\xC3\xA9" "es", "les PC dessus aujourd'hui")));
        versions_ = &versionsPanel_->setBody(std::make_unique<VersionsTable>("stats.maj.versions"));
        behindPanel_ = &static_cast<Panel&>(addChild(std::make_unique<Panel>("stats.maj.retard.carte", "PC pas \xC3\xA0 jour", "les plus en retard d'abord")));
        behind_ = &behindPanel_->setBody(std::make_unique<BehindList>("stats.maj.retard", host_));
        deploy_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("D\xC3\xA9ployer le service", "stats.maj.deployer")));
        deploy_->setStyle(ui::Button::Style::Primary);
        deploy_->setIcon(ui::Icon::ArrowUp);
        links_ += deploy_->clicked->connect([this] {
            if (deployer::running() || (host_.busy && host_.busy())) return;
            std::weak_ptr<bool> alive = alive_;
            if (host_.openDialog) host_.openDialog(makeDeployDialog(host_, [this, alive] {
                if (!alive.expired()) reload();
            }));
        });
        const auto pick = [this](const ch::Pick& p) { picked(p); };
        adoption_->setOnPick(pick);
        share_->setOnPick(pick);
        delays_->setOnPick(pick);
        fill();
    }

    void reload() override {
        Workspace& w = host_.workspace();
        bool old = false;
        note_ = blocked(w, &old);
        tooOld_ = old;
        if (!note_.empty()) {
            has_ = false;
            fill();
            return;
        }
        if (loading_) {
            again_ = true;
            return;
        }
        const Choice c = getChoice_ ? getChoice_() : Choice{};
        stats::UpdatesQuery q;
        q.period = periodOf(c, w.now(), allFromOf(w));
        q.step = daysOf(q.period) <= 200 ? stats::Step::Day : stats::Step::Week;
        q.days = 42;
        q.software = getSoftware_ ? getSoftware_() : std::string("tous");
        const Workspace* wp = &w;
        auto out = std::make_shared<stats::UpdatesReply>();
        auto admins = std::make_shared<stats::AdminsReply>();
        loading_ = true;
        problem_.clear();
        std::weak_ptr<bool> alive = alive_;
        const bool started = runQuietly(
            host_, "Statistiques : les mises \xC3\xA0 jour",
            [wp, q, out, admins] {
                OpResult r = fetchUpdates(*wp, q, *out);
                if (r.ok) (void)fetchAdmins(*wp, q.period, *admins);
                return r;
            },
            [this, alive, out, admins, q](const OpResult& r) {
                if (alive.expired()) return;
                loading_ = false;
                if (r.ok) {
                    reply_ = *out;
                    admins_ = *admins;
                    query_ = q;
                    has_ = true;
                } else {
                    problem_ = r.message;
                    if (saysTooOld(r.message)) {
                        note_ = r.message;
                        tooOld_ = true;
                    }
                }
                fill();
                if (again_) {
                    again_ = false;
                    reload();
                }
            });
        if (!started) {
            loading_ = false;
            again_ = true;
        }
    }

    [[nodiscard]] const stats::UpdatesReply& reply() const noexcept { return reply_; }
    [[nodiscard]] bool loaded() const noexcept { return has_; }

protected:
    void onLayout() override {
        const Rect b = bounds();
        float y = b.y;
        deploy_->setVisibility(tooOld_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        bannerRect_ = {};
        const std::string banner = !note_.empty() ? note_ : problem_.empty() ? std::string() : "Les mises \xC3\xA0 jour ne se lisent pas : " + problem_;
        if (!banner.empty()) {
            const float bw = tooOld_ ? deploy_->sizeHint().preferred.w + 12.f : 0.f;
            const float bh = bannerHeight(banner, b.w, bw + 16.f);
            bannerRect_ = {b.x, y, b.w, bh};
            if (tooOld_) deploy_->setBounds({b.right() - bw - 12.f, y + (bh - 32.f) * 0.5f, bw, 32.f});
            y += bh + 10.f;
        }
        const float gap = 12.f;
        const float tilesH = 96.f;
        tiles_->setBounds({b.x, y, b.w, tilesH});
        y += tilesH + gap;
        const float rest = std::max(420.f, b.bottom() - y);
        const float h2 = std::floor((rest - gap) * 0.47f), h3 = rest - gap - h2;
        const float w2a = std::floor((b.w - gap) * 0.615f);
        adoptionPanel_->setBounds({b.x, y, w2a, h2});
        sharePanel_->setBounds({b.x + w2a + gap, y, b.w - w2a - gap, h2});
        y += h2 + gap;
        const float wa = std::floor((b.w - 2 * gap) * 0.22f), wb = std::floor((b.w - 2 * gap) * 0.41f);
        delayPanel_->setBounds({b.x, y, wa, h3});
        versionsPanel_->setBounds({b.x + wa + gap, y, wb, h3});
        behindPanel_->setBounds({b.x + wa + wb + 2 * gap, y, b.w - wa - wb - 2 * gap, h3});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        if (!bannerRect_.empty())
            paintBanner(ctx.r, ctx.theme, bannerRect_, !note_.empty() ? note_ : "Les mises \xC3\xA0 jour ne se lisent pas : " + problem_, true,
                        tooOld_ ? deploy_->bounds().w + 28.f : 0.f);
    }

private:
    [[nodiscard]] std::string adminName(const std::string& id) const {
        if (const stats::AdminUsage* a = admins_.find(id); a && !a->name.empty()) return a->name;
        return id;
    }

    void fill() {
        const Seconds now = host_.workspace().now();
        const std::string waiting = !note_.empty() ? (tooOld_ ? std::string("D\xC3\xA9ploie la 1.47.0 du service pour ces chiffres.") : note_)
                                    : !problem_.empty() ? problem_
                                                        : std::string("Lecture des versions\xE2\x80\xA6");
        if (!has_) {
            tiles_->set({{ui::Icon::Ok, "PC \xC3\xA0 jour", kDash, "", "", 0, ""},
                         {ui::Icon::Module, "La version actuelle", kDash, "", "", 0, ""},
                         {ui::Icon::History, "D\xC3\xA9lai m\xC3\xA9" "dian avant la mise \xC3\xA0 jour", kDash, "", "", 0, ""},
                         {ui::Icon::History, "PC pas \xC3\xA0 jour", kDash, "", "", 0, ""},
                         {ui::Icon::User, "Admins \xC3\xA0 jour", kDash, "", "", 0, ""}});
            ch::TimeChartData e;
            e.emptyText = waiting;
            adoption_->setData(e);
            share_->setData(e);
            ch::BarListData bl;
            bl.emptyText = waiting;
            delays_->setData(bl);
            versions_->set({}, now);
            behind_->set({}, now, {});
            invalidateLayout();
            invalidate();
            return;
        }
        const auto& r = reply_;
        const std::string soft = query_.software;
        const bool admins = soft == "admins";
        const std::string group = admins ? "admins" : "studios";
        // ---- les tuiles ----
        std::vector<Tile> tiles;
        {
            const int seen = admins ? r.seenAdmins : soft == "studios" ? r.seenStudios : r.seenStudios + r.seenAdmins;
            const int up = admins ? r.upToDateAdmins : soft == "studios" ? r.upToDateStudios : r.upToDateStudios + r.upToDateAdmins;
            const double pct = seen > 0 ? up * 100.0 / seen : ch::kNone;
            Tile t{ui::Icon::Ok, "PC \xC3\xA0 jour", stats::isNumber(pct) ? ch::fr::percent(pct) : kDash, std::to_string(up) + " sur " + std::to_string(seen), "", 0, ""};
            // contre la version d'avant, au meme jour apres sa publication
            const auto pub = publishedOldestFirst(r);
            if (pub.size() >= 2 && r.latestPublished > 0) {
                const int k = static_cast<int>((now - r.latestPublished) / 86400);
                const auto* cur = r.curve(r.latest, group);
                const auto* prev = r.curve(pub[pub.size() - 2].version, group);
                if (cur && prev && cur->base > 0 && prev->base > 0 && !cur->days.empty() && !prev->days.empty()) {
                    const double a = cur->days[std::min<std::size_t>(cur->days.size() - 1, static_cast<std::size_t>(std::max(0, k)))] * 100.0 / cur->base;
                    const double p = prev->days[std::min<std::size_t>(prev->days.size() - 1, static_cast<std::size_t>(std::max(0, k)))] * 100.0 / prev->base;
                    const double diff = std::round(a) - std::round(p);
                    t.subStrong = (diff >= 0 ? "\xE2\x86\x97 " : "\xE2\x86\x98 ") + ch::fr::signedPoints(diff);
                    t.subTone = diff >= 0 ? 1 : 0;
                    t.sub = "contre la " + pub[pub.size() - 2].version + " \xC3\xA0 J+" + std::to_string(std::max(0, k));
                }
            }
            tiles.push_back(t);
            Tile v{ui::Icon::Module, "La version actuelle", r.latest.empty() ? kDash : r.latest, ageText(now, r.latestPublished), "", 0, ""};
            if (r.latestPublished > 0) v.sub = "publi\xC3\xA9" "e le " + ddmm(stats::dayOf(r.latestPublished));
            tiles.push_back(v);
            Tile d{ui::Icon::History, "D\xC3\xA9lai m\xC3\xA9" "dian avant la mise \xC3\xA0 jour", kDash, "", "", 0, ""};
            const stats::UpdateDelay* cur = nullptr;
            const stats::UpdateDelay* prev = nullptr;
            for (const auto& x : r.delays)
                if (x.software == group) {
                    if (x.version == r.latest) cur = &x;
                }
            if (pub.size() >= 2)
                for (const auto& x : r.delays)
                    if (x.software == group && x.version == pub[pub.size() - 2].version) prev = &x;
            if (cur) {
                d.value = daysText(cur->median);
                d.valueSmall = "en " + cur->version;
                if (prev) {
                    const double diff = std::round(cur->median / 86400.0) - std::round(prev->median / 86400.0);
                    d.subStrong = (diff <= 0 ? "\xE2\x86\x98 " : "\xE2\x86\x97 ") + (diff == 0 ? std::string("=") : ch::fr::signedInteger(diff) + "\xC2\xA0j");
                    d.subTone = diff <= 0 ? 1 : 0;
                    d.sub = "contre la " + prev->version + " (" + daysText(prev->median) + ")";
                }
            }
            tiles.push_back(d);
            int two = 0;
            for (const auto& p : r.behind) two += p.behind >= 2 ? 1 : 0;
            Tile b{ui::Icon::History, "PC pas \xC3\xA0 jour", std::to_string(r.behindTotal), "", "", 0, ""};
            if (two > 0) {
                b.subTone = -1;
                b.sub = std::to_string(two) + " ont 2 versions de retard ou plus";
            } else {
                b.sub = r.behindTotal ? "1 version de retard" : "tous \xC3\xA0 jour";
            }
            tiles.push_back(b);
            Tile a{ui::Icon::User, "Admins \xC3\xA0 jour", std::to_string(r.upToDateAdmins), "sur " + std::to_string(r.seenAdmins), "", 0, ""};
            for (const auto& p : r.behind)
                if (!p.admin.empty()) {
                    a.subTone = -1;
                    a.sub = adminName(p.admin) + " : Admin " + p.version;
                    break;
                }
            if (a.sub.empty()) a.sub = r.seenAdmins ? "tous \xC3\xA0 jour" : "aucun Admin vu";
            tiles.push_back(a);
        }
        tiles_->set(tiles);
        // ---- l'adoption ----
        {
            std::vector<const stats::AdoptionCurve*> curves;
            for (const auto& c : r.adoption)
                if (c.software == group) curves.push_back(&c);
            std::sort(curves.begin(), curves.end(), [](const auto* a, const auto* b) { return a->published < b->published; });
            // 4 versions au plus (la maquette) : les correctifs (1.45.2) d'abord laisses de cote, puis les plus anciennes
            for (auto it = curves.begin(); curves.size() > 4 && it != curves.end();) {
                const std::string& v = (*it)->version;
                const bool patch = v.size() > 2 && v.substr(v.rfind('.') + 1) != "0";
                it = patch ? curves.erase(it) : it + 1;
            }
            if (curves.size() > 4) curves.erase(curves.begin(), curves.end() - 4);
            std::size_t n = 0;
            for (const auto* c : curves) n = std::max(n, c->days.size());
            n = std::max<std::size_t>(n, 43);
            ch::TimeChartData t;
            for (std::size_t i = 0; i < n; ++i) {
                ch::Category c;
                c.label = i == 0 ? "J0" : "J+" + std::to_string(i);
                c.tip = i == 0 ? "Le jour de la publication" : std::to_string(i) + (i > 1 ? " jours" : " jour") + " apr\xC3\xA8s la publication";
                t.x.push_back(c);
            }
            for (std::size_t i = 0; i < n; i += 7) t.xTicks.push_back({static_cast<int>(i), i == 0 ? -1 : 0});
            for (std::size_t k = 0; k < curves.size(); ++k) {
                const auto* c = curves[k];
                ch::Series s;
                s.key = "version:" + c->version;
                s.name = c->version;
                s.color = ch::colors::ordinal(static_cast<int>(k), static_cast<int>(curves.size()));
                s.legendValue = "publi\xC3\xA9" "e le " + ddmm(stats::dayOf(c->published));
                const int base = std::max(1, c->base);
                for (std::size_t i = 0; i < n; ++i) s.values.push_back(i < c->days.size() ? c->days[i] * 100.0 / base : ch::kNone);
                s.endDot = true;
                const auto pcs = c->days;
                s.format = [base](double x) { return ch::fr::percent(x) + " " + ch::fr::kDot + " " + ch::fr::integer(x * base / 100.0) + " PC"; };
                if (k + 1 < curves.size() && !c->days.empty())
                    s.endLabel = c->version + " " + ch::fr::kDot + " " + ch::fr::percent(c->days.back() * 100.0 / base);
                t.series.push_back(s);
            }
            if (!curves.empty() && !curves.back()->days.empty()) {
                const auto* c = curves.back();
                const int last = static_cast<int>(c->days.size()) - 1;
                const double v = c->days.back() * 100.0 / std::max(1, c->base);
                t.notes.push_back({last, v, c->version + " " + ch::fr::kDot + " " + ch::fr::percent(v), ch::Note::Anchor::End, -12.f});
                if (const char* e = std::getenv("XPG_STATS_ESSAI"); e && std::string(e) == "1" && hooked()) t.exampleTip = last;
            }
            t.yTicks = {0, 25, 50, 75, 100};
            t.yMax = 100;
            t.yFormat = [](double v) { return ch::fr::percent(v); };
            t.legend = ch::LegendMode::Top;
            t.tipForward = false;
            t.clickHint = "Clic : les PC sur cette version (Activations)";
            t.emptyText = "Aucune version publi\xC3\xA9" "e dans les 400 derniers jours";
            adoption_->setData(t);
        }
        // ---- la part a jour ----
        {
            ch::TimeChartData t;
            t.x = axisOf(r.buckets, query_.step, true);
            t.xTicks = ticksOf(t.x.size());
            const auto pct = [](const std::vector<int>& a, const std::vector<int>& b) {
                std::vector<double> out;
                for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) out.push_back(b[i] > 0 ? a[i] * 100.0 / b[i] : ch::kNone);
                return out;
            };
            ch::Series s{"un", "Studios", pct(r.studiosUpToDate, r.studiosSeen)};
            ch::Series a{"deux", "Admins", pct(r.adminsUpToDate, r.adminsSeen)};
            a.step = true;
            a.width = 1.75f;
            const double sl = lastOf(s.values), al = lastOf(a.values);
            s.legendValue = stats::isNumber(sl) ? ch::fr::percent(sl) : kDash;
            a.legendValue = stats::isNumber(al) ? ch::fr::percent(al) : kDash;
            s.format = [](double v) { return ch::fr::percent(v); };
            const std::vector<int> au = r.adminsUpToDate, as = r.adminsSeen;
            a.format = [](double v) { return ch::fr::percent(v); };
            if (soft != "admins") t.series.push_back(s);
            if (soft != "studios") t.series.push_back(a);
            // les publications reperees
            for (const auto& v : publishedOldestFirst(r)) {
                const std::string day = stats::dayOf(v.published);
                for (std::size_t i = 0; i < r.buckets.size(); ++i)
                    if (r.buckets[i].from <= day && day <= r.buckets[i].to) t.markers.push_back({static_cast<double>(i), v.version, ch::kNone, {0, 0, 0, 0}, false});
            }
            t.yTicks = {0, 25, 50, 75, 100};
            t.yMax = 100;
            t.yFormat = [](double v) { return ch::fr::percent(v); };
            t.tipForward = true;
            t.clickHint = "Clic : les PC pas \xC3\xA0 jour (Activations)";
            share_->setData(t);
            sharePanel_->setSmall(std::string("sur la derni\xC3\xA8re version, chaque ") + stepLabel(query_.step));
        }
        // ---- les delais ----
        {
            ch::BarListData d;
            d.mode = ch::BarMode::Grouped;
            d.partKeys = {"un", "deux"};
            d.partNames = {"m\xC3\xA9" "dian", "moyen"};
            d.format = [](double v) { return ch::fr::decimal(v, v < 10 ? 1 : 0) + ch::fr::kNbsp + "j"; };
            d.axis = true;
            std::vector<const stats::UpdateDelay*> ds;
            for (const auto& x : r.delays)
                if (x.software == group) ds.push_back(&x);
            std::sort(ds.begin(), ds.end(), [](const auto* a, const auto* b) { return a->version < b->version; });
            for (const auto* x : ds) {
                ch::BarRow row;
                row.label = x->version;
                row.sub = x->version == r.latest ? "en cours" : "";
                row.parts = {x->median / 86400.0, x->mean / 86400.0};
                row.tip = x->version + " : m\xC3\xA9" "dian " + daysText(x->median) + ", moyen " + daysText(x->mean) + " (" + std::to_string(x->count) + " PC)";
                d.rows.push_back(row);
            }
            d.clickHint = "Clic : les PC de cette version";
            d.emptyText = "Aucun d\xC3\xA9lai mesur\xC3\xA9 sur la p\xC3\xA9riode";
            delays_->setData(d);
        }
        versions_->set(r, now);
        std::string seenNote = "Un PC compte pour la version qu'il a envoy\xC3\xA9" "e \xC3\xA0 sa derni\xC3\xA8re connexion ; " + std::to_string(r.seenStudios + r.seenAdmins) +
                               " PC vus";
        if (!r.seenSince.empty()) seenNote += " depuis le " + ddmm(r.seenSince);
        seenNote += " (" + std::to_string(r.seenStudios) + " Studios, " + std::to_string(r.seenAdmins) + " Admins).";
        versionsPanel_->setNote(seenNote, false);
        behind_->set(r, now, [this](const std::string& id) { return adminName(id); });
        invalidateLayout();
        invalidate();
    }

    void picked(const ch::Pick& p) {
        ListFilter f;
        f.page = "activations";
        std::string version;
        if (p.series >= 0 && static_cast<std::size_t>(p.series) < adoption_->data().series.size() && adoption_->activeIndex() >= 0) version = adoption_->data().series[static_cast<std::size_t>(p.series)].name;
        if (version.empty() && p.index >= 0 && static_cast<std::size_t>(p.index) < delays_->data().rows.size() && delays_->focused()) version = delays_->data().rows[static_cast<std::size_t>(p.index)].label;
        if (!version.empty()) {
            f.search = version;
            f.what = "Activations \xC2\xB7 version " + version;
        } else {
            f.what = "Activations : les PC pas \xC3\xA0 jour ont une version plus ancienne que " + reply_.latest;
        }
        const std::string said = openList(host_, *this, f, host_.workspace().now());
        if (host_.flash && !said.empty()) host_.flash(said, Tone::Info);
    }

    ShellHost                     host_;
    std::function<Choice()>       getChoice_;
    std::function<std::string()>  getSoftware_;
    Tiles*                        tiles_{nullptr};
    Panel*                        adoptionPanel_{nullptr};
    Panel*                        sharePanel_{nullptr};
    Panel*                        delayPanel_{nullptr};
    Panel*                        versionsPanel_{nullptr};
    Panel*                        behindPanel_{nullptr};
    ch::TimeChart*                adoption_{nullptr};
    ch::TimeChart*                share_{nullptr};
    ch::BarList*                  delays_{nullptr};
    VersionsTable*                versions_{nullptr};
    BehindList*                   behind_{nullptr};
    ui::Button*                   deploy_{nullptr};
    stats::UpdatesReply           reply_;
    stats::AdminsReply            admins_;
    stats::UpdatesQuery           query_;
    bool                          has_{false}, loading_{false}, again_{false}, tooOld_{false};
    std::string                   note_, problem_;
    Rect                          bannerRect_{};
    std::shared_ptr<bool>         alive_{std::make_shared<bool>(true)};
    core::ConnectionScope         links_;
};

std::unique_ptr<ui::Widget> makeUpdatesView(ShellHost host, std::function<Choice()> getChoice, std::function<std::string()> getSoftware) {
    return std::make_unique<UpdatesView>(std::move(host), std::move(getChoice), std::move(getSoftware));
}

} // namespace license_admin::gui::statskit
