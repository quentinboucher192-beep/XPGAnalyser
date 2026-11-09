// =============================================================================
//  app/UpdateWindows.cpp - 1.44.0 : LES FENETRES DES MESSAGES ET DES MISES A
//  JOUR (maquettes 1.43.0 : planche 05 << notification avec fichier >>, planche
//  06 << mise a jour >>) ; la logique : app/Updates
// -----------------------------------------------------------------------------
//  LA FENETRE D'UN MESSAGE (le bouton de sa notification) : l'en-tete (qui,
//  quand, << message signe par l'editeur >>), le texte, la carte de chaque
//  fichier joint - son nom, sa taille, sa version ; les pastilles qui sont
//  VRAIES seulement (<< Signe par l'editeur >> : son manifeste verifie ;
//  << Empreinte verifiee >> : telecharge et verifie ; << recu du service XPG -
//  chiffre >>) ; le SHA-256 court ; Enregistrer... et Installer -, << Ce qui va
//  se passer >> pour l'installateur de l'Admin envoye pour devenir admin,
//  << Envoye a ce PC seulement >>, la carte << Fichier refuse >>, et en bas
//  << Un fichier joint est garde 30 jours >>, Supprimer, Fermer.
//
//  LA FENETRE DE LA MISE A JOUR (la notification, le toast, la pastille) : les
//  nouveautes, les fichiers (1 ; 2 sur le PC d'un admin, et << Ton Admin ... n'est
//  plus a jour >>), Telecharger et installer (Tout installer), Plus tard, Voir les
//  nouveautes ; pendant l'installation, la carte : la barre, les quatre coches
//  (Telecharge, Signature de l'editeur, Empreinte SHA-256, le projet enregistre),
//  << IHM Studio se ferme dans 5 s, puis se rouvre >>, Annuler.
//
//  LE TOAST : en bas a droite, par-dessus tout (App le peint apres les ecrans),
//  << Mise a jour X disponible >>, Voir, la croix ; 20 s, puis il s'en va.
// =============================================================================
#include "Licence.hpp"
#include "Updates.hpp"

#include "App.hpp"
#include "FileDownload.hpp"
#include "screens/Screens.hpp"

#include "../core/Edition.hpp"
#include "../core/Version.hpp"
#include "../licensing/Time.hpp"
#include "../menu/MenuManager.hpp"
#include "../ui/DialogFrame.hpp"
#include "../ui/Icons.hpp"
#include "../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

namespace {

constexpr gfx::FontId kTitleF{19};
constexpr gfx::FontId kH3{15};
constexpr gfx::FontId kBodyF{14};
constexpr gfx::FontId kCap{13};
constexpr gfx::FontId kTiny{12};

gfx::Color tint(gfx::Color c, int a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(a, 0, 255))); }

std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

// Les lignes d'un texte a cette largeur (les retours a la ligne gardes) ; `max` lignes au plus (la derniere : ...).
std::vector<std::string> wrap(const std::string& text, gfx::FontId f, float w, std::size_t max) {
    std::vector<std::string> out;
    std::string para;
    const auto flushPara = [&](const std::string& p) {
        std::string line, word;
        const auto push = [&] {
            if (word.empty()) return;
            const std::string t = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui::measureWidth(t, f) > w) {
                out.push_back(line);
                line = word;
            } else {
                line = t;
            }
            word.clear();
        };
        for (char c : p) {
            if (c == ' ') push();
            else word += c;
        }
        push();
        out.push_back(line);
    };
    for (char c : text) {
        if (c == '\r') continue;
        if (c == '\n') {
            flushPara(para);
            para.clear();
        } else {
            para += c;
        }
    }
    if (!para.empty() || out.empty()) flushPara(para);
    if (out.size() > max) {
        out.resize(max);
        out.back() += " \xE2\x80\xA6";
    }
    return out;
}

