// =============================================================================
//  license_admin/gui/DerogationDialog.cpp - 1.47.0 : LE DIALOGUE << Derogation
//  << Gerer le service >> >> (l'admin+ ; docs/ADMINS.md, par. 11) ; 1.52.0 : LA
//  DEROGATION << tout >>, ACTIVEE PAR UN CODE - ET LE DIALOGUE DU CODE (XPG Admin)
// -----------------------------------------------------------------------------
//  L'ADMIN+ (makeDerogationDialog). Il donne a UN admin, pour un temps COURT, de
//  voir tout comme lui : 30 min, 1 h, 4 h, 24 h, 72 h ou << Autre duree >> (des
//  heures et des minutes, de 10 min a 72 h ; jamais << permanente >>). Il dit ce
//  qu'elle ouvre et ce qu'elle n'ouvre jamais (ce qui demande la cle de l'editeur ;
//  brancher Stripe : sa cle Stripe ne se partage pas) ; que l'admin se connecte a
//  Cloudflare avec SON propre compte, jamais un jeton partage ; << Elle commence
//  quand il tape le code. Le code est valable 1 h. >> << Accorder >> :
//  Workspace::grantDerogation (runSlow). Puis LE CODE, en grand (<< 4821 7730 >>),
//  retire de la cle de l'editeur (Workspace::derogationCode : jamais garde, jamais
//  au journal), << Donne-lui ce code (de vive voix, au telephone) : il le tape dans
//  XPG Admin. Valable jusqu'a 15:42. >>, << Copier >>. Une derogation en cours ou en
//  attente : << Revoquer maintenant >> (Workspace::revokeDerogation).
//  Ouvert par Service en ligne (la carte << La derogation >>) et par la fiche d'un
//  admin (Administrateurs : PageAdmins.cpp, un branchement).
//
//  XPG ADMIN (makeDerogationCodeDialog). La derogation recue (le regard des 15 s,
//  Shell.cpp) : un champ (les espaces, les tirets acceptes), << Activer >>
//  (Workspace::activateDerogation) ; le refus du service dit ici (les essais qui
//  restent, trop tard, annulee) ; reussi : << Derogation active : tu vois tout comme
//  l'admin+ pendant 4 h (jusqu'a 19:42). >> - le menu suit aussitot.
// =============================================================================
#include "AccountDerogation.hpp"   // 1.52.0 : la carte de Mon compte (implementee ici)
#include "AdminsKit.hpp"
#include "Page.hpp"

#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace {

using namespace kit;

constexpr FontId kF12{12}, kF13{13}, kF14{14};
constexpr int kOther = 5;   // << Autre duree >> (l'index du bouton radio)

class DerogationDialog;

class DerogationBody final : public ui::Widget {
public:
    DerogationBody(DerogationDialog& owner, ShellHost host, std::string adminKey, std::function<void()> done);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    [[nodiscard]] Workspace& ws() const { return host_.workspace(); }
    // La duree choisie (secondes) ; 0 : << Autre duree >> illisible ou hors de 10 min a 72 h (`why_` le dit).
    [[nodiscard]] Seconds chosenSeconds(std::string* why) const;
    void grant();
    void revoke();
    void copy();
    void sync();
    void closeMe();
    void paintCode(const ui::PaintContext& ctx);

    DerogationDialog&                owner_;
    ShellHost                        host_;
    std::function<void()>            done_;
    ui::DropDown*                    admin_{nullptr};
    std::shared_ptr<ui::RadioGroup>  choice_{std::make_shared<ui::RadioGroup>()};
    ui::RadioButton*                 radio_[6]{};
    ui::InputText*                   hoursField_{nullptr};
    ui::InputText*                   minutesField_{nullptr};
    ui::Button*                      cancel_{nullptr};
    ui::Button*                      ok_{nullptr};
    ui::Button*                      revoke_{nullptr};
    ui::Button*                      copy_{nullptr};
    std::vector<std::string>         keys_;    // les cles des admins de la liste deroulante
    std::string                      error_;
    bool                             running_{false};
    // 1.52.0 : accordee - le code a montrer (retire de la cle ; garde le temps du dialogue seulement)
    Derogation                       sent_;
    std::string                      code_;
    bool                             copied_{false};
    Frame                            frame_;
    float                            textTop_{0.f}, stateY_{0.f}, otherY_{0.f};
    Rect                             codeBox_{};
    core::ConnectionScope            links_;
    std::shared_ptr<bool>            alive_{std::make_shared<bool>(true)};
};

class DerogationDialog final : public Dialog {
public:
    DerogationDialog(ShellHost host, std::string adminKey, std::function<void()> done)
        : Dialog("dialog.derogation"), host_(std::move(host)), adminKey_(std::move(adminKey)), done_(std::move(done)) {}
    [[nodiscard]] std::string title() const override { return "D\xC3\xA9rogation"; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<DerogationBody>(*this, host_, adminKey_, done_));
        return core::ok();
    }
    void onTick() override {
        if (ui::Widget* r = widgetRoot()) r->invalidate();   // le compte a rebours
    }

