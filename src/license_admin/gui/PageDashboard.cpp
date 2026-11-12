// =============================================================================
//  license_admin/gui/PageDashboard.cpp - 1.33.0 : LE TABLEAU DE BORD
// -----------------------------------------------------------------------------
//  Ce que disent l'archive et le registre des activations de ce poste, hors
//  ligne (Workspace::dashboard) :
//    - les tuiles : clients, licences actives (et expirees), licences en ligne,
//      revoquees, PC actives (lies et en ligne), demandes en attente, celles qui
//      finissent dans 30 jours, jetons flottants en cours ; 1.35.0 : ce qui vient
//      du service est le DERNIER RELEVE (sa date est dite ; jamais releve : pas
//      de chiffre) ; un clic sur une tuile ouvre la page qui en dit plus ;
//    - les dernieres operations (signees, demandes, revocations) ;
//    - la repartition des licences actives par edition (des barres) ;
//    - l'etat du poste : la cle, le registre, la liste de revocation, le service ;
//    - les actions rapides : Nouvelle licence... (l'assistant), Noter des
//      demandes... (des .xpgreq recus, pas encore signes), Service en ligne (la
//      page du service : l'archive, le certificat) ;
//    - le premier lancement (aucune cle) : comment commencer ;
//    - 1.45.0 : le service en ligne plus ancien que ce qu'il faut a l'Admin (ou que
//      le kit qu'il porte) : un bandeau, avec << Deployer le service >>.
//  Des demandes (.xpgreq) glissees sur la page ouvrent l'assistant avec elles.
// =============================================================================
#include "Page.hpp"
#include "StatsKit.hpp"   // 1.47.0 : la rangee de courbes et la petite carte du globe

#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace licdlg {
// LicenceWizard.cpp : l'assistant ouvert prend des demandes glissees (faux : aucun assistant ouvert).
bool wizardTakeDrop(const std::vector<std::string>& paths);
} // namespace licdlg