menu::MenuTraits dialogTraits() {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

// "aujourd'hui 11:02", "le 04/11/2026 \xC3\xA0 11:02" (l'heure locale).
std::string whenText(licensing::Seconds at) {
    if (at <= 0) return {};
    const std::time_t t = static_cast<std::time_t>(at), now = std::time(nullptr);
    std::tm a{}, n{};
#if defined(_WIN32)
    localtime_s(&a, &t);
    localtime_s(&n, &now);
#else
    localtime_r(&t, &a);
    localtime_r(&now, &n);
#endif
    char hm[16];
    std::snprintf(hm, sizeof hm, "%02d:%02d", a.tm_hour, a.tm_min);
    if (a.tm_year == n.tm_year && a.tm_yday == n.tm_yday) return std::string("aujourd'hui ") + hm;
    char d[64];
    std::snprintf(d, sizeof d, "le %02d/%02d/%04d \xC3\xA0 ", a.tm_mday, a.tm_mon + 1, a.tm_year + 1900);
    return d + std::string(hm);
}

// Une pastille (un ton, une coche facultative) ; rend sa largeur.
float pill(gfx::IRenderer& r, float x, float y, const std::string& text, gfx::Color tone, bool check, bool dark) {
    const float h = 22.f;
    const float w = r.measure(text, kTiny).width + (check ? 30.f : 16.f);
    r.fillRoundedRect({x, y, w, h}, tint(tone, dark ? 60 : 34), h / 2.f);
    float tx = x + 8.f;
    if (check) {
        ui::drawIcon(r, ui::Icon::Ok, {x + 7.f, y + 5.f, 12.f, 12.f}, tone);
        tx += 14.f;
    }
    r.drawText({tx, y + (h - r.lineHeight(kTiny)) / 2.f}, text, kTiny, tone);
    return w;
}

// Le carre d'un fichier (une icone sur un fond de couleur).
void fileTile(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color tone, ui::Icon icon) {
    r.fillRoundedRect(b, tone, 12.f);
    ui::drawIcon(r, icon, {b.x + b.w * 0.27f, b.y + b.h * 0.27f, b.w * 0.46f, b.h * 0.46f}, gfx::Color{255, 255, 255, 255});
}

void overline(gfx::IRenderer& r, float x, float y, const std::string& text, gfx::Color c) {
    std::string up;
    for (char ch : text) up += (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 32) : ch;
    r.drawText({x, y}, up, kTiny, c);
}

// Ce qu'on attend d'un fichier (la copie deja la : sa taille ; l'affichage, chaque image).
filedl::Want wantOf(const std::string& id, const std::string& name, std::uint64_t size, const std::string& sha256) {
    filedl::Want w;
    w.id = id;
    w.name = name;
    w.size = size;
    w.sha256 = sha256;
    return w;
}

ui::Visibility shown(bool on) { return on ? ui::Visibility::Visible : ui::Visibility::Hidden; }

gfx::Color violet() { return gfx::Color{109, 74, 255, 255}; }
gfx::Color teal() { return gfx::Color{13, 148, 136, 255}; }

// ============================================================ la fenetre d'un message (planche 05) ==
class MessageBody final : public ui::Widget {
public:
    MessageBody(App& app, std::string key, std::function<void()> close)
        : ui::Widget("dialog.message"), app_(app), key_(std::move(key)), close_(std::move(close)) {
        reload();
        for (const auto& f : data_.files) {
            if (!f.refused.empty()) continue;
            auto* save = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Enregistrer\xE2\x80\xA6", "dialog.message.enregistrer." + f.id)));
            save->setIcon(ui::Icon::Save);
            auto* inst = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Installer", "dialog.message.installer." + f.id)));
            inst->setIcon(ui::Icon::Import);
            inst->setStyle(ui::Button::Style::Primary);
            const std::string id = f.id;
            links_ += save->clicked->connect([this, id] {
                std::string why;
                if (!updates::saveMessageFile(key_, id, &why)) status_ = why;
            });
            links_ += inst->clicked->connect([this, id] {
                std::string why;
                if (!updates::installMessageFile(key_, id, &why)) status_ = why;
            });
            cards_.push_back({id, save, inst, {}});
        }
        remove_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Supprimer", "dialog.message.supprimer")));
        remove_->setIcon(ui::Icon::Delete);
        remove_->setStyle(ui::Button::Style::Flat);
        close_b_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Fermer", "dialog.message.fermer")));
        links_ += remove_->clicked->connect([this] {
            if (const auto n = updates::notice(key_)) (void)notices::remove(n->id);
            if (close_) close_();
        });
        links_ += close_b_->clicked->connect([this] {
            if (close_) close_();
        });
    }

    void poll() {
        const auto rev = notices::revision();
        float frac = 0.f;
        const std::string now = updates::fetching(&frac) + "|" + std::to_string(static_cast<int>(frac * 100.f)) + "|" + updates::lastFetch();
        if (rev != seenRev_ || now != seenFetch_) {
            seenRev_ = rev;
            seenFetch_ = now;
            reload();
            invalidate();
        }
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(860.f, std::max(520.f, r.w - 40.f));
        const float want = contentHeight(w) + 64.f + 64.f;
        const float h = std::min(std::max(360.f, r.h - 40.f), want);
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        closeBox_ = {panel_.right() - 40.f, panel_.y + 18.f, 24.f, 24.f};
        ui::dialogframe::keep(*this, closeBox_);
        body_ = {panel_.x, panel_.y + 76.f, panel_.w, panel_.h - 76.f - 60.f};
        maxScroll_ = std::max(0.f, contentHeight(w) - body_.h);
        scroll_ = std::clamp(scroll_, 0.f, maxScroll_);
        // les boutons de chaque carte (a droite du nom)
        float y = body_.y + 20.f - scroll_ + textH_ + 44.f;
        for (auto& c : cards_) {
            const float iw = std::max(96.f, c.install->sizeHint().preferred.w);
            const float sw = std::max(110.f, c.save->sizeHint().preferred.w);
            const float by = y + 24.f;
            c.install->setBounds({panel_.right() - 28.f - 18.f - iw, by, iw, 32.f});
            c.save->setBounds({c.install->bounds().x - 8.f - sw, by, sw, 32.f});
            const bool visible = by > body_.y - 8.f && by + 32.f < body_.bottom() + 8.f;
            c.install->setVisibility(shown(visible));
            c.save->setVisibility(shown(visible));
            c.top = y;
            y += kCardH + 12.f;
        }
        const float cw = std::max(96.f, close_b_->sizeHint().preferred.w);
        close_b_->setBounds({panel_.right() - 20.f - cw, panel_.bottom() - 46.f, cw, 32.f});
        const float rw = std::max(100.f, remove_->sizeHint().preferred.w);
        remove_->setBounds({close_b_->bounds().x - 8.f - rw, close_b_->bounds().y, rw, 32.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const bool dark = ctx.theme.isDark();
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        // ---- l'en-tete : qui, quand, signe
        const std::string who = data_.from.empty() ? std::string("l'\xC3\xA9" "diteur") : data_.from;
        const gfx::Rect av{panel_.x + 24.f, panel_.y + 16.f, 44.f, 44.f};
        r.fillRoundedRect(av, violet(), 12.f);
        std::string letter = who.substr(0, 1);
        if (!letter.empty() && (static_cast<unsigned char>(letter[0]) & 0x80)) letter = "X";   // une lettre accentuee : X (XPG)
        r.drawText({av.x + (av.w - r.measure(letter, kTitleF).width) / 2.f, av.y + (av.h - r.lineHeight(kTitleF)) / 2.f}, letter, kTitleF,
                   gfx::Color{255, 255, 255, 255});
        const float tx = av.right() + 14.f;
        r.drawText({tx, panel_.y + 14.f}, fit(r, title_, kTitleF, closeBox_.x - tx - 12.f), kTitleF, c.text);
        const std::string sub = "De " + who + (data_.from.empty() ? std::string() : ", l'\xC3\xA9" "diteur") +
                                (data_.issued ? " \xC2\xB7 " + whenText(data_.issued) : std::string());
        const float sy = panel_.y + 14.f + r.lineHeight(kTitleF) + 6.f;
        r.drawText({tx, sy + 3.f}, sub, kCap, c.textMuted);
        (void)pill(r, tx + r.measure(sub, kCap).width + 10.f, sy, "message sign\xC3\xA9 par l'\xC3\xA9" "diteur", c.ok, true, dark);
        r.drawText({closeBox_.x + 7.f, closeBox_.y + 2.f}, "\xC3\x97", kTitleF, c.textMuted);
        r.fillRect({panel_.x, panel_.y + 75.f, panel_.w, 1.f}, c.border);
        // ---- le corps (il defile)
        r.pushClip(body_);
        const float x = panel_.x + 28.f, w = panel_.w - 56.f;
        float y = body_.y + 20.f - scroll_;
        for (const auto& l : lines_) {
            r.drawText({x, y}, l, kBodyF, c.text);
            y += r.lineHeight(kBodyF) + 4.f;
        }
        y = body_.y + 20.f - scroll_ + textH_;
        std::size_t ok = 0;
        for (const auto& f : data_.files) ok += f.refused.empty() ? 1 : 0;
        if (ok) {
            overline(r, x, y + 16.f, std::to_string(ok) + (ok > 1 ? " fichiers joints" : " fichier joint"), c.textMuted);
            y += 44.f;
        }
        const std::string busyId = updates::fetching(&frac_);
        for (const auto& f : data_.files) {
            if (!f.refused.empty()) continue;
            paintCard(ctx, f, {x, y, w, kCardH}, busyId == f.id);
            y += kCardH + 12.f;
        }
        if (data_.action == "admin") {
            overline(r, x, y + 10.f, "Ce qui va se passer", c.textMuted);
            y += 34.f;
            const char* steps[4] = {"Tu installes l'Admin (2 min). Tes Studios ne changent pas.", "Il cr\xC3\xA9" "e ta cl\xC3\xA9, sur ce PC. Elle n'en sort jamais.",
                                    "", "Il accepte : tu es admin, l'Admin te le dit."};
            const std::string third = (data_.from.empty() ? std::string("L'\xC3\xA9" "diteur") : data_.from) +
                                      " t'appelle : vous lisez l'empreinte de ta cl\xC3\xA9 ensemble.";
            for (int i = 0; i < 4; ++i) {
                const float cx = x + static_cast<float>(i % 2) * (w / 2.f + 11.f), cy = y + static_cast<float>(i / 2) * 34.f;
                r.fillRoundedRect({cx, cy, 22.f, 22.f}, tint(c.accent, dark ? 60 : 30), 7.f);
                const std::string n = std::to_string(i + 1);
                r.drawText({cx + (22.f - r.measure(n, kTiny).width) / 2.f, cy + (22.f - r.lineHeight(kTiny)) / 2.f}, n, kTiny, c.accent);
                r.drawText({cx + 32.f, cy + 2.f}, fit(r, i == 2 ? third : std::string(steps[i]), kCap, w / 2.f - 44.f), kCap, c.text);
            }
            y += 76.f;
        }
        // les deux notes
        {
            const float nw = data_.action == "admin" ? (w - 12.f) / 2.f : w;
            const gfx::Rect n1{x, y + 8.f, nw, 46.f};
            r.fillRoundedRect(n1, tint(c.info, dark ? 40 : 22), 8.f);
            ui::drawIcon(r, ui::Icon::Screen, {n1.x + 10.f, n1.y + 15.f, 16.f, 16.f}, c.info);
            const auto t1 = wrap("Envoy\xC3\xA9 \xC3\xA0 ce PC seulement : " + who + " l'a choisi. Les autres PC ne le voient pas.", kCap, nw - 44.f, 2);
            for (std::size_t i = 0; i < t1.size(); ++i) r.drawText({n1.x + 34.f, n1.y + 6.f + static_cast<float>(i) * 17.f}, t1[i], kCap, c.text);
            if (data_.action == "admin") {
                const gfx::Rect n2{x + nw + 12.f, y + 8.f, nw, 46.f};
                r.fillRoundedRect(n2, tint(c.accent, dark ? 40 : 22), 8.f);
                ui::drawIcon(r, ui::Icon::Module, {n2.x + 10.f, n2.y + 15.f, 16.f, 16.f}, c.accent);
                const auto t2 = wrap("Les mises \xC3\xA0 jour de l'Admin arriveront ici aussi, avec celles des Studios.", kCap, nw - 44.f, 2);
                for (std::size_t i = 0; i < t2.size(); ++i) r.drawText({n2.x + 34.f, n2.y + 6.f + static_cast<float>(i) * 17.f}, t2[i], kCap, c.text);
            }
            y += 66.f;
        }
        // les fichiers refuses
        for (const auto& f : data_.files) {
            if (f.refused.empty()) continue;
            const gfx::Rect b{x, y + 8.f, w, 62.f};
            r.fillRoundedRect(b, tint(c.error, dark ? 30 : 12), 10.f);
            r.strokeRect(b, tint(c.error, 120), 1.f);
            r.fillRoundedRect({b.x + 12.f, b.y + 14.f, 34.f, 34.f}, tint(c.error, dark ? 60 : 30), 10.f);
            ui::drawIcon(r, ui::Icon::Error, {b.x + 21.f, b.y + 23.f, 16.f, 16.f}, c.error);
            r.drawText({b.x + 58.f, b.y + 12.f}, fit(r, f.name + " \xC2\xB7 refus\xC3\xA9", kH3, b.w - 160.f), kH3, c.text);
            r.drawText({b.x + 58.f, b.y + 34.f},
                       fit(r, "La signature ne correspond pas : il a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9 en route. Il n'est ni propos\xC3\xA9, ni enregistr\xC3\xA9.", kCap,
                           b.w - 160.f),
                       kCap, c.textMuted);
            (void)pill(r, b.right() - 86.f, b.y + 20.f, "Refus\xC3\xA9", c.error, false, dark);
            y += 78.f;
        }
        r.popClip();
        if (maxScroll_ > 0.f) {   // la barre de defilement, discrete
            const float th = std::max(30.f, body_.h * body_.h / (body_.h + maxScroll_));
            const float ty = body_.y + (body_.h - th) * (scroll_ / maxScroll_);
            r.fillRoundedRect({panel_.right() - 8.f, ty, 4.f, th}, c.scrollbar, 2.f);
        }
        // ---- le pied
        r.fillRect({panel_.x, panel_.bottom() - 60.f, panel_.w, 1.f}, c.border);
        ui::drawIcon(r, ui::Icon::Lock, {panel_.x + 22.f, panel_.bottom() - 38.f, 14.f, 14.f}, c.textMuted);
        const std::string foot = status_.empty() ? updates::lastFetch() : status_;
        r.drawText({panel_.x + 42.f, panel_.bottom() - 39.f},
                   fit(r, foot.empty() ? std::string("Un fichier joint est gard\xC3\xA9 30 jours, puis il faut le redemander.") : foot, kCap,
                       remove_->bounds().x - panel_.x - 56.f),
                   kCap, c.textMuted);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && closeBox_.contains(d->pos)) {
            if (close_) close_();
            return ui::EventResult::Consumed;
        }
        if (const auto* wh = std::get_if<ui::MouseWheel>(&ev); wh && body_.contains(wh->pos) && maxScroll_ > 0.f) {
            scroll_ = std::clamp(scroll_ - wh->dy * 40.f, 0.f, maxScroll_);
            invalidateLayout();
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    static constexpr float kCardH = 150.f;
    struct Card {
        std::string id;
        ui::Button* save{nullptr};
        ui::Button* install{nullptr};
        float       top{0.f};
    };

    void reload() {
        const auto n = updates::notice(key_);
        if (!n) return;
        title_ = n->title;
        if (auto d = updates::dataOf(n->data)) data_ = std::move(*d);
        lines_ = wrap(data_.text, kBodyF, 700.f, 12);
        textH_ = static_cast<float>(lines_.size()) * (ui::lineHeight(kBodyF) + 4.f);
        invalidateLayout();
    }

    float contentHeight(float w) const {
        (void)w;
        float h = 20.f + textH_;
        std::size_t ok = 0, bad = 0;
        for (const auto& f : data_.files) (f.refused.empty() ? ok : bad) += 1;
        if (ok) h += 44.f + static_cast<float>(ok) * (kCardH + 12.f);
        if (data_.action == "admin") h += 34.f + 76.f;
        h += 66.f + static_cast<float>(bad) * 78.f + 12.f;
        return h;
    }

    void paintCard(const ui::PaintContext& ctx, const updates::NoticeFile& f, const gfx::Rect& b, bool busy) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const bool dark = ctx.theme.isDark();
        r.fillRoundedRect(b, c.panelBg, 14.f);
        r.strokeRect(b, c.border, 1.5f);
        fileTile(r, {b.x + 18.f, b.y + 16.f, 52.f, 52.f}, data_.action == "admin" || f.kind == "admin" ? violet() : teal(),
                 data_.action == "admin" || f.kind == "admin" ? ui::Icon::Lock : ui::Icon::Import);
        const float nx = b.x + 84.f;
        float right = b.right() - 18.f;
        for (const auto& card : cards_)
            if (card.id == f.id && card.save) right = card.save->bounds().x - 12.f;
        r.drawText({nx, b.y + 20.f}, fit(r, f.name, kH3, right - nx), gfx::semibold(kH3), c.text);
        std::string cap = filedl::sizeText(f.size);
        if (!f.version.empty()) cap += data_.action == "admin" || f.kind == "admin" ? " \xC2\xB7 l'Admin " + f.version : " \xC2\xB7 version " + f.version;
        if (f.name.size() > 4 && f.name.compare(f.name.size() - 4, 4, ".exe") == 0) cap += " \xC2\xB7 installateur Windows";
        r.drawText({nx, b.y + 44.f}, fit(r, cap, kCap, right - nx), kCap, c.textMuted);
        // les pastilles : seulement ce qui est vrai
        const bool hashOk = f.hashOk && filedl::presentCopy(wantOf(f.id, f.name, f.size, f.sha256));
        float px = b.x + 18.f;
        const float py = b.y + 80.f;
        if (f.manifestOk) px += pill(r, px, py, "Sign\xC3\xA9 par l'\xC3\xA9" "diteur", c.ok, true, dark) + 8.f;
        if (hashOk) px += pill(r, px, py, "Empreinte v\xC3\xA9rifi\xC3\xA9" "e", c.ok, true, dark) + 8.f;
        (void)pill(r, px, py, "re\xC3\xA7u du service XPG \xC2\xB7 chiffr\xC3\xA9", c.textMuted, false, dark);
        // le SHA-256 court (ou le telechargement en cours)
        const gfx::Rect sha{b.x + 1.5f, b.bottom() - 36.f, b.w - 3.f, 34.5f};
        r.fillRect(sha, c.rowAltBg);
        r.fillRect({sha.x, sha.y, sha.w, 1.f}, c.border);
        if (busy) {
            const gfx::Rect bar{sha.x + 16.f, sha.y + 14.f, sha.w - 160.f, 6.f};
            r.fillRoundedRect(bar, c.border, 3.f);
            r.fillRoundedRect({bar.x, bar.y, std::max(6.f, bar.w * std::clamp(frac_, 0.f, 1.f)), bar.h}, teal(), 3.f);
            r.drawText({bar.right() + 12.f, sha.y + (sha.h - r.lineHeight(kCap)) / 2.f},
                       "T\xC3\xA9l\xC3\xA9" "chargement " + std::to_string(static_cast<int>(frac_ * 100.f)) + " %", kCap, c.textMuted);
        } else {
            const float ly = sha.y + (sha.h - r.lineHeight(kCap)) / 2.f;
            r.drawText({sha.x + 16.f, ly}, "SHA-256", kCap, c.textMuted);
            r.drawText({sha.x + 84.f, ly}, filedl::shortHash(f.sha256), kCap, c.text);
            const std::string same = hashOk ? std::string("la m\xC3\xAAme que celle publi\xC3\xA9" "e par l'\xC3\xA9" "diteur \xE2\x9C\x93")
                                            : std::string("v\xC3\xA9rifi\xC3\xA9" "e au t\xC3\xA9l\xC3\xA9" "chargement");
            r.drawText({sha.right() - 16.f - r.measure(same, kCap).width, ly}, same, kCap, hashOk ? c.ok : c.textMuted);
        }
    }

    App&                     app_;
    std::string              key_, title_, status_;
    updates::NoticeData      data_;
    std::vector<std::string> lines_;
    float                    textH_{0.f};
    std::vector<Card>        cards_;
    ui::Button*              remove_{nullptr};
    ui::Button*              close_b_{nullptr};
    std::function<void()>    close_;
    gfx::Rect                panel_{}, closeBox_{}, body_{};
    float                    scroll_{0.f}, maxScroll_{0.f}, frac_{0.f};
    std::uint64_t            seenRev_{0};
    std::string              seenFetch_;
    core::ConnectionScope    links_;
};

class MessageWindow final : public menu::WidgetMenu {
public:
    MessageWindow(App& app, std::string key) : menu::WidgetMenu("dialog.message"), app_(app), key_(std::move(key)) {}
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return "Message de l'\xC3\xA9" "diteur"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
            fermer();
            return ui::EventResult::Consumed;
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }
    void Update(const menu::FrameContext& f) override {
        menu::WidgetMenu::Update(f);
        if (body_) body_->poll();
    }
    void fermer() {
        if (done_) return;
        done_ = true;
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
    }

protected:
    core::Status buildUi() override {
        auto body = std::make_unique<MessageBody>(app_, key_, [this] { fermer(); });
        body_ = body.get();
        setRoot(std::move(body));
        return core::ok();
    }

private:
    App&         app_;
    std::string  key_;
    MessageBody* body_{nullptr};
    bool         done_{false};
};

// ============================================================ la fenetre de la mise a jour (planche 06) ==
class UpdateBody final : public ui::Widget {
public:
    UpdateBody(App& app, std::function<void()> close) : ui::Widget("dialog.maj"), app_(app), close_(std::move(close)) {
        primary_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("T\xC3\xA9l\xC3\xA9" "charger et installer", "dialog.maj.installer")));
        primary_->setIcon(ui::Icon::Import);
        primary_->setStyle(ui::Button::Style::Primary);
        later_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Plus tard", "dialog.maj.plus-tard")));
        notes_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Voir les nouveaut\xC3\xA9s", "dialog.maj.nouveautes")));
        notes_->setStyle(ui::Button::Style::Flat);
        links_ += primary_->clicked->connect([this] {
            const auto in = updates::install();
            if (installing(in.phase)) {   // Annuler
                updates::cancelInstall();
                return;
            }
            if (in.phase == updates::Phase::Done || in.phase == updates::Phase::Cancelled || in.phase == updates::Phase::Failed) {
                if (in.phase == updates::Phase::Done || in.phase == updates::Phase::Cancelled) {
                    if (close_) close_();
                    return;
                }
            }
            const auto o = updates::available();
            (void)updates::beginInstall(app_, o && o->adminPc);
            refresh();
        });
        links_ += later_->clicked->connect([this] {
            updates::later();
            if (close_) close_();
        });
        links_ += notes_->clicked->connect([this] {
            const auto o = updates::available();
            if (!o) return;
            std::string text;
            for (const auto& n : o->offer.notes) text += "\xE2\x80\xA2 " + n + "\n";
            if (text.empty()) text = "L'\xC3\xA9" "diteur n'a pas \xC3\xA9" "crit de notes pour cette version.";
            auto dlg = std::make_unique<MessageDialog>("Les nouveaut\xC3\xA9s de la " + o->offer.version, text, MessageDialog::Icon::Info);
            dlg->setCancelLabel("Fermer");
            dlg->setParagraphWidth(620.f);
            app_.menus().ShowDialog(std::move(dlg), nullptr);
        });
        refresh();
    }

    void poll() {
        const auto in = updates::install();
        const std::string sig = std::to_string(static_cast<int>(in.phase)) + "|" + std::to_string(static_cast<int>(in.fraction * 200.f)) + "|" +
                                std::to_string(in.secondsLeft) + "|" + in.message + "|" + std::to_string(notices::revision());
        if (sig != seen_) {
            seen_ = sig;
            refresh();
        }
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = 560.f;
        const float h = std::min(std::max(300.f, r.h - 40.f), wantH_);
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        closeBox_ = {panel_.right() - 34.f, panel_.y + 12.f, 24.f, 24.f};
        ui::dialogframe::keep(*this, closeBox_);
        const float by = panel_.bottom() - 48.f;
        const float pw = std::max(150.f, primary_->sizeHint().preferred.w);
        primary_->setBounds({panel_.x + 20.f, by, pw, 32.f});
        const float lw = std::max(100.f, later_->sizeHint().preferred.w);
        later_->setBounds({primary_->bounds().right() + 8.f, by, lw, 32.f});
        const float nw = std::max(150.f, notes_->sizeHint().preferred.w);
        notes_->setBounds({panel_.right() - 20.f - nw, by, nw, 32.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const bool dark = ctx.theme.isDark();
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, offer_ && offer_->adminPc ? violet() : c.borderStrong, offer_ && offer_->adminPc ? 2.f : 1.f);
        r.drawText({closeBox_.x + 7.f, closeBox_.y + 2.f}, "\xC3\x97", kTitleF, c.textMuted);
        const float x = panel_.x + 24.f, w = panel_.w - 48.f;
        if (!offer_) {
            r.drawText({x, panel_.y + 24.f}, "Aucune mise \xC3\xA0 jour", kTitleF, c.text);
            r.drawText({x, panel_.y + 56.f}, std::string("Ce programme est \xC3\xA0 jour (") + XPG_ANALYZER_VERSION + ").", kBodyF, c.textMuted);
            return;
        }
        const auto& o = *offer_;
        if (installing(in_.phase) || in_.phase == updates::Phase::Done || in_.phase == updates::Phase::Failed || in_.phase == updates::Phase::Cancelled) {
            paintInstall(ctx, x, w);
            return;
        }
        // ---- l'en-tete
        fileTile(r, {x, panel_.y + 20.f, 44.f, 44.f}, teal(), ui::Icon::Assistant);
        const float tx = x + 58.f;
        r.drawText({tx, panel_.y + 18.f}, fit(r, "Mise \xC3\xA0 jour " + o.offer.version + " disponible", kTitleF, w - 200.f), kTitleF, c.text);
        const std::string sub = "De l'\xC3\xA9" "diteur" + (o.offer.published ? " \xC2\xB7 " + whenText(o.offer.published) : std::string());
        const float sy = panel_.y + 18.f + r.lineHeight(kTitleF) + 6.f;
        r.drawText({tx, sy + 3.f}, sub, kCap, c.textMuted);
        (void)pill(r, tx + r.measure(sub, kCap).width + 8.f, sy, "sign\xC3\xA9" "e", c.ok, true, dark);
        if (o.adminPc) {
            const float pw = r.measure("pour les admins", kTiny).width + 16.f;
            (void)pill(r, closeBox_.x - 8.f - pw, panel_.y + 22.f, "pour les admins", violet(), false, dark);
        } else {
            const std::string me = core::productName() + " " + XPG_ANALYZER_VERSION;
            r.drawText({closeBox_.x - 10.f - r.measure(me, kCap).width, panel_.y + 24.f}, me, kCap, c.textMuted);
        }
        float y = panel_.y + 84.f;
        // ---- l'Admin de ce PC n'est plus a jour
        if (const std::string line = updates::adminLine(o); !line.empty()) {
            const auto t = wrap(line + ". Les 2 installateurs sont joints : les Studios, et l'Admin.", kCap, w - 44.f, 3);
            const float h = 16.f + static_cast<float>(t.size()) * 18.f;
            r.fillRoundedRect({x, y, w, h}, tint(c.warning, dark ? 40 : 24), 8.f);
            ui::drawIcon(r, ui::Icon::Warning, {x + 10.f, y + (h - 16.f) / 2.f, 16.f, 16.f}, c.warning);
            for (std::size_t i = 0; i < t.size(); ++i) r.drawText({x + 34.f, y + 8.f + static_cast<float>(i) * 18.f}, t[i], kCap, c.text);
            y += h + 14.f;
        }
        // ---- les nouveautes
        if (!o.offer.notes.empty()) {
            overline(r, x, y, "Les nouveaut\xC3\xA9s", c.textMuted);
            y += 22.f;
            for (std::size_t i = 0; i < o.offer.notes.size() && i < 5; ++i) {
                ui::drawIcon(r, ui::Icon::Ok, {x, y + 2.f, 14.f, 14.f}, c.ok);
                r.drawText({x + 22.f, y}, fit(r, o.offer.notes[i], kCap, w - 24.f), kCap, c.text);
                y += 22.f;
            }
            if (o.offer.notes.size() > 5) {
                r.drawText({x + 22.f, y}, "+ " + std::to_string(o.offer.notes.size() - 5) + " autres (Voir les nouveaut\xC3\xA9s)", kCap, c.textMuted);
                y += 22.f;
            }
            y += 10.f;
        }
        // ---- les fichiers
        const auto files = o.files();
        overline(r, x, y, std::to_string(files.size()) + (files.size() > 1 ? " fichiers joints" : " fichier joint"), c.textMuted);
        y += 22.f;
        bool allSigned = !files.empty(), allHashed = !files.empty();
        for (const auto& f : files) {
            const bool admin = f.kind == "admin";
            const gfx::Rect row{x, y, w, 56.f};
            r.fillRoundedRect(row, admin ? tint(violet(), dark ? 30 : 10) : c.rowAltBg, 10.f);
            r.strokeRect(row, admin ? tint(violet(), 90) : c.border, 1.f);
            fileTile(r, {row.x + 10.f, row.y + 9.f, 38.f, 38.f}, admin ? violet() : teal(), admin ? ui::Icon::Lock : ui::Icon::Import);
            const bool refused = !f.refused.empty();
            const bool hashed = filedl::presentCopy(wantOf(f.id, f.name, f.size, f.sha256));
            allSigned = allSigned && f.manifestOk;
            allHashed = allHashed && hashed;
            r.drawText({row.x + 60.f, row.y + 9.f}, fit(r, f.name, kCap, row.w - 150.f), gfx::semibold(kCap), c.text);
            const std::string what = admin ? "l'Admin" : (files.size() > 1 ? "les Studios" : "API Studio et IHM Studio");
            r.drawText({row.x + 60.f, row.y + 30.f}, filedl::sizeText(f.size) + " \xC2\xB7 " + what, kTiny, c.textMuted);
            if (refused) (void)pill(r, row.right() - 90.f, row.y + 17.f, "refus\xC3\xA9", c.error, false, dark);
            else if (f.manifestOk) (void)pill(r, row.right() - 80.f, row.y + 17.f, "sign\xC3\xA9", c.ok, true, dark);
            y += 62.f;
        }
        float px = x;
        const bool plural = files.size() > 1;
        if (allSigned) px += pill(r, px, y + 2.f, plural ? "Sign\xC3\xA9s par l'\xC3\xA9" "diteur" : "Sign\xC3\xA9 par l'\xC3\xA9" "diteur", c.ok, true, dark) + 8.f;
        if (allHashed)
            (void)pill(r, px, y + 2.f, plural ? "Empreintes v\xC3\xA9rifi\xC3\xA9" "es" : "Empreinte v\xC3\xA9rifi\xC3\xA9" "e (SHA-256)", c.ok, true, dark);
        if (allSigned || allHashed) y += 34.f;
        // ---- la note
        {
            const std::string note = o.adminPc ? "Ta cl\xC3\xA9 d'admin et ton certificat restent sur ce PC : la mise \xC3\xA0 jour ne les touche pas."
                                               : "Avant d'installer, ton projet est enregistr\xC3\xA9. Les Studios se ferment, puis se rouvrent sur ton projet.";
            const auto t = wrap(note, kCap, w - 44.f, 3);
            const float h = 16.f + static_cast<float>(t.size()) * 18.f;
            const gfx::Color tone = o.adminPc ? violet() : c.info;
            r.fillRoundedRect({x, y + 6.f, w, h}, tint(tone, dark ? 40 : 22), 8.f);
            ui::drawIcon(r, o.adminPc ? ui::Icon::Lock : ui::Icon::Save, {x + 10.f, y + 6.f + (h - 16.f) / 2.f, 16.f, 16.f}, tone);
            for (std::size_t i = 0; i < t.size(); ++i) r.drawText({x + 34.f, y + 14.f + static_cast<float>(i) * 18.f}, t[i], kCap, c.text);
            y += h + 16.f;
        }
        bodyBottom_ = y;
        r.fillRect({panel_.x, panel_.bottom() - 64.f, panel_.w, 1.f}, c.border);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && closeBox_.contains(d->pos)) {
            if (!installing(updates::install().phase)) updates::later();
            if (close_) close_();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    static bool installing(updates::Phase p) {
        return p == updates::Phase::Downloading || p == updates::Phase::Saving || p == updates::Phase::Countdown || p == updates::Phase::Launching;
    }

    void refresh() {
        offer_ = updates::available();
        in_ = updates::install();
        const bool busy = installing(in_.phase);
        const bool ended = in_.phase == updates::Phase::Done || in_.phase == updates::Phase::Failed || in_.phase == updates::Phase::Cancelled;
        if (busy) primary_->setText("Annuler");
        else if (in_.phase == updates::Phase::Failed) primary_->setText("R\xC3\xA9" "essayer");
        else if (ended) primary_->setText("Fermer");
        else primary_->setText(offer_ && offer_->adminPc ? "Tout installer" : "T\xC3\xA9l\xC3\xA9" "charger et installer");
        primary_->setStyle(busy ? ui::Button::Style::Default : ui::Button::Style::Primary);
        primary_->setEnabled(offer_.has_value() && in_.phase != updates::Phase::Launching);
        later_->setVisibility(shown(!busy && !ended && offer_.has_value()));
        notes_->setVisibility(shown(!busy && !ended && offer_ && !offer_->offer.notes.empty()));
        // la hauteur voulue
        float h = 84.f;
        if (offer_) {
            if (busy || ended) h = 220.f;
            else {
                if (!updates::adminLine(*offer_).empty()) h += 70.f;
                const std::size_t n = std::min<std::size_t>(offer_->offer.notes.size(), 5) + (offer_->offer.notes.size() > 5 ? 1 : 0);
                if (n) h += 22.f + 22.f * static_cast<float>(n) + 10.f;
                h += 22.f + 62.f * static_cast<float>(offer_->files().size()) + 34.f + 70.f;
                h += 70.f;
            }
        } else {
            h = 180.f;
        }
        wantH_ = h;
        invalidateLayout();
        invalidate();
    }

    void paintInstall(const ui::PaintContext& ctx, float x, float w) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        fileTile(r, {x, panel_.y + 20.f, 36.f, 36.f}, teal(), ui::Icon::Import);
        r.drawText({x + 50.f, panel_.y + 18.f}, "Installation de la " + in_.version, kH3, c.text);
        std::string sub;
        switch (in_.phase) {
            case updates::Phase::Downloading:
                sub = "T\xC3\xA9l\xC3\xA9" "chargement " + std::to_string(static_cast<int>(in_.fraction * 100.f)) + " % \xC2\xB7 " + in_.name;
                break;
            case updates::Phase::Saving: sub = "Enregistrement du projet\xE2\x80\xA6"; break;
            case updates::Phase::Countdown:
                sub = core::productName() + " se ferme dans " + std::to_string(std::max(1, in_.secondsLeft)) + " s, puis se rouvre";
                break;
            case updates::Phase::Launching: sub = (in_.all ? "Installation de l'Admin, puis des Studios\xE2\x80\xA6" : "Lancement de l'installateur\xE2\x80\xA6"); break;
            default: sub = in_.message; break;
        }
        const auto lines = wrap(sub, kCap, w - 60.f, 4);
        float y = panel_.y + 18.f + r.lineHeight(kH3) + 4.f;
        const gfx::Color subInk = in_.phase == updates::Phase::Failed ? c.error : c.textMuted;
        for (const auto& l : lines) {
            r.drawText({x + 50.f, y}, l, kCap, subInk);
            y += 17.f;
        }
        y = std::max(y + 10.f, panel_.y + 76.f);
        const gfx::Rect bar{x, y, w, 8.f};
        r.fillRoundedRect(bar, c.border, 4.f);
        const float f = in_.phase == updates::Phase::Downloading ? in_.fraction : (in_.downloaded ? 1.f : 0.f);
        if (f > 0.f) r.fillRoundedRect({bar.x, bar.y, std::max(8.f, bar.w * std::clamp(f, 0.f, 1.f)), bar.h}, teal(), 4.f);
        y += 22.f;
        const auto check = [&](int i, bool on, const std::string& text) {
            const float cx = x + static_cast<float>(i % 2) * (w / 2.f), cy = y + static_cast<float>(i / 2) * 24.f;
            ui::drawIcon(r, on ? ui::Icon::Ok : ui::Icon::History, {cx, cy + 1.f, 14.f, 14.f}, on ? c.ok : c.textMuted);
            r.drawText({cx + 22.f, cy}, fit(r, text, kCap, w / 2.f - 30.f), kCap, on ? c.text : c.textMuted);
        };
        check(0, in_.downloaded, "T\xC3\xA9l\xC3\xA9" "charg\xC3\xA9 \xC2\xB7 " + filedl::sizeText(in_.total));
        check(1, in_.signature, "Signature de l'\xC3\xA9" "diteur");
        check(2, in_.hash, "Empreinte SHA-256");
        check(3, in_.saved, in_.project.empty() ? std::string("Aucun projet \xC3\xA0 enregistrer") : in_.project + " enregistr\xC3\xA9");
    }

    App&                         app_;
    std::function<void()>        close_;
    ui::Button*                  primary_{nullptr};
    ui::Button*                  later_{nullptr};
    ui::Button*                  notes_{nullptr};
    std::optional<updates::Offer> offer_;
    updates::Install             in_;
    gfx::Rect                    panel_{}, closeBox_{};
    float                        wantH_{400.f}, bodyBottom_{0.f};
    std::string                  seen_;
    core::ConnectionScope        links_;
};

class UpdateWindow final : public menu::WidgetMenu {
public:
    explicit UpdateWindow(App& app) : menu::WidgetMenu("dialog.maj"), app_(app) {}
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return "Mise \xC3\xA0 jour"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
            const auto p = updates::install().phase;
            if (p != updates::Phase::Downloading && p != updates::Phase::Countdown && p != updates::Phase::Launching) updates::later();
            fermer();
            return ui::EventResult::Consumed;
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }
    void Update(const menu::FrameContext& f) override {
        menu::WidgetMenu::Update(f);
        if (body_) body_->poll();
    }
    void fermer() {
        if (done_) return;
        done_ = true;
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
    }

protected:
    core::Status buildUi() override {
        auto body = std::make_unique<UpdateBody>(app_, [this] { fermer(); });
        body_ = body.get();
        setRoot(std::move(body));
        return core::ok();
    }

private:
    App&        app_;
    UpdateBody* body_{nullptr};
    bool        done_{false};
};