private:
    ShellHost             host_;
    std::string           adminKey_;
    std::function<void()> done_;
};

const char* const kWhat =
    "Pour un temps court, l'admin choisi voit tout comme toi : les param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9, les services, les administrateurs, le journal, "
    "les versions ; il g\xC3\xA8re le service (le mettre en ligne ou \xC3\xA0 jour, son adresse, ses adresses de secours). Jamais ce qui demande ta cl\xC3\xA9 "
    "d'\xC3\xA9" "diteur (ajouter ou retirer un admin, les invitations, publier une version, certifier, la sauvegarde, la restauration), ni Brancher "
    "Stripe (ta cl\xC3\xA9 Stripe ne se partage pas).";
const char* const kStart = "Elle commence quand il tape le code. Le code est valable 1 h.";
const char* const kCloudflare =
    "Cloudflare : il se connecte avec SON propre compte, sur son PC (invite-le comme membre de ton compte Cloudflare : tableau de bord \xE2\x80\xBA "
    "G\xC3\xA9rer le compte \xE2\x80\xBA Membres). Jamais un jeton partag\xC3\xA9, jamais ton mot de passe.";
const char* const kService =
    "Le service la tient \xC3\xA0 SON heure : \xC3\xA0 la fin, elle s'arr\xC3\xAAte d'elle-m\xC3\xAAme ; tu peux la r\xC3\xA9voquer avant. Chaque usage est "
    "\xC3\xA9" "crit au journal et tu es pr\xC3\xA9venu tout de suite. Une nouvelle d\xC3\xA9rogation remplace celle en cours.";

DerogationBody::DerogationBody(DerogationDialog& owner, ShellHost host, std::string adminKey, std::function<void()> done)
    : ui::Widget("derogation.corps"), owner_(owner), host_(std::move(host)), done_(std::move(done)) {
    admin_ = &adopt(*this, std::make_unique<ui::DropDown>("derogation.admin"));
    std::vector<ui::DropDown::Item> items;
    int chosen = 0;
    if (const auto& l = ws().adminList())
        for (const auto& a : l->admins) {
            if (!a.active()) continue;
            if (a.key == adminKey) chosen = static_cast<int>(keys_.size());
            items.push_back({a.name + " \xC2\xB7 " + a.id, a.key});
            keys_.push_back(a.key);
        }
    admin_->setItems(std::move(items));
    if (!keys_.empty()) admin_->setSelectedIndex(chosen);
    // 1.52.0 : 30 min, 1 h, 4 h, 24 h, 72 h, Autre duree
    const char* const labels[] = {"30 min", "1 h", "4 h", "24 h", "72 h", "Autre dur\xC3\xA9" "e"};
    const char* const ids[] = {"derogation.duree.30min", "derogation.duree.1", "derogation.duree.4", "derogation.duree.24", "derogation.duree.72",
                               "derogation.duree.autre"};
    for (int i = 0; i < 6; ++i) radio_[i] = &adopt(*this, std::make_unique<ui::RadioButton>(labels[i], choice_, i, ids[i]));
    choice_->setValue(2);   // 4 h
    hoursField_ = &adopt(*this, std::make_unique<ui::InputText>("derogation.duree.heures"));
    hoursField_->setPlaceholder("2");
    hoursField_->setMaxLength(3);
    minutesField_ = &adopt(*this, std::make_unique<ui::InputText>("derogation.duree.minutes"));
    minutesField_->setPlaceholder("30");
    minutesField_->setMaxLength(3);
    cancel_ = &button(*this, "Fermer", "derogation.annuler");
    ok_ = &button(*this, "Accorder", "derogation.accorder", ui::Button::Style::Primary, ui::Icon::Lock);
    revoke_ = &button(*this, "R\xC3\xA9voquer maintenant", "derogation.revoquer", ui::Button::Style::Danger);
    copy_ = &button(*this, "Copier", "derogation.copier", ui::Button::Style::Default, ui::Icon::Copy);
    links_ += cancel_->clicked->connect([this] { closeMe(); });
    links_ += ok_->clicked->connect([this] { grant(); });
    links_ += revoke_->clicked->connect([this] { revoke(); });
    links_ += copy_->clicked->connect([this] { copy(); });
    // << Autre duree >> ouvre ses deux champs (la mise en page suit) ; un autre choix les referme
    links_ += choice_->valueChanged->connect([this](int) {
        error_.clear();
        sync();
        invalidateLayout();
        invalidate();
    });
    sync();
}