namespace {

using gfx::Color;
using gfx::FontId;
using gfx::Rect;

constexpr FontId kF28{28}, kF26{26}, kF16{16}, kF15{15}, kF14{14}, kF13{13}, kF12{12};

std::string fit(const gfx::IRenderer& r, const std::string& s, FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();   // 1.44.0 : un caractere entier
        if (!t.empty()) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

// Un chemin trop long : on garde la fin (le nom du dossier), pas le debut.
std::string fitTail(const gfx::IRenderer& r, const std::string& s, FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::size_t from = 0;
    while (from < s.size() && r.measure("\xE2\x80\xA6" + s.substr(from), f).width > w) {
        ++from;
        while (from < s.size() && (static_cast<unsigned char>(s[from]) & 0xC0) == 0x80) ++from;
    }
    return "\xE2\x80\xA6" + s.substr(from);
}

// 1.47.0 : un texte en morceaux (<< a \xC2\xB7 b \xC2\xB7 c >>) qui ne tient pas : les derniers morceaux s'en vont d'abord, entiers.
std::string fitParts(const gfx::IRenderer& r, std::string s, FontId f, float w) {
    for (std::size_t at = s.rfind(" \xC2\xB7 "); r.measure(s, f).width > w && at != std::string::npos; at = s.rfind(" \xC2\xB7 ")) s.erase(at);
    return fit(r, s, f, w);
}

std::vector<std::string> wrap(const std::string& text, FontId f, float w) {
    std::vector<std::string> out;
    std::string line;
    std::size_t i = 0;
    while (i <= text.size()) {
        const std::size_t sp = text.find(' ', i);
        const std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(tryLine, f) > w) {
            out.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        if (sp == std::string::npos) break;
        i = sp + 1;
    }
    if (!line.empty() || out.empty()) out.push_back(line);
    return out;
}

Color toneColor(const ui::Theme& th, Tone t) {
    switch (t) {
        case Tone::Ok: return th.color.ok;
        case Tone::Warning: return th.color.warning;
        case Tone::Error: return th.color.error;
        case Tone::Info: break;
    }
    return th.color.text;
}

bool isRequest(const std::string& path) {
    std::string ext = std::filesystem::path(std::u8string(path.begin(), path.end())).extension().string();
    for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ext == ".xpgreq";
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// 1.34.0 : "06/11 14:05" (la date d'un releve, courte : une tuile).
std::string shortWhen(Seconds t) {
    const std::string full = dateTimeFr(t);   // "06/11/2026 14:05"
    return full.size() >= 16 ? full.substr(0, 5) + full.substr(10) : full;
}

// 1.41.0 : LA CARTE de la maquette - blanche, coins de 12, un trait, une ombre a peine.
void cardBox(gfx::IRenderer& r, const ui::Theme& th, const Rect& box, Color fill, Color border) {
    if (box.empty()) return;
    r.fillRoundedRect({box.x, box.y + 1.f, box.w, box.h + 1.f}, th.brand.cardShadow, 12.f);
    r.fillRoundedRect(box, border, 12.f);
    r.fillRoundedRect(box.inset(1.f, 1.f), fill, 11.f);
}
void cardBox(gfx::IRenderer& r, const ui::Theme& th, const Rect& box) { cardBox(r, th, box, th.brand.card, th.brand.cardBorder); }

// "il y a 9 min", "il y a 2 h", "hier", ou la date.
std::string agoFr(Seconds now, Seconds at) {
    if (at <= 0) return {};
    const long long d = static_cast<long long>(now) - static_cast<long long>(at);
    if (d < 60) return "\xC3\xA0 l'instant";
    if (d < 3600) return "il y a " + std::to_string(d / 60) + " min";
    if (d < 86400) return "il y a " + std::to_string(d / 3600) + " h";
    if (d < 2 * 86400) return "hier";
    return dateFr(at);
}

// Le jour en toutes lettres : "dimanche 8 novembre".
std::string longDateFr(Seconds t) {
    static const char* const kDays[] = {"Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};
    static const char* const kMonths[] = {"janvier", "f\xC3\xA9vrier", "mars", "avril", "mai", "juin", "juillet", "ao\xC3\xBBt", "septembre", "octobre", "novembre", "d\xC3\xA9" "cembre"};
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    return std::string(kDays[tm.tm_wday % 7]) + " " + std::to_string(tm.tm_mday) + " " + kMonths[tm.tm_mon % 12];
}

// ---- une tuile : un chiffre, ce qu'il compte ; un clic ouvre la page qui en dit plus ----
// 1.41.0 : deux tailles - les quatre grandes de la maquette (l'icone dans son carre teinte, le grand chiffre, une
// ligne, une barre pour les PC) et quatre petites dessous (le libelle, le chiffre a droite).
class StatTile final : public ui::Button {
public:
    StatTile(std::string id, std::string label, ui::Icon icon, bool big) : ui::Button(label, std::move(id)), label_(std::move(label)), icon_(icon), big_(big) {}
    void set(std::string value, std::string sub, Tone tone, bool dim, float progress = -1.f, std::string more = {}) {
        value_ = std::move(value);
        sub_ = std::move(sub);
        tone_ = tone;
        dim_ = dim;
        progress_ = progress;
        more_ = std::move(more);
        invalidate();
    }
    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    // 1.46.0 : le libelle change (la tuile des demandes devient << Commandes a livrer >> quand il y en a).
    void setLabel(std::string label) {
        if (label == label_) return;
        label_ = std::move(label);
        setText(label_);
        invalidate();
    }
    [[nodiscard]] const std::string& label() const noexcept { return label_; }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Rect  r = bounds();
        const bool  marked = tone_ != Tone::Info && !dim_;
        const Color ink = dim_ ? c.textDisabled : marked ? toneColor(th, tone_) : c.accent;
        // Une demande a traiter : la carte ambree (la maquette) ; survolee : le trait de l'accent.
        const Color fill = marked && tone_ == Tone::Warning && big_ ? Color::rgb(0xFFFBF2) : th.brand.card;
        const Color line = hovered() ? c.accent.withAlpha(170) : marked && tone_ == Tone::Warning && big_ ? Color::rgb(0xF6DFB5) : th.brand.cardBorder;
        cardBox(ctx.r, th, r, fill, line);
        const float x = r.x + 18.f;
        if (!big_) {
            ui::drawIcon(ctx.r, icon_, {x, r.y + (r.h - 16.f) * 0.5f, 16.f, 16.f}, dim_ ? c.textDisabled : c.textMuted);
            const FontId vf = gfx::semibold(FontId{20});
            const float vw = value_.empty() ? 0.f : ctx.r.measure(value_, vf).width;
            // Le libelle, et dessous ce que compte le chiffre (ou pourquoi il n'y en a pas) ; le chiffre a droite.
            const bool two = !sub_.empty() && r.h >= 44.f;
            const float ly = two ? r.y + r.h * 0.5f - ctx.r.lineHeight(kF14) + 1.f : r.y + (r.h - ctx.r.lineHeight(kF14)) * 0.5f;
            // 1.47.0 : etroite, le libelle en plus petit plutot que raccourci
            const FontId lf = ctx.r.measure(label_, kF14).width > r.w - 64.f - vw ? FontId{12} : kF14;
            ctx.r.drawText({x + 26.f, ly}, fit(ctx.r, label_, lf, r.w - 64.f - vw), lf, dim_ ? c.textDisabled : c.text);
            if (two) ctx.r.drawText({x + 26.f, r.y + r.h * 0.5f + 2.f}, fitParts(ctx.r, sub_, kF12, r.w - 64.f - vw), kF12, value_.empty() ? c.textDisabled : c.textMuted);
            if (!value_.empty())
                ctx.r.drawText({r.right() - 18.f - vw, r.y + (r.h - ctx.r.lineHeight(vf)) * 0.5f}, value_, vf, marked ? ink : c.text);
            return;
        }
        // L'icone dans son carre teinte, le libelle a cote.
        const Rect ic{x, r.y + 18.f, 30.f, 30.f};
        ctx.r.fillRoundedRect(ic, ink.withAlpha(dim_ ? 18 : 26), 9.f);
        ui::drawIcon(ctx.r, icon_, {ic.x + 7.f, ic.y + 7.f, 16.f, 16.f}, ink);
        // 1.47.0 : etroite, le libelle sur deux lignes (plus petit) a cote de l'icone plutot que raccourci
        if (ctx.r.measure(label_, kF14).width <= r.w - 76.f) {
            ctx.r.drawText({ic.right() + 10.f, ic.y + (ic.h - ctx.r.lineHeight(kF14)) * 0.5f}, label_, kF14, c.textMuted);
        } else {
            const auto ls = wrap(label_, FontId{12}, r.w - 76.f);
            const float lh12 = ctx.r.lineHeight(FontId{12});
            float ly = ic.y + (ic.h - lh12 * static_cast<float>(std::min<std::size_t>(2, ls.size()))) * 0.5f;
            for (std::size_t i = 0; i < ls.size() && i < 2; ++i, ly += lh12)
                ctx.r.drawText({ic.right() + 10.f, ly}, fit(ctx.r, i == 1 && ls.size() > 2 ? ls[1] + " " + ls[2] : ls[i], FontId{12}, r.w - 76.f), FontId{12},
                               c.textMuted);
        }
        const float vy = ic.bottom() + 12.f;
        if (value_.empty()) {   // pas de chiffre (le service en ligne) : la phrase
            float ly = vy + 4.f;
            for (const auto& l : wrap(sub_, kF13, r.w - 36.f)) {
                if (ly + 14.f > r.bottom() - 8.f) break;
                ctx.r.drawText({x, ly}, l, kF13, c.textDisabled);
                ly += 17.f;
            }
            return;
        }
        const FontId vf = gfx::semibold(r.h < 100.f ? FontId{24} : r.h < 110.f ? FontId{28} : FontId{34});
        ctx.r.drawText({x, vy}, value_, vf, marked ? ink : c.text);
        const float vw = ctx.r.measure(value_, vf).width;
        const float lh = ctx.r.lineHeight(vf);
        // 1.47.0 : pas de place sous le chiffre (une fenetre basse) : la phrase a sa droite, jamais par-dessus lui.
        if (progress_ < 0.f && more_.empty() && vy + lh + 6.f + ctx.r.lineHeight(kF13) > r.bottom() - 6.f) {
            ctx.r.drawText({x + vw + 10.f, vy + lh - ctx.r.lineHeight(kF13) - 4.f}, fitParts(ctx.r, sub_, kF13, r.w - 36.f - vw - 10.f), kF13, c.textMuted);
            return;
        }
        if (!more_.empty()) {
            const FontId fm = gfx::semibold(kF14);
            ctx.r.drawText({x + vw + 8.f, vy + lh - ctx.r.lineHeight(fm) - 4.f}, fit(ctx.r, more_, fm, r.w - 36.f - vw), fm, c.textDisabled);
        }
        const float sy = std::min(r.bottom() - 26.f, vy + lh + 6.f);
        if (progress_ >= 0.f) {   // les PC : la barre des places prises
            const Rect track{x, sy + 6.f, r.w - 36.f, 6.f};
            ctx.r.fillRoundedRect(track, c.gridLine, 3.f);
            ctx.r.fillRoundedRect({track.x, track.y, std::max(6.f, track.w * std::clamp(progress_, 0.f, 1.f)), track.h}, c.ok, 3.f);
        } else {
            ctx.r.drawText({x, sy}, fitParts(ctx.r, sub_, kF13, r.w - 36.f), kF13, c.textMuted);
        }
    }

private:
    std::string label_, value_, sub_, more_;
    ui::Icon    icon_{ui::Icon::None};
    bool        big_{false};
    Tone        tone_{Tone::Info};
    bool        dim_{false};
    float       progress_{-1.f};
};

// ---- 1.41.0 : L'ACTIVITE - les dernieres operations en fil (la maquette) : l'icone de ce qui s'est passe dans son
// carre teinte, qui et quoi, la licence et le PC dessous, quand a droite. Un clic : l'historique.
struct FeedRow {
    ui::Icon    icon{ui::Icon::Info};
    Tone        tone{Tone::Info};
    std::string who, what, detail, when, tip;
};

class ActivityFeed final : public ui::Widget {
public:
    ActivityFeed() : ui::Widget("tableau.operations") {}
    void setRows(std::vector<FeedRow> rows) {
        rows_ = std::move(rows);
        invalidate();
    }
    const core::SignalPtr<int> activated = core::Signal<int>::create();

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Rect  b = bounds();
        ctx.r.pushClip(b);
        float y = b.y;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (y + kRowH > b.bottom() + 1.f) break;
            const FeedRow& f = rows_[i];
            const Rect row{b.x, y, b.w, kRowH};
            if (static_cast<int>(i) == hover_) ctx.r.fillRoundedRect(row.inset(6.f, 3.f), th.brand.hover, 8.f);
            const Color ink = f.tone == Tone::Info ? c.accent : toneColor(th, f.tone);
            const Rect  ic{row.x + 18.f, row.y + 13.f, 32.f, 32.f};
            ctx.r.fillRoundedRect(ic, ink.withAlpha(24), 9.f);
            ui::drawIcon(ctx.r, f.icon, {ic.x + 8.f, ic.y + 8.f, 16.f, 16.f}, ink);
            const float tx = ic.right() + 14.f;
            const float ww = ctx.r.measure(f.when, kF12).width;
            const float room = row.right() - 18.f - ww - 12.f - tx;
            const FontId fb = gfx::semibold(kF14);
            const std::string who = fit(ctx.r, f.who, fb, room * 0.6f);
            ctx.r.drawText({tx, row.y + 12.f}, who, fb, c.text);
            const float wx = tx + ctx.r.measure(who, fb).width + (who.empty() ? 0.f : 6.f);
            ctx.r.drawText({wx, row.y + 12.f}, fit(ctx.r, f.what, kF14, tx + room - wx), kF14, c.text);
            ctx.r.drawText({tx, row.y + 33.f}, fit(ctx.r, f.detail, kF13, row.right() - 18.f - tx), kF13, c.textMuted);
            ctx.r.drawText({row.right() - 18.f - ww, row.y + 14.f}, f.when, kF12, c.textDisabled);
            if (i + 1 < rows_.size()) ctx.r.fillRect({tx, row.bottom() - 1.f, row.right() - 18.f - tx, 1.f}, c.gridLine);
            y += kRowH;
        }
        ctx.r.popClip();
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            const int h = rowAt(m->pos);
            if (h != hover_) {
                hover_ = h;
                setTooltip(h >= 0 ? rows_[static_cast<std::size_t>(h)].tip : std::string());
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (d->button == ui::MouseButton::Left && d->clickCount >= 2 && rowAt(d->pos) >= 0) {
                activated->emit(rowAt(d->pos));
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    static constexpr float kRowH = 58.f;
    [[nodiscard]] int rowAt(gfx::Point p) const {
        const Rect b = bounds();
        if (!b.contains(p)) return -1;
        const int i = static_cast<int>((p.y - b.y) / kRowH);
        return i >= 0 && static_cast<std::size_t>(i) < rows_.size() ? i : -1;
    }
    std::vector<FeedRow> rows_;
    int                  hover_{-1};
};

struct PosteLine {
    std::string label, value;
    Tone        tone{Tone::Info};
};

class DashboardPage final : public Page {
public:
    explicit DashboardPage(ShellHost host) : Page("page.tableau", std::move(host)) {
        // 1.41.0 : << Nouvelle licence >> est en haut de la fenetre (l'action principale de l'Admin) ; celui-ci reste
        // pour les essais et les raccourcis, cache.
        newLicence_ = addButton("Nouvelle licence\xE2\x80\xA6", "tableau.nouvelle-licence", ui::Button::Style::Primary, ui::Icon::Plus);
        newLicence_->setVisibility(ui::Visibility::Collapsed);
        noteRequests_ = addButton("Noter des demandes\xE2\x80\xA6", "tableau.noter-demandes", ui::Button::Style::Default, ui::Icon::Import);
        sync_ = addButton("Service en ligne", "tableau.synchroniser", ui::Button::Style::Default, ui::Icon::Globe);
        newLicence_->setTooltip("L'assistant : le client, l'\xC3\xA9" "dition, les PC (leurs demandes .xpgreq), la dur\xC3\xA9" "e, puis signer.");
        noteRequests_->setTooltip("Des demandes d'activation re\xC3\xA7ues (.xpgreq), pas encore sign\xC3\xA9" "es : not\xC3\xA9" "es dans le registre des activations.");
        sync_->setTooltip("La page Service en ligne : l'archive, la cl\xC3\xA9 du service et son certificat.");
        links_ += newLicence_->clicked->connect([this] { openWizard({}); });
        links_ += noteRequests_->clicked->connect([this] { askRequests(); });
        links_ += sync_->clicked->connect([this] { host_.navigate("service"); });

        // 1.49.0 : les deux seuls gestes de l'Admin+ sans la cle de l'editeur (docs/ADMINS.md, par. 12)
        createKey_ = addButton("Cr\xC3\xA9" "er la cl\xC3\xA9 de l'\xC3\xA9" "diteur\xE2\x80\xA6", "tableau.creer-cle", ui::Button::Style::Primary, ui::Icon::Lock);
        restore_ = addButton("Restaurer la cl\xC3\xA9 de l'\xC3\xA9" "diteur\xE2\x80\xA6", "tableau.restaurer", ui::Button::Style::Default, ui::Icon::Open);
        toEditions_ = addButton("Aller aux \xC3\xA9" "ditions", "tableau.aller-editions", ui::Button::Style::Primary, ui::Icon::GoTo);
        links_ += createKey_->clicked->connect([this] { host_.navigate("securite"); });
        links_ += restore_->clicked->connect([this] { host_.navigate("securite"); });
        links_ += toEditions_->clicked->connect([this] { host_.navigate("editions"); });
        // 1.44.0 : le second chemin d'un poste sans cle (devenir admin), et ce qu'attend un poste en attente.
        becomeAdmin_ = addButton("Demander \xC3\xA0 devenir admin\xE2\x80\xA6", "tableau.devenir-admin", ui::Button::Style::Default, ui::Icon::User);
        becomeAdmin_->setTooltip("L'admin+ t'a fait installer l'Admin : ton nom, ton courriel ; ta cl\xC3\xA9 na\xC3\xAEt sur ce PC, l'admin+ l'accepte.");
        myFingerprint_ = addButton("Voir mon empreinte", "tableau.voir-empreinte", ui::Button::Style::Primary, ui::Icon::Lock);
        toService_ = addButton("Recevoir l'archive", "tableau.aller-service", ui::Button::Style::Primary, ui::Icon::Globe);
        links_ += becomeAdmin_->clicked->connect([this] { host_.navigate("admins"); });
        links_ += myFingerprint_->clicked->connect([this] { host_.navigate("admins"); });
        links_ += toService_->clicked->connect([this] { host_.navigate("service"); });
        // 1.45.0 : le service en ligne a mettre a jour - le deployer d'ici (le dialogue de Service en ligne).
        deploy_ = addButton("D\xC3\xA9ployer le service", "tableau.deployer", ui::Button::Style::Primary, ui::Icon::ArrowUp);
        deploy_->setTooltip("Lance le kit install\xC3\xA9 avec l'Admin : ton service part \xC3\xA0 jour sur ton compte Cloudflare (la sortie s'affiche en direct).");
        deploy_->setVisibility(ui::Visibility::Collapsed);
        links_ += deploy_->clicked->connect([this] {
            if (host_.busy && host_.busy()) {
                host_.report(OpResult::fail("un travail est en cours : attends qu'il finisse, puis D\xC3\xA9ployer le service"));
                return;
            }
            if (deployer::running()) return;
            std::weak_ptr<bool> alive = alive_;
            if (host_.openDialog) host_.openDialog(makeDeployDialog(host_, [this, alive] {
                if (!alive.expired()) refresh();
            }));
        });
        // 1.49.0 : le bandeau du premier lancement de l'Admin+ 1.49 (une fois) - << Repartir de zero >> (ResetDialog.cpp).
        offerOpen_ = addButton("Voir ce qui serait effac\xC3\xA9\xE2\x80\xA6", "tableau.repartir", ui::Button::Style::Primary, ui::Icon::Lock);
        offerClose_ = addButton("Non merci", "tableau.repartir.fermer", ui::Button::Style::Default, ui::Icon::None);
        offerOpen_->setVisibility(ui::Visibility::Collapsed);
        offerClose_->setVisibility(ui::Visibility::Collapsed);
        links_ += offerOpen_->clicked->connect([this] {
            if (!host_.openDialog) return;
            if (auto d = makeStartOverDialog(host_)) host_.openDialog(std::move(d));
        });
        links_ += offerClose_->clicked->connect([this] {
            offer_ = false;
            refresh();
            invalidateLayout();
        });

        const struct {
            const char* id;
            const char* label;
            const char* page;
            ui::Icon    icon;
            bool        big;
        } tiles[] = {
            {"actives", "Licences actives", "licences", ui::Icon::Document, true},
            {"pc", "PC activ\xC3\xA9s", "activations", ui::Icon::Station, true},
            {"demandes", "Demandes en attente", "activations", ui::Icon::Import, true},
            {"bientot", "Finissent dans 30 jours", "licences", ui::Icon::History, true},
            {"clients", "Clients", "clients", ui::Icon::User, false},
            {"en-ligne", "Licences en ligne", "licences", ui::Icon::Globe, false},
            {"revoquees", "R\xC3\xA9voqu\xC3\xA9" "es", "revocations", ui::Icon::Close, false},
            {"flottantes", "Licences flottantes", "flottantes", ui::Icon::Network, false},
        };
        for (const auto& t : tiles) {
            auto tile = std::make_unique<StatTile>(std::string("tableau.tuile.") + t.id, t.label, t.icon, t.big);
            const std::string page = t.page;
            const bool requests = std::string(t.id) == "demandes";
            links_ += tile->clicked->connect([this, page, requests] {
                // 1.46.0 : des commandes payees a livrer : la tuile les montre, son clic ouvre Commandes.
                if (requests && ws().ordersToDeliver() > 0) {
                    host_.navigate("commandes");
                    return;
                }
                // Le service regle, les demandes en attente sont d'abord celles en ligne.
                host_.navigate(requests && ws().service().ready() ? std::string("demandes") : page);
            });
            tiles_.push_back(&static_cast<StatTile&>(addChild(std::move(tile))));
        }
        tiles_.back()->setTooltip("Les jetons flottants en cours, au dernier relev\xC3\xA9 du service (Licences flottantes \xE2\x80\xBA Actualiser).");

        feed_ = &static_cast<ActivityFeed&>(addChild(std::make_unique<ActivityFeed>()));
        // 1.47.0 : le resume des statistiques (4 petites courbes sur 30 jours : un clic, la page Statistiques) et la place de la
        // petite carte du globe (planche 11 : PageStatsGlobe.cpp) - montres quand le service donne ses chiffres.
        strip_ = &static_cast<statskit::DashboardStrip&>(addChild(statskit::makeDashboardStrip(host_)));
        globe_ = &addChild(statskit::makeDashboardGlobe(host_));
        links_ += feed_->activated->connect([this](int) { host_.navigate("historique"); });
        refresh();
    }

    [[nodiscard]] std::string title() const override { return "Tableau de bord"; }

    void refresh() override {
        const Workspace& w = ws();
        d_ = w.dashboard();
        // 1.49.0 : la proposition de repartir de zero, UNE fois (le premier lancement de l'Admin+ 1.49) : notee des qu'elle est montree.
        if (!offerDecided_ && w.role() == Role::Plus) {
            offerDecided_ = true;
            offer_ = w.startOverOfferDue();
            if (offer_) w.noteStartOverOffered();
        }
        if (offer_ && w.role() != Role::Plus) offer_ = false;
        offerOpen_->setVisibility(offer_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        offerClose_->setVisibility(offer_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        const auto& k = w.key();
        hint_.clear();
        hintKind_ = 0;
        const Role role = w.role();
        if (role == Role::None) {
            // 1.44.0 : deux chemins - le premier PC (la cle de l'editeur, l'admin+) ou un admin que l'admin+ accepte.
            // 1.49.0 (docs/ADMINS.md, par. 12) : XPG Admin n'a que le chemin de l'admin (l'invitation) ; l'Admin+ n'a que la cle
            // de l'editeur (la creer, la restaurer), jamais << Demander a devenir admin >>.
            hintKind_ = 1;
            hint_ = license_admin::classique()
                        ? std::string("Pas encore de cl\xC3\xA9 d'admin sur ce poste : l'admin+ t'a invit\xC3\xA9 ? Ta demande part avec ton code d'invitation "
                                      "(Administrateurs), puis l'admin+ l'accepte. Sans cl\xC3\xA9, rien ne se signe.")
                        : std::string("Ce programme est celui de l'admin+. Un admin installe XPG Admin, re\xC3\xA7u avec son invitation. Pour commencer : "
                                      "cr\xC3\xA9" "e la cl\xC3\xA9 de l'\xC3\xA9" "diteur (le premier PC), ou restaure sa sauvegarde. Sans cl\xC3\xA9, rien ne se signe.");
        } else if (role == Role::Pending) {
            hintKind_ = 3;
            hint_ = "Ta demande pour devenir admin attend l'admin+ : il va t'appeler, lis-lui ton empreinte. En attendant, l'Admin ne peut rien signer.";
        } else if (role == Role::Retired) {
            hintKind_ = 4;
            hint_ = w.whyNot("licence_creer");
        } else if (!w.hasRegistry()) {
            hintKind_ = role == Role::Admin ? 5 : 2;
            hint_ = role == Role::Admin ? "Il manque le registre (les offres) : il arrive avec l'archive de l'admin+ (Service en ligne \xE2\x80\xBA Recevoir l'archive)."
                                        : "Il manque le registre (les \xC3\xA9" "ditions et les fonctionnalit\xC3\xA9s) : signe-le dans \xC3\x89" "ditions avant la premi\xC3\xA8re licence.";
        }
        const bool classic = license_admin::classique();   // 1.49.0
        createKey_->setVisibility(hintKind_ == 1 && !classic ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        restore_->setVisibility(hintKind_ == 1 && !classic ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        becomeAdmin_->setVisibility(hintKind_ == 1 && classic ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        myFingerprint_->setVisibility(hintKind_ == 3 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        toService_->setVisibility(hintKind_ == 5 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        toEditions_->setVisibility(hintKind_ == 2 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        // 1.45.0 : le service en ligne a mettre a jour (l'etat lu au lancement, ou par Verifier)
        {
            const deployer::VersionCheck ver = serviceVersion(w);
            serviceNote_ = ver.outdated ? deployer::outdatedMessage(ver) : std::string();
            deploy_->setVisibility(serviceNote_.empty() ? ui::Visibility::Collapsed : ui::Visibility::Visible);
            deploy_->setEnabled(!deployer::running());
        }
        // 1.44.0 : le droit << licence_creer >> (un admin qui ne l'a pas : le bouton dit pourquoi).
        newLicence_->setEnabled(w.can("licence_creer") && w.hasRegistry());
        if (!w.can("licence_creer") && role != Role::None) newLicence_->setTooltip(w.whyNot("licence_creer"));

        const auto n = [](int v) { return std::to_string(v); };
        // Les places des licences actives liees (leurs PC au plus) : la barre de la tuile des PC.
        int places = 0;
        for (const auto& l : w.licences())
            if (!l.revoked && !l.expired && !Workspace::isOnlineLicence(l.licence)) places += std::max(0, l.licence.maxDevices);
        tiles_[0]->set(n(d_.activeLicences), "sur " + n(d_.licences) + (d_.expiredLicences ? ", " + n(d_.expiredLicences) + " expir\xC3\xA9" "e(s)" : std::string(" au total")),
                       d_.expiredLicences ? Tone::Warning : Tone::Info, false);
        if (d_.serviceConfigured && d_.snapshotAt)
            tiles_[1]->set(n(d_.devices + d_.onlineDevices), n(d_.devices) + " li\xC3\xA9s, " + n(d_.onlineDevices) + " en ligne", Tone::Info, false,
                           places > 0 ? static_cast<float>(d_.devices) / static_cast<float>(places) : -1.f,
                           places > 0 ? "/ " + n(places + d_.onlineDevices) + " places" : std::string());
        else
            tiles_[1]->set(n(d_.devices), "li\xC3\xA9s \xC3\xA0 une licence", Tone::Info, false,
                           places > 0 ? static_cast<float>(d_.devices) / static_cast<float>(places) : -1.f, places > 0 ? "/ " + n(places) + " places" : std::string());
        if (d_.serviceConfigured) {
            // 1.34.0 : les demandes par fichier (notees) et celles en ligne (au dernier releve).
            const int all = d_.pendingRequests + d_.onlinePending;
            const std::string when = d_.onlineFetchedAt ? "relev\xC3\xA9" "es le " + dateTimeFr(d_.onlineFetchedAt) : std::string("en ligne : jamais relev\xC3\xA9" "es");
            tiles_[2]->set(n(all), d_.onlineFetchedAt ? "dont " + n(d_.onlinePending) + " en ligne \xC2\xB7 relev\xC3\xA9 " + shortWhen(d_.onlineFetchedAt) : when,
                           all ? Tone::Warning : Tone::Info, false);
            tiles_[2]->setTooltip("Les demandes en attente de ta d\xC3\xA9" "cision : " + n(d_.pendingRequests) + " not\xC3\xA9" "e(s) par fichier, " + n(d_.onlinePending)
                                  + " en ligne (" + when + "). Clic : Commandes et demandes.");
            // 1.46.0 : des commandes de la boutique payees, a livrer (le regard : GET /admin/etat) - la tuile devient la leur :
            // leur nombre en grand, les demandes en attente dessous (jamais un 0 ambre).
            if (const int toDeliver = w.ordersToDeliver(); toDeliver > 0) {
                tiles_[2]->setLabel("Commandes \xC3\xA0 livrer");
                tiles_[2]->set(n(toDeliver), std::string(toDeliver > 1 ? "pay\xC3\xA9" "es dans la boutique" : "pay\xC3\xA9" "e dans la boutique") +
                                                 (all ? " \xC2\xB7 " + n(all) + (all > 1 ? " demandes en attente" : " demande en attente") : std::string()),
                               Tone::Warning, false);
                tiles_[2]->setTooltip(n(toDeliver) + " commande(s) de la boutique pay\xC3\xA9" "e(s), \xC3\xA0 livrer (Commandes et demandes \xE2\x80\xBA D\xC3\xA9livrer maintenant, ou "
                                      "d'elles-m\xC3\xAAmes si \xC2\xAB Livrer seul \xC2\xBB est coch\xC3\xA9). " + n(all) + " demande(s) en attente de ta d\xC3\xA9" "cision. Clic : Commandes et demandes.");
            } else {
                tiles_[2]->setLabel("Demandes en attente");
            }
        } else {
            tiles_[2]->setLabel("Demandes en attente");
            tiles_[2]->set(n(d_.pendingRequests), "pas encore sign\xC3\xA9" "es", d_.pendingRequests ? Tone::Warning : Tone::Info, false);
            tiles_[2]->setTooltip({});
        }
        tiles_[3]->set(n(d_.expiringSoon), d_.expiringSoon ? std::string("licences \xC3\xA0 prolonger") : std::string("licences actives"),
                       d_.expiringSoon ? Tone::Error : Tone::Info, false);
        tiles_[4]->set(n(d_.clients), "dans l'archive", Tone::Info, false);
        const std::string seen = d_.snapshotAt ? "relev\xC3\xA9 " + shortWhen(d_.snapshotAt) : std::string();
        if (!d_.serviceConfigured) tiles_[5]->set("", "service non r\xC3\xA9gl\xC3\xA9", Tone::Info, true);
        else if (!d_.snapshotAt) tiles_[5]->set("", "jamais relev\xC3\xA9" "es", Tone::Info, true);
        else tiles_[5]->set(n(d_.onlineLicences), seen, Tone::Info, false);
        tiles_[5]->setTooltip(d_.snapshotAt ? "Les licences en ligne, " + seen + "." : std::string());
        tiles_[6]->set(n(d_.revokedLicences), w.hasRevocations() ? "liste n\xC2\xB0 " + std::to_string(w.revocations().number) : std::string("aucune liste"),
                       d_.revokedLicences ? Tone::Error : Tone::Info, false);
        if (!d_.serviceConfigured) tiles_[7]->set("", "service non r\xC3\xA9gl\xC3\xA9", Tone::Info, true);
        else if (!d_.snapshotAt) tiles_[7]->set("", "jamais relev\xC3\xA9" "es", Tone::Info, true);
        else
            tiles_[7]->set(n(d_.floatingLeases) + " / " + n(d_.floatingSeats), "jetons en cours", d_.floatingSeats && d_.floatingLeases >= d_.floatingSeats ? Tone::Warning : Tone::Info, false);

        rebuildFeed();

        // L'etat du poste.
        poste_.clear();
        if (k.present)
            poste_.push_back({"Cl\xC3\xA9", k.id + (k.test ? std::string(" (essai)") : std::string()), k.test ? Tone::Warning : Tone::Ok});
        else if (w.adminKey().present)   // 1.44.0 : la cle d'admin de ce poste, et ce qu'elle peut
            poste_.push_back({"Cl\xC3\xA9 d'admin",
                              w.adminKey().id + " \xC2\xB7 " + (role == Role::Admin ? std::to_string(w.myRights().size()) + " droits sur 13" : roleLabel(role)),
                              role == Role::Admin ? Tone::Ok : role == Role::Pending ? Tone::Warning : Tone::Error});
        else
            poste_.push_back({"Cl\xC3\xA9", "aucune sur ce poste", Tone::Error});
        if (k.present) poste_.push_back({"Protection", k.protectedHere ? "chiffr\xC3\xA9" "e pour ce compte (DPAPI)" : "fichier r\xC3\xA9serv\xC3\xA9 \xC3\xA0 ce compte", Tone::Info});
        if (w.hasRegistry())
            poste_.push_back({"Registre", "n\xC2\xB0 " + std::to_string(w.registry().number) + ", du " + dateFr(w.registry().issuedAt), Tone::Info});
        else
            poste_.push_back({"Registre", "pas encore sign\xC3\xA9", Tone::Warning});
        poste_.push_back({"R\xC3\xA9vocations", w.hasRevocations() ? "liste n\xC2\xB0 " + std::to_string(w.revocations().number) + " (" +
                                                                  plural(static_cast<int>(w.revocations().revoked.size()), "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + ")"
                                                            : std::string("aucune liste sign\xC3\xA9" "e"),
                          Tone::Info});
        poste_.push_back({"Archive", plural(static_cast<int>(w.licences().size()), "licence sign\xC3\xA9" "e", "licences sign\xC3\xA9" "es"), Tone::Info});
        poste_.push_back({"Activations", plural(static_cast<int>(w.records().size()), "ligne", "lignes") + " dans le registre", Tone::Info});
        const auto& sv = w.service();
        if (!sv.problem.empty()) {
            poste_.push_back({"Service", sv.problem, Tone::Error});
        } else if (!sv.configured) {
            poste_.push_back({"Service", "non r\xC3\xA9gl\xC3\xA9 (licences par fichier seulement)", Tone::Info});
        } else {
            std::string host = sv.config.url;
            if (const auto p = host.find("://"); p != std::string::npos) host.erase(0, p + 3);
            const bool certBad = sv.hasState && (!sv.state.certOk || sv.state.needsRenewal(w.now()));
            poste_.push_back({"Service", host + (certBad ? std::string(" (certificat \xC3\xA0 renouveler)") : std::string()), certBad ? Tone::Warning : Tone::Info});
            poste_.push_back({"Demandes", sv.fetch.fetchedAt ? n(sv.pending) + " en ligne en attente (" + shortWhen(sv.fetch.fetchedAt) + ")"
                                                             : std::string("jamais relev\xC3\xA9" "es"),
                              sv.pending ? Tone::Warning : Tone::Info});
            poste_.push_back({"Archive en ligne", !sv.syncKey ? std::string("pas de cl\xC3\xA9 de synchro")
                                                  : sv.lastSync.at ? "synchronis\xC3\xA9" "e " + shortWhen(sv.lastSync.at) : std::string("jamais synchronis\xC3\xA9" "e"),
                              !sv.syncKey || !sv.lastSync.at ? Tone::Warning : Tone::Info});
        }
        if (!w.archiveProblems().empty())
            poste_.push_back({"Illisibles", plural(static_cast<int>(w.archiveProblems().size()), "fichier de l'archive", "fichiers de l'archive"), Tone::Warning});
        if (w.skippedLines() > 0)
            poste_.push_back({"Illisibles", plural(w.skippedLines(), "ligne des activations", "lignes des activations"), Tone::Warning});
        poste_.push_back({"Dossier", w.folder(), Tone::Info});
        invalidateLayout();
    }

    bool dropFiles(const std::vector<std::string>& paths) override {
        if (paths.empty() || !std::all_of(paths.begin(), paths.end(), isRequest)) return false;   // la coquille les range
        if (licdlg::wizardTakeDrop(paths)) return true;   // l'assistant deja ouvert les prend
        if (!ws().can("licence_creer") || !ws().hasRegistry()) {   // 1.44.0 : le droit, plus seulement la cle de l'editeur
            host_.report(OpResult::fail(ws().role() == Role::None ? "pas de cl\xC3\xA9 de l'\xC3\xA9" "diteur sur ce poste : cr\xC3\xA9" "e-la d'abord (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9), puis glisse \xC3\xA0 nouveau les demandes"
                                        : !ws().can("licence_creer") ? ws().whyNot("licence_creer")
                                                            : "pas encore de registre sign\xC3\xA9 : signe-le d'abord (\xC3\x89" "ditions), puis glisse \xC3\xA0 nouveau les demandes"));
            return true;
        }
        openWizard(paths);
        return true;
    }

    // Les essais.
    [[nodiscard]] const Dashboard& numbers() const noexcept { return d_; }

protected:
    void onLayout() override {
        const Rect  b = bounds();
        const float pad = b.w < 900.f ? 20.f : 28.f;
        const float x0 = b.x + pad, w = std::max(240.f, b.w - 2 * pad);
        const bool  compact = b.h < 680.f;
        float       y = b.y + (compact ? 18.f : 26.f);
        // Le titre, sa ligne, et deux actions a droite.
        titleY_ = y;
        float bx = x0 + w;
        for (ui::Button* btn : {noteRequests_, sync_}) {
            const float bw = btn->sizeHint().preferred.w + 8.f;
            bx -= bw;
            btn->setBounds({bx, y + 4.f, bw, 34.f});
            bx -= 8.f;
        }
        newLicence_->setBounds({bx, y + 4.f, 10.f, 34.f});
        titleRight_ = bx - 12.f;
        y += 36.f;
        leadY_ = y;
        y += compact ? 28.f : 36.f;

        // Le premier lancement.
        hintRect_ = {};
        hintLines_.clear();
        if (hintKind_ != 0) {
            std::vector<ui::Button*> btns;
            if (hintKind_ == 1) btns = {createKey_, restore_, becomeAdmin_};
            else if (hintKind_ == 2) btns = {toEditions_};
            else if (hintKind_ == 3) btns = {myFingerprint_};
            else if (hintKind_ == 5) btns = {toService_};
            float bw = 0.f;
            for (auto* btn : btns) bw += btn->sizeHint().preferred.w + 8.f;
            const bool beside = w - bw - 64.f > 320.f;
            hintLines_ = wrap(hint_, kF15, (beside ? w - bw - 72.f : w - 64.f));
            const float textH = static_cast<float>(hintLines_.size()) * 20.f;
            const float h = beside ? std::max(textH, 34.f) + 28.f : textH + 34.f + 28.f + 8.f;
            hintRect_ = {x0, y, w, h};
            float hx = x0 + w - 14.f;
            const float hy = beside ? y + (h - 34.f) * 0.5f : y + 14.f + textH + 8.f;
            if (!beside) hx = x0 + 44.f + bw;
            for (auto it = btns.rbegin(); it != btns.rend(); ++it) {
                const float bw1 = (*it)->sizeHint().preferred.w + 8.f;
                hx -= bw1;
                (*it)->setBounds({hx, hy, bw1, 34.f});
                hx -= 8.f;
            }
            y += h + 16.f;
        }

        // 1.45.0 : le bandeau du service a mettre a jour (le texte, << Deployer le service >> a droite ou dessous).
        serviceRect_ = {};
        serviceLines_.clear();
        if (!serviceNote_.empty()) {
            const float bw = deploy_->sizeHint().preferred.w + 8.f;
            const bool beside = w - bw - 64.f > 320.f;
            serviceLines_ = wrap(serviceNote_, kF15, beside ? w - bw - 72.f : w - 64.f);
            const float textH = static_cast<float>(serviceLines_.size()) * 20.f;
            const float h = beside ? std::max(textH, 34.f) + 28.f : textH + 34.f + 28.f + 8.f;
            serviceRect_ = {x0, y, w, h};
            deploy_->setBounds(beside ? Rect{x0 + w - 14.f - bw, y + (h - 34.f) * 0.5f, bw, 34.f} : Rect{x0 + 44.f, y + 14.f + textH + 8.f, bw, 34.f});
            y += h + 16.f;
        }
        // Les quatre grandes tuiles, puis les quatre petites.
        const float gap = 16.f;
        const float bigH = compact ? 104.f : 126.f, smallH = compact ? 46.f : 52.f;
        const float tileW = (w - 3 * gap) / 4.f;
        for (std::size_t i = 0; i < tiles_.size(); ++i) {
            const bool big = i < 4;
            const float tx = x0 + static_cast<float>(i % 4) * (tileW + gap);
            const float ty = big ? y : y + bigH + 12.f;
            tiles_[i]->setBounds({tx, ty, tileW, big ? bigH : smallH});
        }
        y += bigH + 12.f + smallH + gap;

        // 1.47.0 : la rangee de courbes (a gauche) et la petite carte du globe (a droite), si la place et le service le permettent.
        stripShown_ = strip_->shown();
        const bool curves = !compact && stripShown_ && b.bottom() - 18.f - y > 150.f + gap + 200.f;
        strip_->setVisibility(curves ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        globe_->setVisibility(curves ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        if (curves) {
            const float rw = std::clamp(w * 0.34f, 300.f, 440.f);
            strip_->setBounds({x0, y, w - rw - gap, 150.f});
            globe_->setBounds({x0 + w - rw, y, rw, 150.f});
            y += 150.f + gap;
        }

        // 1.49.0 : le bandeau << La 1.49 peut repartir de zero >> (le texte, les deux boutons a droite ou dessous) : SOUS la
        // rangee de courbes, pris sur la place de l'activite - il ne pousse ni les tuiles ni la rangee.
        offerRect_ = {};
        offerLines_.clear();
        if (offer_) {
            const float bw1 = offerOpen_->sizeHint().preferred.w + 8.f, bw2 = offerClose_->sizeHint().preferred.w + 8.f;
            const bool beside = w - bw1 - bw2 - 8.f - 64.f > 420.f;
            offerLines_ = wrap(kOfferText, kF15, beside ? w - bw1 - bw2 - 8.f - 72.f : w - 64.f);
            const float textH = static_cast<float>(offerLines_.size()) * 20.f;
            const float h = beside ? std::max(textH, 34.f) + 28.f : textH + 34.f + 28.f + 8.f;
            offerRect_ = {x0, y, w, h};
            if (beside) {
                const float by = y + (h - 34.f) * 0.5f;
                offerClose_->setBounds({x0 + w - 14.f - bw2, by, bw2, 34.f});
                offerOpen_->setBounds({x0 + w - 14.f - bw2 - 8.f - bw1, by, bw1, 34.f});
            } else {
                offerOpen_->setBounds({x0 + 44.f, y + 14.f + textH + 8.f, bw1, 34.f});
                offerClose_->setBounds({x0 + 44.f + bw1 + 8.f, y + 14.f + textH + 8.f, bw2, 34.f});
            }
            y += h + gap;
        }

        // En bas : l'activite a gauche ; a droite, les editions puis l'etat du poste.
        const float bottom = b.bottom() - 18.f;
        const float rightW = std::clamp(w * 0.34f, 300.f, 440.f);
        const float leftW = w - rightW - gap;
        opsRect_ = {x0, y, leftW, std::max(120.f, bottom - y)};
        feed_->setBounds({opsRect_.x + 1.f, opsRect_.y + 52.f, opsRect_.w - 2.f, std::max(40.f, opsRect_.h - 58.f)});
        const std::size_t bars = std::max<std::size_t>(d_.byEdition.size(), 2);
        barH_ = compact ? 40.f : 46.f;
        lineH_ = compact ? 19.f : 22.f;
        const float wantEd = 56.f + static_cast<float>(std::min<std::size_t>(bars, 6)) * barH_ + 8.f;
        const float wantPoste = 56.f + static_cast<float>(poste_.size()) * lineH_ + 10.f;
        const float colH = std::max(200.f, bottom - y);
        const float edH = std::clamp(colH - wantPoste - gap, 100.f, wantEd);
        editionsRect_ = {x0 + leftW + gap, y, rightW, edH};
        posteRect_ = {x0 + leftW + gap, y + edH + gap, rightW, std::max(60.f, colH - edH - gap)};
    }

    void onPaint(const ui::PaintContext& ctx) override {
        if (strip_ && strip_->shown() != stripShown_) invalidateLayout();   // 1.47.0 : le service lu (ou regle) depuis : la rangee suit
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Rect  b = bounds();
        ctx.r.fillRect(b, c.windowBg);
        const float pad = b.w < 900.f ? 20.f : 28.f;
        const float x0 = b.x + pad;
        ctx.r.drawText({x0, titleY_}, "Tableau de bord", gfx::semibold(kF28), c.text);
        // La ligne sous le titre : le jour, et ce qui se fait seul.
        const std::string lead = longDateFr(ws().now()) + " \xC2\xB7 " +
                                 (ws().service().ready() ? std::string("ce qui change en ligne appara\xC3\xAEt seul ; un clic sur un chiffre ouvre sa page.")
                                                         : std::string("ce que disent l'archive et le registre de ce poste ; un clic sur un chiffre ouvre sa page."));
        ctx.r.drawText({x0, leadY_}, fit(ctx.r, lead, kF14, b.w - 2 * pad), kF14, c.textMuted);

        if (!hintRect_.empty()) {
            cardBox(ctx.r, th, hintRect_, Color::rgb(0xFFFFFF), c.accent.withAlpha(120));
            ctx.r.fillRoundedRect(hintRect_.inset(1.f, 1.f), c.accent.withAlpha(14), 11.f);
            ui::drawIcon(ctx.r, hintKind_ == 1 ? ui::Icon::Lock : ui::Icon::Info, {hintRect_.x + 16.f, hintRect_.y + 14.f, 18.f, 18.f}, c.accent);
            float y = hintRect_.y + 14.f;
            for (const auto& l : hintLines_) {
                ctx.r.drawText({hintRect_.x + 44.f, y}, l, kF15, c.text);
                y += 20.f;
            }
        }

        // 1.45.0 : le service a mettre a jour.
        if (!serviceRect_.empty()) {
            cardBox(ctx.r, th, serviceRect_, Color::rgb(0xFFFFFF), c.warning.withAlpha(140));
            ctx.r.fillRoundedRect(serviceRect_.inset(1.f, 1.f), c.warning.withAlpha(18), 11.f);
            ui::drawIcon(ctx.r, ui::Icon::Warning, {serviceRect_.x + 16.f, serviceRect_.y + 14.f, 18.f, 18.f}, c.warning);
            float y = serviceRect_.y + 14.f;
            for (const auto& l : serviceLines_) {
                ctx.r.drawText({serviceRect_.x + 44.f, y}, l, kF15, c.text);
                y += 20.f;
            }
        }

        // 1.49.0 : la proposition de repartir de zero.
        if (!offerRect_.empty()) {
            cardBox(ctx.r, th, offerRect_, Color::rgb(0xFFFFFF), c.info.withAlpha(140));
            ctx.r.fillRoundedRect(offerRect_.inset(1.f, 1.f), c.info.withAlpha(18), 11.f);
            ui::drawIcon(ctx.r, ui::Icon::Lock, {offerRect_.x + 16.f, offerRect_.y + 14.f, 18.f, 18.f}, c.info);
            float y = offerRect_.y + 14.f;
            for (const auto& l : offerLines_) {
                ctx.r.drawText({offerRect_.x + 44.f, y}, l, kF15, c.text);
                y += 20.f;
            }
        }

        // L'activite.
        cardBox(ctx.r, th, opsRect_);
        const FontId fh = gfx::semibold(kF16);
        ctx.r.drawText({opsRect_.x + 20.f, opsRect_.y + 17.f}, "Activit\xC3\xA9", fh, c.text);
        {
            const std::string n = d_.recent.empty() ? std::string() : d_.recent.size() >= 12 ? std::string("les 12 plus r\xC3\xA9" "centes") : plural(static_cast<int>(d_.recent.size()), "op\xC3\xA9ration", "op\xC3\xA9rations");
            const float nw = ctx.r.measure(n, kF13).width;
            ctx.r.drawText({opsRect_.right() - 20.f - nw, opsRect_.y + 19.f}, n, kF13, c.textMuted);
        }
        ctx.r.fillRect({opsRect_.x + 1.f, opsRect_.y + 51.f, opsRect_.w - 2.f, 1.f}, c.gridLine);

        // Les licences par edition : le nom, le nombre, la barre dessous (la maquette).
        cardBox(ctx.r, th, editionsRect_);
        ctx.r.drawText({editionsRect_.x + 20.f, editionsRect_.y + 17.f}, "Licences par \xC3\xA9" "dition", fh, c.text);
        {
            const std::size_t fits = static_cast<std::size_t>(std::max(1.f, (editionsRect_.bottom() - 8.f - (editionsRect_.y + 56.f)) / barH_));
            const std::size_t hidden = d_.byEdition.size() > fits ? d_.byEdition.size() - fits : 0;
            int total = 0;
            for (const auto& e : d_.byEdition) total += e.second;
            const std::string sub = hidden ? "+ " + std::to_string(hidden) + (hidden > 1 ? " autres" : " autre") : std::to_string(total) + (total > 1 ? " actives" : " active");
            const float sw = ctx.r.measure(sub, kF13).width;
            ctx.r.drawText({editionsRect_.right() - 20.f - sw, editionsRect_.y + 19.f}, sub, kF13, c.textMuted);
            float y = editionsRect_.y + 56.f;
            if (d_.byEdition.empty()) {
                ctx.r.drawText({editionsRect_.x + 20.f, y}, "Aucune licence active.", kF14, c.textMuted);
            } else {
                int most = 1;
                for (const auto& [name, count] : d_.byEdition) most = std::max(most, count);
                const FontId fn = gfx::semibold(kF13);
                for (std::size_t i = 0; i < d_.byEdition.size() && i < fits; ++i) {
                    const auto& [name, count] = d_.byEdition[i];
                    const std::string cnt = std::to_string(count);
                    const float cw = ctx.r.measure(cnt, kF14).width;
                    ctx.r.drawText({editionsRect_.x + 20.f, y}, fit(ctx.r, name, fn, editionsRect_.w - 60.f - cw), fn, c.text);
                    ctx.r.drawText({editionsRect_.right() - 20.f - cw, y}, cnt, kF14, c.textMuted);
                    const Rect track{editionsRect_.x + 20.f, y + 22.f, editionsRect_.w - 40.f, 8.f};
                    ctx.r.fillRoundedRect(track, c.gridLine, 4.f);
                    ctx.r.fillRoundedRect({track.x, track.y, std::max(8.f, track.w * static_cast<float>(count) / static_cast<float>(most)), track.h}, c.accent, 4.f);
                    y += barH_;
                }
            }
        }

        // L'etat du poste.
        cardBox(ctx.r, th, posteRect_);
        ctx.r.drawText({posteRect_.x + 20.f, posteRect_.y + 17.f}, "\xC3\x89tat du poste", fh, c.text);
        {
            float y = posteRect_.y + 54.f;
            const float labelW = 112.f;
            for (const auto& l : poste_) {
                if (y + 18.f > posteRect_.bottom() - 4.f) break;
                ctx.r.drawText({posteRect_.x + 20.f, y}, l.label, kF13, c.textMuted);
                const float vx = posteRect_.x + 20.f + labelW, vw = posteRect_.right() - 20.f - vx;
                const std::string v = l.label == "Dossier" ? fitTail(ctx.r, l.value, kF13, vw) : fit(ctx.r, l.value, kF13, vw);
                ctx.r.drawText({vx, y}, v, kF13, l.tone == Tone::Info ? c.text : toneColor(th, l.tone));
                y += lineH_;
            }
        }
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!d_.recent.empty()) return;
        const auto& c = ctx.theme.color;
        const Rect  t = feed_->bounds();
        ctx.r.drawText({t.x + 20.f, t.y + 18.f}, fit(ctx.r, "Aucune op\xC3\xA9ration pour l'instant : signe une licence ou note une demande.", kF14, t.w - 40.f), kF14,
                       c.textMuted);
    }

private:
    // Les dernieres operations, en fil.
    void rebuildFeed() {
        std::vector<FeedRow> rows;
        const Seconds now = ws().now();
        for (const auto& a : d_.recent) {
            FeedRow f;
            if (a.kind == "licence") {
                f.what = a.extension ? "Licence \xC3\xA9tendue" : "Licence sign\xC3\xA9" "e";
                f.icon = ui::Icon::Ok;
                f.tone = Tone::Ok;
            } else if (a.kind == "demande") {
                f.what = "Demande re\xC3\xA7ue";
                f.icon = ui::Icon::Mail;
                f.tone = Tone::Warning;
            } else if (a.kind == "revocation") {
                f.what = a.deviceKey.empty() ? "Licence r\xC3\xA9voqu\xC3\xA9" "e" : "PC r\xC3\xA9voqu\xC3\xA9";
                f.icon = ui::Icon::Close;
                f.tone = Tone::Error;
            } else if (a.kind == "client") {
                // 1.44.0 : un client supprime ou restaure (Clients) - ni PC ni licence a dire : son nom, ses licences.
                const bool restored = a.action == "restaure";
                f.what = (restored ? "Client restaur\xC3\xA9 : " : "Client supprim\xC3\xA9 : ") + a.customer;
                f.icon = restored ? ui::Icon::Undo : ui::Icon::Delete;
                f.tone = restored ? Tone::Ok : Tone::Warning;
                f.who.clear();
                f.detail = a.licences.empty() ? std::string("aucune licence")
                                              : plural(static_cast<int>(a.licences.size()), "licence", "licences") + (restored ? " revenue(s) de la corbeille" : " dans la corbeille");
                if (!a.by.empty()) f.detail += " \xC2\xB7 par " + a.by;
                f.when = agoFr(now, a.at);
                f.tip = dateTimeFr(a.at) + ". Double-clic : l'historique.";
                rows.push_back(std::move(f));
                continue;
            } else {
                f.what = a.kind;
            }
            // Une revocation ne note pas le client : celui de la licence de l'archive.
            std::string customer = a.customer;
            if (customer.empty() && !a.licenseId.empty())
                if (const LicenceEntry* l = ws().licence(a.licenseId)) customer = l->licence.customer;
            f.who = customer.empty() ? std::string() : customer + " \xC2\xB7";
            const std::string pc = a.kind == "revocation" && a.deviceKey.empty() ? std::string("toute la licence")
                                   : a.deviceName.empty()                         ? std::string("(sans nom)")
                                                                                  : a.deviceName;
            f.detail = (a.licenseId.empty() ? std::string() : "Licence " + a.licenseId + " \xC2\xB7 ") + "PC " + pc;
            f.when = agoFr(now, a.at);
            f.tip = dateTimeFr(a.at) + (a.fingerprint.empty() ? std::string() : " \xC2\xB7 empreinte de la cl\xC3\xA9 du PC : " + a.fingerprint) + ". Double-clic : l'historique.";
            rows.push_back(std::move(f));
        }
        feed_->setRows(std::move(rows));
    }

    ui::Button* addButton(const char* text, const char* id, ui::Button::Style style, ui::Icon icon) {
        auto b = std::make_unique<ui::Button>(text, id);
        b->setStyle(style);
        b->setIcon(icon);
        return &static_cast<ui::Button&>(addChild(std::move(b)));
    }

    void openWizard(std::vector<std::string> requests) {
        if (!ws().can("licence_creer") || !ws().hasRegistry()) {   // 1.44.0 : le droit
            host_.report(OpResult::fail(ws().role() != Role::None && !ws().can("licence_creer") ? ws().whyNot("licence_creer")
                                                                                               : std::string("il faut d'abord la cl\xC3\xA9 de l'\xC3\xA9" "diteur et le registre sign\xC3\xA9")));
            return;
        }
        host_.openDialog(makeLicenceWizard(host_, std::move(requests)));
    }

    void askRequests() {
        if (host_.busy()) {
            host_.report(OpResult::fail("un travail lent tourne : attends sa fin pour noter des demandes"));
            return;
        }
        ui::FilePick pick;
        pick.title = "Choisis la demande d'activation re\xC3\xA7ue (.xpgreq)";
        pick.filters = {{"Demande d'activation (.xpgreq)", "xpgreq"}};
        host_.pickFile(pick, [this](std::string path) {
            if (path.empty()) return;
            if (host_.busy()) {
                host_.report(OpResult::fail("un travail lent tourne : attends sa fin pour noter des demandes"));
                return;
            }
            host_.report(ws().recordRequests({path}));
        });
    }

    Dashboard                    d_;
    std::vector<PosteLine>       poste_;
    std::string                  hint_;
    int                          hintKind_{0};   // 1 : pas de cle ; 2 : pas de registre
    std::vector<std::string>     hintLines_;
    ui::Button*                  newLicence_{nullptr};
    ui::Button*                  noteRequests_{nullptr};
    ui::Button*                  sync_{nullptr};
    ui::Button*                  createKey_{nullptr};
    ui::Button*                  restore_{nullptr};
    ui::Button*                  toEditions_{nullptr};
    ui::Button*                  becomeAdmin_{nullptr};    // 1.44.0
    ui::Button*                  myFingerprint_{nullptr};  // 1.44.0
    ui::Button*                  toService_{nullptr};      // 1.44.0
    ui::Button*                  deploy_{nullptr};         // 1.45.0 : Deployer le service (le bandeau)
    std::string                  serviceNote_;             // 1.45.0 : la phrase du bandeau ("" : a jour)
    std::vector<std::string>     serviceLines_;
    Rect                         serviceRect_{};
    // 1.49.0 : le bandeau << La 1.49 peut repartir de zero >> (l'admin+, une fois)
    static constexpr const char* kOfferText =
        "La 1.49 peut repartir de z\xC3\xA9ro : effacer les licences, les clients, les PC, les demandes, les commandes et les admins, sur le service et "
        "sur ce poste. Ta cl\xC3\xA9, ton catalogue et ton service restent ; une sauvegarde compl\xC3\xA8te est faite d'abord (\xC2\xAB Restaurer \xC2\xBB "
        "remet tout). Rien n'est effac\xC3\xA9 sans ton accord : ce bandeau ne revient pas (Service en ligne \xE2\x80\xBA Repartir de z\xC3\xA9ro, plus tard).";
    ui::Button*                  offerOpen_{nullptr};
    ui::Button*                  offerClose_{nullptr};
    bool                         offer_{false}, offerDecided_{false};
    std::vector<std::string>     offerLines_;
    Rect                         offerRect_{};
    std::shared_ptr<bool>        alive_{std::make_shared<bool>(true)};
    std::vector<StatTile*>       tiles_;
    ActivityFeed*                feed_{nullptr};
    statskit::DashboardStrip*    strip_{nullptr};           // 1.47.0 : la rangee de courbes
    bool                         stripShown_{false};       // 1.47.0 : ce que la mise en page a vu (le service lu ensuite : refaite)
    ui::Widget*                  globe_{nullptr};           // 1.47.0 : la petite carte du globe (PageStatsGlobe.cpp)
    Rect                         hintRect_{}, opsRect_{}, editionsRect_{}, posteRect_{};
    float                        barH_{46.f}, lineH_{22.f};
    float                        titleY_{0.f}, leadY_{0.f}, titleRight_{0.f};
    core::ConnectionScope        links_;
};

} // namespace

PagePtr makeDashboardPage(ShellHost host) { return std::make_unique<DashboardPage>(std::move(host)); }

} // namespace license_admin::gui