// ============================================================ le toast ==
struct ToastGeometry {
    gfx::Rect box, view, close;
};
gfx::Size g_toastSurface{};   // la taille de la fenetre au dernier dessin du toast (ses clics)
ToastGeometry toastGeometry(gfx::Size surface) {
    ToastGeometry g;
    const float w = 420.f, h = 68.f;
    g.box = {surface.w - w - 20.f, surface.h - h - 44.f, w, h};
    g.view = {g.box.right() - 96.f, g.box.y + (h - 30.f) / 2.f, 58.f, 30.f};
    g.close = {g.box.right() - 30.f, g.box.y + 8.f, 20.f, 20.f};
    return g;
}

} // namespace

namespace licence {

bool openNoticeAction(App& app, const std::string& action) {
    if (action.rfind("fichiers:", 0) == 0) {
        const std::string key = action.substr(9);
        const auto n = updates::notice(key);
        if (!n) return false;
        (void)notices::markRead(n->id);
        app.menus().ShowDialog(std::make_unique<MessageWindow>(app, key), nullptr);
        return true;
    }
    if (action.rfind("maj:", 0) == 0) return showUpdateWindow(app);
    if (action.rfind("detail:", 0) == 0) return showOfferChange(app, action.substr(7));
    if (action.rfind("licence:", 0) == 0) {
        showLicenceWindow(app, notices::parseAction(action).tab);
        return true;
    }
    return false;
}

bool showUpdateWindow(App& app) {
    if (!updates::available() && updates::install().phase == updates::Phase::Idle) return false;
    updates::dismissToast();
    for (const auto& n : notices::list())
        if (n.key.rfind("maj:", 0) == 0 && !n.read) (void)notices::markRead(n.id);
    app.menus().ShowDialog(std::make_unique<UpdateWindow>(app), nullptr);
    return true;
}

} // namespace licence