Seconds DerogationBody::chosenSeconds(std::string* why) const {
    const int v = std::clamp(choice_->value(), 0, kOther);
    if (v < kOther) return static_cast<Seconds>(kDerogationMinutes[v]) * 60;
    const auto number = [](const std::string& t, long& out) {
        std::string d;
        for (const char c : t)
            if (c != ' ') d += c;
        if (d.empty()) {
            out = 0;
            return true;
        }
        if (d.size() > 3 || d.find_first_not_of("0123456789") != std::string::npos) return false;
        out = std::atol(d.c_str());
        return true;
    };
    long h = 0, m = 0;
    if (!number(hoursField_->text(), h) || !number(minutesField_->text(), m)) {
        if (why) *why = "Autre dur\xC3\xA9" "e : des heures et des minutes en chiffres (par exemple 2 h et 30 min).";
        return 0;
    }
    if (m >= 60) {
        if (why) *why = "Autre dur\xC3\xA9" "e : les minutes vont de 0 \xC3\xA0 59.";
        return 0;
    }
    const Seconds s = static_cast<Seconds>(h) * 3600 + static_cast<Seconds>(m) * 60;
    if (s < kDerogationMinSeconds || s > kDerogationMaxSeconds) {
        if (why) *why = "Autre dur\xC3\xA9" "e : de 10 min \xC3\xA0 72 h (jamais plus, jamais \xC2\xAB permanente \xC2\xBB).";
        return 0;
    }
    return s;
}

void DerogationBody::sync() {
    const bool plus = ws().role() == Role::Plus;
    const auto& d = ws().derogation();
    const bool sent = !code_.empty();
    const bool live = d.activeAt(ws().now()) || d.pendingAt(ws().now());
    ok_->setEnabled(plus && !running_ && !keys_.empty());
    revoke_->setEnabled(plus && !running_ && live);
    show(revoke_, live);
    show(ok_, !sent);
    show(copy_, sent);
    show(admin_, !sent);
    for (auto* r : radio_) show(r, !sent);
    const bool other = !sent && choice_->value() == kOther;
    show(hoursField_, other);
    show(minutesField_, other);
    if (!plus) ok_->setTooltip("Seul l'admin+ accorde une d\xC3\xA9rogation.");
}

void DerogationBody::closeMe() {
    code_.assign(code_.size(), '0');   // le code ne reste pas en memoire plus que le dialogue
    code_.clear();
    if (done_) done_();
    owner_.close();
}

void DerogationBody::copy() {
    if (code_.empty()) return;
    ui::setClipboardText(formatDerogationCode(code_));
    copied_ = true;
    invalidate();
}

void DerogationBody::grant() {
    if (running_) return;
    const auto* item = admin_->selectedItem();
    if (!item) {
        error_ = "Choisis l'admin (la liste des admins n'a personne d'actif).";
        invalidate();
        return;
    }
    std::string why;
    const Seconds seconds = chosenSeconds(&why);
    if (seconds <= 0) {
        error_ = why;
        invalidate();
        return;
    }
    const std::string key = item->value;
    const Workspace* w = &ws();
    auto out = std::make_shared<Derogation>();
    std::weak_ptr<bool> alive = alive_;
    running_ = true;
    error_.clear();
    sync();
    const bool started = host_.runSlow && host_.runSlow("Service : d\xC3\xA9rogation", [w, key, seconds, out] { return w->grantDerogation(key, seconds, out.get()); },
                                                        [this, alive, out](const OpResult& r) {
                                                            if (alive.expired()) return;
                                                            running_ = false;
                                                            if (!r.ok) {
                                                                error_ = r.message;
                                                                sync();
                                                                invalidate();
                                                                return;
                                                            }
                                                            ws().adoptDerogation(*out);
                                                            // 1.52.0 : le code, retire de la cle (rien n'est garde) - montre ici
                                                            sent_ = *out;
                                                            code_ = ws().derogationCode(out->id);
                                                            if (code_.empty()) {
                                                                closeMe();
                                                                return;
                                                            }
                                                            sync();
                                                            invalidateLayout();
                                                            invalidate();
                                                        });
    if (!started) {
        running_ = false;
        error_ = "Un travail est d\xC3\xA9j\xC3\xA0 en cours : attends qu'il finisse, puis recommence.";
        sync();
    }
    invalidate();
}

void DerogationBody::revoke() {
    if (running_) return;
    const Workspace* w = &ws();
    auto out = std::make_shared<Derogation>();
    std::weak_ptr<bool> alive = alive_;
    running_ = true;
    error_.clear();
    sync();
    const bool started = host_.runSlow && host_.runSlow("Service : d\xC3\xA9rogation r\xC3\xA9voqu\xC3\xA9" "e", [w, out] { return w->revokeDerogation(out.get()); },
                                                        [this, alive, out](const OpResult& r) {
                                                            if (alive.expired()) return;
                                                            running_ = false;
                                                            if (!out->id.empty()) ws().adoptDerogation(*out);
                                                            if (!r.ok) error_ = r.message;
                                                            else if (!code_.empty()) {   // le code ne sert plus
                                                                code_.clear();
                                                                sent_ = Derogation{};
                                                                invalidateLayout();
                                                            }
                                                            sync();
                                                            invalidate();
                                                        });
    if (!started) {
        running_ = false;
        error_ = "Un travail est d\xC3\xA9j\xC3\xA0 en cours : attends qu'il finisse, puis recommence.";
        sync();
    }
    invalidate();
}