namespace updates {

void paintToast(gfx::IRenderer& r, const ui::Theme& theme, gfx::Size surface) {
    const auto t = toast();
    g_toastSurface = surface;
    if (!t || surface.w < 500.f || surface.h < 200.f) return;
    (void)theme;
    const auto g = toastGeometry(surface);
    const gfx::Color bg{15, 23, 42, 246}, ink{255, 255, 255, 255}, sub{148, 163, 184, 255};
    r.fillRoundedRect(g.box, bg, 14.f);
    fileTile(r, {g.box.x + 14.f, g.box.y + 15.f, 38.f, 38.f}, teal(), ui::Icon::Assistant);
    const float tx = g.box.x + 64.f, tw = g.view.x - tx - 10.f;
    r.drawText({tx, g.box.y + 14.f}, fit(r, t->title, kBodyF, tw), gfx::semibold(kBodyF), ink);
    r.drawText({tx, g.box.y + 38.f}, fit(r, t->text, kTiny, tw), kTiny, sub);
    r.fillRoundedRect(g.view, gfx::Color{255, 255, 255, 30}, 8.f);
    r.drawText({g.view.x + (g.view.w - r.measure("Voir", kCap).width) / 2.f, g.view.y + (g.view.h - r.lineHeight(kCap)) / 2.f}, "Voir", kCap, ink);
    r.drawText({g.close.x + 5.f, g.close.y}, "\xC3\x97", kBodyF, sub);
}

ui::EventResult handleToast(App& app, const ui::InputEvent& ev) {
    const auto* d = std::get_if<ui::MouseDown>(&ev);
    if (!d || !toast()) return ui::EventResult::Ignored;
    const gfx::Size surface = g_toastSurface;
    if (surface.w < 500.f || surface.h < 200.f) return ui::EventResult::Ignored;
    const auto g = toastGeometry(surface);
    if (!g.box.contains(d->pos)) return ui::EventResult::Ignored;
    if (g.close.contains(d->pos)) {
        dismissToast();
        later();   // la croix : comme << Plus tard >> (la pastille reste)
        return ui::EventResult::Consumed;
    }
    dismissToast();
    (void)licence::showUpdateWindow(app);
    return ui::EventResult::Consumed;
}

} // namespace updates

} // namespace app