void DerogationBody::onLayout() {
    frame_.place(*this, 700.f, 680.f);
    const auto& p = frame_.panel;
    const float x = p.x + 24.f, w = p.w - 48.f;
    textTop_ = p.y + 92.f;
    const float by = p.bottom() - 54.f;
    const float ow = buttonWidth(*ok_, 130.f), cw = buttonWidth(*cancel_, 100.f), rw = buttonWidth(*revoke_, 170.f), pw = buttonWidth(*copy_, 110.f);
    ok_->setBounds({p.right() - 24.f - ow, by, ow, 34.f});
    cancel_->setBounds({(code_.empty() ? ok_->bounds().x : p.right() - 24.f) - 8.f - cw + (code_.empty() ? 0.f : 8.f), by, cw, 34.f});
    revoke_->setBounds({x, by, rw, 34.f});
    if (!code_.empty()) {
        // le code, en grand ; << Copier >> a sa droite
        codeBox_ = {x, textTop_ + 36.f, w, 92.f};
        copy_->setBounds({codeBox_.right() - 16.f - pw, codeBox_.y + (codeBox_.h - 34.f) * 0.5f, pw, 34.f});
        return;
    }
    float y = textTop_ + wrappedHeight(kWhat, kF14, w, 20.f) + 14.f;
    admin_->setBounds({x + 120.f, y, std::min(420.f, w - 120.f), 32.f});
    y += 44.f;
    float hx = x + 120.f;
    for (auto* r : radio_) {
        const float rww = std::max(70.f, r->sizeHint().preferred.w + 10.f);
        if (hx + rww > x + w) {
            hx = x + 120.f;
            y += 32.f;
        }
        r->setBounds({hx, y + 4.f, rww, 26.f});
        hx += rww + 8.f;
    }
    y += 38.f;
    otherY_ = y;
    if (choice_->value() == kOther) {
        hoursField_->setBounds({x + 120.f, y, 64.f, 30.f});
        minutesField_->setBounds({x + 120.f + 64.f + 34.f, y, 64.f, 30.f});
        y += 40.f;
    }
    stateY_ = y;
}

void DerogationBody::paintCode(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto& p = frame_.panel;
    const float x = p.x + 24.f, w = p.w - 48.f;
    const Seconds now = ws().now();
    drawTick(r, x, textTop_ + 4.f, c.ok);
    r.drawText({x + 24.f, textTop_}, fit(r, "D\xC3\xA9rogation accord\xC3\xA9" "e \xC3\xA0 " + sent_.adminName + " pour " + durationText(sent_.duration), gfx::semibold(FontId{17}), w - 24.f),
               gfx::semibold(FontId{17}), c.ok);
    r.fillRoundedRect(codeBox_, c.accent.withAlpha(16), 12.f);
    ui::shapes::strokeRoundedRect(r, codeBox_, c.accent, 12.f, 1.5f);
    r.drawText({codeBox_.x + 22.f, codeBox_.y + 12.f}, formatDerogationCode(code_), gfx::semibold(FontId{40}), c.text);
    r.drawText({codeBox_.x + 24.f, codeBox_.y + 66.f}, fit(r, copied_ ? "copi\xC3\xA9 \xC2\xB7 le code de la d\xC3\xA9rogation \xC2\xB7 8 chiffres" : "le code de la d\xC3\xA9rogation \xC2\xB7 8 chiffres",
                                                         kF12, copy_->bounds().x - codeBox_.x - 40.f),
               kF12, c.textMuted);
    float y = codeBox_.bottom() + 18.f;
    const std::string until = clockText(sent_.localTime(sent_.validUntil, now), now);
    y += drawWrapped(r, x, y, w, "Donne-lui ce code (de vive voix, au t\xC3\xA9l\xC3\xA9phone) : il le tape dans XPG Admin. Valable jusqu'\xC3\xA0 " + until + ".",
                     gfx::semibold(kF14), c.text, 21.f) +
         10.f;
    y += drawWrapped(r, x, y, w,
                     "Elle commence quand il tape le code : il voit alors tout comme toi pendant " + durationText(sent_.duration) +
                         ". 5 codes faux l'annulent. Tant qu'il attend, ce PC te remontre le code (Service en ligne, sa fiche dans Administrateurs) ; "
                         "quand il l'a tap\xC3\xA9, tu es pr\xC3\xA9venu.",
                     kF13, c.textMuted, 18.f) +
         12.f;
    const float hc = wrappedHeight(kCloudflare, kF13, w - 24.f, 18.f) + 18.f;
    r.fillRoundedRect({x, y, w, hc}, c.warning.withAlpha(30), 8.f);
    (void)drawWrapped(r, x + 12.f, y + 9.f, w - 24.f, kCloudflare, kF13, c.text, 18.f);
    y += hc + 12.f;
    if (running_) r.drawText({x, y}, "Le service r\xC3\xA9pond\xE2\x80\xA6", kF13, c.textMuted);
    else if (!error_.empty()) (void)drawWrapped(r, x, y, w, error_, kF13, c.error, 18.f, 3);
}

void DerogationBody::onPaint(const ui::PaintContext& ctx) {
    sync();
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto& p = frame_.panel;
    (void)frame_.paint(r, ctx.theme, "D\xC3\xA9rogation : tout, comme l'admin+", "\xC3\x80 un seul admin, pour un temps court : 72 h au plus, jamais permanente",
                       ui::Icon::Lock);
    if (!code_.empty()) {
        paintCode(ctx);
        return;
    }
    const float x = p.x + 24.f, w = p.w - 48.f;
    (void)drawWrapped(r, x, textTop_, w, kWhat, kF14, c.text, 20.f);
    r.drawText({x, admin_->bounds().y + 7.f}, "L'admin", gfx::semibold(kF14), c.text);
    r.drawText({x, radio_[0]->bounds().y + 3.f}, "Dur\xC3\xA9" "e", gfx::semibold(kF14), c.text);
    if (choice_->value() == kOther) {
        const Rect hb = hoursField_->bounds(), mb = minutesField_->bounds();
        r.drawText({hb.right() + 8.f, hb.y + 6.f}, "h", kF14, c.text);
        r.drawText({mb.right() + 8.f, mb.y + 6.f}, "min", kF14, c.text);
        r.drawText({mb.right() + 44.f, mb.y + 7.f}, fit(r, "de 10 min \xC3\xA0 72 h", kF13, p.right() - 24.f - (mb.right() + 44.f)), kF13, c.textMuted);
    }
    float y = stateY_;
    r.drawText({x, y}, fit(r, kStart, gfx::semibold(kF14), w), gfx::semibold(kF14), c.text);
    y += 28.f;
    // Cloudflare : son propre compte, jamais un jeton partage (un encadre ambre)
    const float hc = wrappedHeight(kCloudflare, kF13, w - 24.f, 18.f) + 18.f;
    r.fillRoundedRect({x, y, w, hc}, c.warning.withAlpha(30), 8.f);
    (void)drawWrapped(r, x + 12.f, y + 9.f, w - 24.f, kCloudflare, kF13, c.text, 18.f);
    y += hc + 12.f;
    y += drawWrapped(r, x, y, w, kService, kF13, c.textMuted, 18.f) + 10.f;   // (drawWrapped rend la hauteur)
    // la derogation en place (en attente : son code, retire de la cle)
    const auto& d = ws().derogation();
    const Seconds now = ws().now();
    if (d.exists()) {
        const bool active = d.activeAt(now), pending = d.pendingAt(now);
        std::string line;
        if (pending) {
            const std::string code = ws().derogationCode(d.id);
            line = "En attente du code : " + d.adminName + (code.empty() ? std::string() : " \xE2\x80\x94 " + formatDerogationCode(code)) + " (valable jusqu'\xC3\xA0 " +
                   clockText(d.localTime(d.validUntil, now), now) + ")";
        } else {
            line = std::string(active ? "En cours : " : "La derni\xC3\xA8re : ") + d.adminName + " \xE2\x80\x94 " + derogationText(d, now) +
                   (d.uses ? " \xC2\xB7 utilis\xC3\xA9" "e " + std::to_string(d.uses) + " fois" : std::string());
        }
        r.drawText({x, y}, fit(r, line, gfx::semibold(kF14), w), gfx::semibold(kF14), active || pending ? c.warning : c.textMuted);
        y += 24.f;
    } else if (!d.known) {
        r.drawText({x, y}, fit(r, "Aucune d\xC3\xA9rogation connue de ce poste (le regard du service la relit toutes les 15 s).", kF13, w), kF13, c.textMuted);
        y += 22.f;
    }
    if (keys_.empty()) {
        r.drawText({x, y}, fit(r, "Aucun admin actif dans la liste : ajoute d'abord un admin (Administrateurs).", kF13, w), kF13, c.error);
        y += 22.f;
    }
    if (running_) r.drawText({x, y}, "Le service r\xC3\xA9pond\xE2\x80\xA6", kF13, c.textMuted);
    else if (!error_.empty()) (void)drawWrapped(r, x, y, w, error_, kF13, c.error, 18.f, 3);
}

ui::EventResult DerogationBody::onEvent(const ui::InputEvent& ev) {
    if (frame_.hitClose(ev)) {
        closeMe();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ============================================================================== 1.52.0 : XPG Admin, taper le code
class CodeDialog;

class CodeBody final : public ui::Widget {
public:
    CodeBody(CodeDialog& owner, ShellHost host, std::function<void()> done);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    [[nodiscard]] Workspace& ws() const { return host_.workspace(); }
    void activate();
    void sync();
    void closeMe();

    CodeDialog&            owner_;
    ShellHost              host_;
    std::function<void()>  done_;
    ui::InputText*         field_{nullptr};
    ui::Button*            later_{nullptr};
    ui::Button*            ok_{nullptr};
    std::string            error_, success_;
    bool                   running_{false};
    Frame                  frame_;
    float                  textTop_{0.f};
    core::ConnectionScope  links_;
    std::shared_ptr<bool>  alive_{std::make_shared<bool>(true)};
};

class CodeDialog final : public Dialog {
public:
    CodeDialog(ShellHost host, std::function<void()> done) : Dialog("dialog.derogation.code"), host_(std::move(host)), done_(std::move(done)) {}
    [[nodiscard]] std::string title() const override { return "Le code de la d\xC3\xA9rogation"; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<CodeBody>(*this, host_, done_));
        return core::ok();
    }
    void onTick() override {
        if (ui::Widget* r = widgetRoot()) r->invalidate();
    }

private:
    ShellHost             host_;
    std::function<void()> done_;
};

CodeBody::CodeBody(CodeDialog& owner, ShellHost host, std::function<void()> done)
    : ui::Widget("derogation.code.corps"), owner_(owner), host_(std::move(host)), done_(std::move(done)) {
    field_ = &adopt(*this, std::make_unique<ui::InputText>("derogation.code"));
    field_->setPlaceholder("1234 5678");
    field_->setMaxLength(14);
    later_ = &button(*this, "Plus tard", "derogation.code.fermer");
    ok_ = &button(*this, "Activer", "derogation.code.activer", ui::Button::Style::Primary, ui::Icon::Lock);
    links_ += later_->clicked->connect([this] { closeMe(); });
    links_ += ok_->clicked->connect([this] {
        if (!success_.empty()) closeMe();
        else activate();
    });
    links_ += field_->textChanged->connect([this](const std::string&) {
        if (!error_.empty() && !running_) {
            error_.clear();
            invalidate();
        }
    });
    sync();
}

void CodeBody::sync() {
    const auto& d = ws().derogation();
    const bool mine = d.forKey(ws().adminKey().keyHex);
    const bool pending = mine && d.pendingAt(ws().now());
    ok_->setText(success_.empty() ? std::string("Activer") : std::string("Fermer"));
    ok_->setEnabled(!running_ && (!success_.empty() || pending));
    show(later_, success_.empty());
    show(field_, success_.empty());
}

void CodeBody::closeMe() {
    field_->setText({});   // le code tape ne reste pas
    if (done_) done_();
    owner_.close();
}

void CodeBody::activate() {
    if (running_) return;
    const std::string code = normalizeDerogationCode(field_->text());
    if (code.empty()) {
        error_ = "Le code fait 8 chiffres : celui que l'admin+ voit sur son PC (les espaces ne comptent pas).";
        invalidate();
        return;
    }
    const std::string id = ws().derogation().id;
    const Workspace* w = &ws();
    auto out = std::make_shared<Derogation>();
    std::weak_ptr<bool> alive = alive_;
    running_ = true;
    error_.clear();
    sync();
    const bool started = host_.runSlow && host_.runSlow("Service : le code de la d\xC3\xA9rogation", [w, id, code, out] { return w->activateDerogation(id, code, out.get()); },
                                                        [this, alive, out](const OpResult& r) {
                                                            if (alive.expired()) return;
                                                            running_ = false;
                                                            if (!out->id.empty()) ws().adoptDerogation(*out);   // le menu suit aussitot
                                                            field_->setText({});   // le code tape ne reste pas (avant la phrase : vider le champ l'efface)
                                                            if (!r.ok) {
                                                                error_ = r.message;
                                                                if (!error_.empty()) error_[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(error_[0])));
                                                            } else {
                                                                success_ = r.message;
                                                            }
                                                            sync();
                                                            invalidate();
                                                        });
    if (!started) {
        running_ = false;
        error_ = "Un travail est d\xC3\xA9j\xC3\xA0 en cours : attends qu'il finisse, puis recommence.";
        sync();
    }
    invalidate();
}

void CodeBody::onLayout() {
    frame_.place(*this, 600.f, 420.f);
    const auto& p = frame_.panel;
    const float x = p.x + 24.f;
    textTop_ = p.y + 92.f;
    field_->setBounds({x, textTop_ + 96.f, 240.f, 40.f});
    const float by = p.bottom() - 54.f;
    const float ow = buttonWidth(*ok_, 120.f), lw = buttonWidth(*later_, 110.f);
    ok_->setBounds({p.right() - 24.f - ow, by, ow, 34.f});
    later_->setBounds({ok_->bounds().x - 8.f - lw, by, lw, 34.f});
}

void CodeBody::onPaint(const ui::PaintContext& ctx) {
    sync();
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto& p = frame_.panel;
    const auto& d = ws().derogation();
    const Seconds now = ws().now();
    const std::string howLong = durationText(d.duration > 0 ? d.duration : d.end - d.start);
    (void)frame_.paint(r, ctx.theme, "Taper le code de la d\xC3\xA9rogation",
                       d.exists() ? "L'admin+ t'a envoy\xC3\xA9 une d\xC3\xA9rogation (" + howLong + ")" : std::string("Une d\xC3\xA9rogation de l'admin+"), ui::Icon::Lock);
    const float x = p.x + 24.f, w = p.w - 48.f;
    if (!success_.empty()) {
        drawTick(r, x, textTop_ + 4.f, c.ok);
        (void)drawWrapped(r, x + 24.f, textTop_, w - 24.f, success_, gfx::semibold(FontId{16}), c.ok, 22.f, 3);
        (void)drawWrapped(r, x, textTop_ + 80.f, w,
                          "Le service la tient \xC3\xA0 son heure : \xC3\xA0 la fin, ou si l'admin+ la r\xC3\xA9voque, XPG Admin repasse \xC3\xA0 la gestion seule. "
                          "Chaque geste qu'elle ouvre est \xC3\xA9" "crit au journal des admins.",
                          kF13, c.textMuted, 18.f);
        return;
    }
    (void)drawWrapped(r, x, textTop_, w,
                      "Tape le code de 8 chiffres que l'admin+ voit sur son PC (il te le donne de vive voix). D\xC3\xA8s que le service l'accepte, tu vois tout "
                      "comme l'admin+ pendant " + howLong + ".",
                      kF14, c.text, 20.f, 3);
    const bool pending = d.forKey(ws().adminKey().keyHex) && d.pendingAt(now);
    std::string state;
    if (pending)
        state = "Valable jusqu'\xC3\xA0 " + clockText(d.localTime(d.validUntil, now), now) + " \xC2\xB7 " + std::to_string(d.triesLeft > 0 ? d.triesLeft : kDerogationCodeTries) +
                (d.triesLeft == 1 ? " essai" : " essais");
    else if (d.exists() && d.forKey(ws().adminKey().keyHex))
        state = "Ta d\xC3\xA9rogation : " + derogationText(d, now) + ".";
    else
        state = "Aucune d\xC3\xA9rogation n'attend de code.";
    r.drawText({x, textTop_ + 66.f}, fit(r, state, kF13, w), kF13, pending ? c.textMuted : c.warning);
    const Rect fb = field_->bounds();
    r.drawText({fb.right() + 14.f, fb.y + 11.f}, fit(r, "les espaces et les tirets ne comptent pas", kF12, p.right() - 24.f - fb.right() - 14.f), kF12, c.textMuted);
    const float y = fb.bottom() + 18.f;
    if (running_) r.drawText({x, y}, "Le service v\xC3\xA9rifie\xE2\x80\xA6", kF13, c.textMuted);
    else if (!error_.empty()) (void)drawWrapped(r, x, y, w, error_, gfx::semibold(kF14), c.error, 20.f, 3);
}

ui::EventResult CodeBody::onEvent(const ui::InputEvent& ev) {
    if (frame_.hitClose(ev)) {
        closeMe();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace

// ============================================================================== 1.52.0 : Mon compte, la derogation recue
DerogationCard::DerogationCard(ShellHost host) : ui::Widget("compte.derogation"), host_(std::move(host)) {
    code_ = &adopt(*this, std::make_unique<ui::InputText>("compte.derogation.code"));
    code_->setPlaceholder("1234 5678");
    code_->setMaxLength(14);
    activate_ = &button(*this, "Activer", "compte.derogation.activer", ui::Button::Style::Primary, ui::Icon::Lock);
    links_ += activate_->clicked->connect([this] { activate(); });
    refresh();
}

DerogationCard::~DerogationCard() {
    *alive_ = false;
    links_.clear();
}

bool DerogationCard::pending() const {
    const Derogation& d = ws().derogation();
    return ws().role() == Role::Admin && d.forKey(ws().adminKey().keyHex) && d.pendingAt(ws().now());
}

bool DerogationCard::active() const {
    const Derogation& d = ws().derogation();
    return ws().role() == Role::Admin && d.forKey(ws().adminKey().keyHex) && d.activeAt(ws().now());
}

bool DerogationCard::wanted() const { return pending() || active(); }

float DerogationCard::wantedHeight(float) const {
    if (pending()) return 214.f + (said_.empty() ? 0.f : 30.f);
    return active() ? 132.f : 0.f;
}

void DerogationCard::refresh() {
    if (!pending() && !ok_) said_.clear();
    show(code_, pending());
    show(activate_, pending());
    activate_->setEnabled(!running_);
    invalidateLayout();
    invalidate();
}

void DerogationCard::activate() {
    if (running_) return;
    const std::string code = normalizeDerogationCode(code_->text());
    if (code.empty()) {
        said_ = "Le code fait 8 chiffres : celui que l'admin+ voit sur son PC (les espaces ne comptent pas).";
        ok_ = false;
        invalidate();
        return;
    }
    const std::string id = ws().derogation().id;
    const Workspace* w = &ws();
    auto out = std::make_shared<Derogation>();
    std::weak_ptr<bool> alive = alive_;
    running_ = true;
    said_.clear();
    activate_->setEnabled(false);
    const bool started = host_.runSlow && host_.runSlow("Service : le code de la d\xC3\xA9rogation", [w, id, code, out] { return w->activateDerogation(id, code, out.get()); },
                                                        [this, alive, out](const OpResult& r) {
                                                            if (alive.expired()) return;
                                                            running_ = false;
                                                            if (!out->id.empty()) ws().adoptDerogation(*out);
                                                            code_->setText({});   // le code tape ne reste pas
                                                            ok_ = r.ok;
                                                            said_ = r.message;
                                                            if (!said_.empty()) said_[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(said_[0])));
                                                            refresh();
                                                        });
    if (!started) {
        running_ = false;
        said_ = "Un travail est d\xC3\xA9j\xC3\xA0 en cours : attends qu'il finisse, puis recommence.";
        activate_->setEnabled(true);
    }
    invalidate();
}

void DerogationCard::onLayout() {
    const Rect b = bounds();
    if (!pending()) return;
    const float fx = b.x + 24.f;
    code_->setBounds({fx, b.y + 124.f, 220.f, 38.f});
    activate_->setBounds({fx + 232.f, b.y + 124.f, buttonWidth(*activate_, 120.f), 38.f});
}

void DerogationCard::onPaint(const ui::PaintContext& ctx) {
    if (!wanted()) return;
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto& c = th.color;
    const Rect b = bounds();
    const Derogation& d = ws().derogation();
    const Seconds now = ws().now();
    const bool wait = pending();
    cardBox(r, th, b);
    r.fillRect({b.x, b.y + 12.f, 4.f, b.h - 24.f}, c.warning);
    const float fx = b.x + 24.f, fw = b.w - 48.f;
    const Rect ib{fx, b.y + 24.f, 40.f, 40.f};
    r.fillRoundedRect(ib, c.warning, 11.f);
    ui::drawIcon(r, wait ? ui::Icon::Warning : ui::Icon::Lock, {ib.x + 11.f, ib.y + 11.f, 18.f, 18.f}, rgb(0xFFFFFF));
    const std::string howLong = durationText(d.duration > 0 ? d.duration : d.end - d.start);
    if (!wait) {
        r.drawText({fx + 54.f, b.y + 26.f}, "Ta d\xC3\xA9rogation", gfx::semibold(FontId{16}), c.text);
        r.drawText({fx + 54.f, b.y + 48.f}, fit(r, "accord\xC3\xA9" "e par l'admin+ \xC2\xB7 " + howLong + " \xC2\xB7 le service la tient \xC3\xA0 son heure", kF13, fw - 54.f), kF13,
                   c.textMuted);
        (void)drawWrapped(r, fx, b.y + 82.f, fw, (ok_ && !said_.empty() ? said_ + " " : std::string()) + "Tu vois tout comme l'admin+ : " + derogationText(d, now) + ".",
                          gfx::semibold(kF14), c.text, 21.f, 2);
        return;
    }
    r.drawText({fx + 54.f, b.y + 26.f}, "D\xC3\xA9rogation re\xC3\xA7ue : taper le code", gfx::semibold(FontId{16}), c.text);
    r.drawText({fx + 54.f, b.y + 48.f},
               fit(r, "valable jusqu'\xC3\xA0 " + clockText(d.localTime(d.validUntil, now), now) + " \xC2\xB7 " + std::to_string(d.triesLeft > 0 ? d.triesLeft : kDerogationCodeTries) +
                          (d.triesLeft == 1 ? " essai" : " essais"),
                   kF13, fw - 54.f),
               kF13, c.textMuted);
    (void)drawWrapped(r, fx, b.y + 82.f, fw,
                      "L'admin+ t'a envoy\xC3\xA9 une d\xC3\xA9rogation (" + howLong + ") : tape le code qu'il voit sur son PC. D\xC3\xA8s que le service l'accepte, tu vois tout "
                      "comme l'admin+ pendant " + howLong + ".",
                      kF14, c.text, 20.f, 2);
    const Rect cb = code_->bounds();
    r.drawText({activate_->bounds().right() + 14.f, cb.y + 11.f}, fit(r, "les espaces et les tirets ne comptent pas", kF12, std::max(10.f, fx + fw - activate_->bounds().right() - 14.f)),
               kF12, c.textMuted);
    if (running_) r.drawText({fx, cb.bottom() + 12.f}, "Le service v\xC3\xA9rifie\xE2\x80\xA6", kF13, c.textMuted);
    else if (!said_.empty()) (void)drawWrapped(r, fx, cb.bottom() + 12.f, fw, said_, gfx::semibold(kF14), ok_ ? c.ok : c.error, 20.f, 2);
}

menu::MenuPtr makeDerogationDialog(ShellHost host, std::string adminKey, std::function<void()> done) {
    return std::make_unique<DerogationDialog>(std::move(host), std::move(adminKey), std::move(done));
}

menu::MenuPtr makeDerogationCodeDialog(ShellHost host, std::function<void()> done) {
    return std::make_unique<CodeDialog>(std::move(host), std::move(done));
}

} // namespace license_admin::gui
