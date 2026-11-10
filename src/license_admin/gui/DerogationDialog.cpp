// =============================================================================
//  license_admin/gui/DerogationDialog.cpp - 1.47.0 : LE DIALOGUE << Derogation
//  << Gerer le service >> >> (l'admin+ ; docs/ADMINS.md, par. 11)
// -----------------------------------------------------------------------------
//  L'admin+ seul gere le service. Ce dialogue le donne a UN admin pour un temps
//  COURT : 1 h, 4 h, 24 h ou 72 h (jamais plus, jamais << permanente >>). Il dit
//  ce que la derogation ouvre (mettre le service en ligne ou a jour, changer son
//  adresse et ses adresses de secours, le releve du jour) et ce qu'elle n'ouvre
//  jamais (certifier, la sauvegarde, la restauration : la cle de l'editeur ;
//  brancher Stripe : sa cle Stripe ne se partage pas) ; que l'admin se connecte a Cloudflare avec SON propre compte
//  (membre du compte de l'editeur), jamais un jeton partage ; que le service la
//  tient a son heure et que chaque usage est ecrit au journal (l'admin+ prevenu).
//  << Accorder >> : Workspace::grantDerogation (runSlow) ; une derogation en cours :
//  << Revoquer maintenant >> (Workspace::revokeDerogation).
//  Ouvert par Service en ligne (la carte << La derogation >>) et par la fiche d'un
//  admin (Administrateurs : PageAdmins.cpp, un branchement).
// =============================================================================
#include "AdminsKit.hpp"
#include "Page.hpp"

#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace {

using namespace kit;

constexpr FontId kF13{13}, kF14{14};

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
    void grant();
    void revoke();
    void sync();
    void closeMe();

    DerogationDialog&                owner_;
    ShellHost                        host_;
    std::function<void()>            done_;
    ui::DropDown*                    admin_{nullptr};
    std::shared_ptr<ui::RadioGroup>  hours_{std::make_shared<ui::RadioGroup>()};
    ui::RadioButton*                 hour_[4]{};
    ui::Button*                      cancel_{nullptr};
    ui::Button*                      ok_{nullptr};
    ui::Button*                      revoke_{nullptr};
    std::vector<std::string>         keys_;    // les cles des admins de la liste deroulante
    std::string                      error_;
    bool                             running_{false};
    Frame                            frame_;
    float                            textTop_{0.f}, stateY_{0.f};
    core::ConnectionScope            links_;
    std::shared_ptr<bool>            alive_{std::make_shared<bool>(true)};
};

class DerogationDialog final : public Dialog {
public:
    DerogationDialog(ShellHost host, std::string adminKey, std::function<void()> done)
        : Dialog("dialog.derogation"), host_(std::move(host)), adminKey_(std::move(adminKey)), done_(std::move(done)) {}
    [[nodiscard]] std::string title() const override { return "D\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB"; }

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
    "Pour un temps court, l'admin choisi peut g\xC3\xA9rer le service comme toi : le mettre en ligne ou \xC3\xA0 jour (D\xC3\xA9ployer le service), "
    "changer son adresse et ses adresses de secours, le relev\xC3\xA9 du jour. Jamais : certifier la cl\xC3\xA9 du service, la sauvegarde, la "
    "restauration (ta cl\xC3\xA9 d'\xC3\xA9" "diteur ne quitte jamais ce PC), Brancher Stripe (ta cl\xC3\xA9 Stripe ne se partage pas).";
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
    const char* const labels[] = {"1 heure", "4 heures", "24 heures", "72 heures (le plus long)"};
    const char* const ids[] = {"derogation.duree.1", "derogation.duree.4", "derogation.duree.24", "derogation.duree.72"};
    for (int i = 0; i < 4; ++i) hour_[i] = &adopt(*this, std::make_unique<ui::RadioButton>(labels[i], hours_, i, ids[i]));
    hours_->setValue(0);
    cancel_ = &button(*this, "Fermer", "derogation.annuler");
    ok_ = &button(*this, "Accorder", "derogation.accorder", ui::Button::Style::Primary, ui::Icon::Lock);
    revoke_ = &button(*this, "R\xC3\xA9voquer maintenant", "derogation.revoquer", ui::Button::Style::Danger);
    links_ += cancel_->clicked->connect([this] { closeMe(); });
    links_ += ok_->clicked->connect([this] { grant(); });
    links_ += revoke_->clicked->connect([this] { revoke(); });
    sync();
}

void DerogationBody::sync() {
    const bool plus = ws().role() == Role::Plus;
    const auto& d = ws().derogation();
    const bool active = d.activeAt(ws().now());
    ok_->setEnabled(plus && !running_ && !keys_.empty());
    revoke_->setEnabled(plus && !running_ && active);
    show(revoke_, active);
    if (!plus) ok_->setTooltip("Seul l'admin+ accorde une d\xC3\xA9rogation.");
}

void DerogationBody::closeMe() {
    if (done_) done_();
    owner_.close();
}

void DerogationBody::grant() {
    if (running_) return;
    const auto* item = admin_->selectedItem();
    if (!item) {
        error_ = "Choisis l'admin (la liste des admins n'a personne d'actif).";
        invalidate();
        return;
    }
    const int hours = kDerogationHours[std::clamp(hours_->value(), 0, 3)];
    const std::string key = item->value;
    const Workspace* w = &ws();
    auto out = std::make_shared<Derogation>();
    std::weak_ptr<bool> alive = alive_;
    running_ = true;
    error_.clear();
    sync();
    const bool started = host_.runSlow && host_.runSlow("Service : d\xC3\xA9rogation", [w, key, hours, out] { return w->grantDerogation(key, hours, out.get()); },
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
                                                            closeMe();
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
    frame_.place(*this, 680.f, 640.f);
    const auto& p = frame_.panel;
    const float x = p.x + 24.f, w = p.w - 48.f;
    textTop_ = p.y + 92.f;
    float y = textTop_ + wrappedHeight(kWhat, kF14, w, 20.f) + 14.f;
    admin_->setBounds({x + 120.f, y, std::min(420.f, w - 120.f), 32.f});
    y += 44.f;
    float hx = x + 120.f;
    for (auto* r : hour_) {
        const float rw = std::max(90.f, r->sizeHint().preferred.w + 10.f);
        r->setBounds({hx, y + 4.f, rw, 26.f});
        hx += rw + 10.f;
    }
    y += 44.f;
    stateY_ = y;
    const float by = p.bottom() - 54.f;
    const float ow = buttonWidth(*ok_, 130.f), cw = buttonWidth(*cancel_, 100.f), rw = buttonWidth(*revoke_, 170.f);
    ok_->setBounds({p.right() - 24.f - ow, by, ow, 34.f});
    cancel_->setBounds({ok_->bounds().x - 8.f - cw, by, cw, 34.f});
    revoke_->setBounds({x, by, rw, 34.f});
}

void DerogationBody::onPaint(const ui::PaintContext& ctx) {
    sync();
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto& p = frame_.panel;
    (void)frame_.paint(r, ctx.theme, "D\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB",
                       "\xC3\x80 un seul admin, pour un temps court : 72 h au plus, jamais permanente", ui::Icon::Lock);
    const float x = p.x + 24.f, w = p.w - 48.f;
    (void)drawWrapped(r, x, textTop_, w, kWhat, kF14, c.text, 20.f);
    r.drawText({x, admin_->bounds().y + 7.f}, "L'admin", gfx::semibold(kF14), c.text);
    r.drawText({x, hour_[0]->bounds().y + 3.f}, "Dur\xC3\xA9" "e", gfx::semibold(kF14), c.text);
    float y = stateY_;
    // Cloudflare : son propre compte, jamais un jeton partage (un encadre ambre)
    const float hc = wrappedHeight(kCloudflare, kF13, w - 24.f, 18.f) + 18.f;
    r.fillRoundedRect({x, y, w, hc}, c.warning.withAlpha(30), 8.f);
    (void)drawWrapped(r, x + 12.f, y + 9.f, w - 24.f, kCloudflare, kF13, c.text, 18.f);
    y += hc + 12.f;
    y = drawWrapped(r, x, y, w, kService, kF13, c.textMuted, 18.f) + 10.f;
    // la derogation en place
    const auto& d = ws().derogation();
    if (d.exists()) {
        const bool active = d.activeAt(ws().now());
        const std::string line = std::string(active ? "En cours : " : "La derni\xC3\xA8re : ") + d.adminName + " \xE2\x80\x94 " + derogationText(d, ws().now()) +
                                 (d.uses ? " \xC2\xB7 utilis\xC3\xA9" "e " + std::to_string(d.uses) + " fois" : std::string());
        r.drawText({x, y}, fit(r, line, gfx::semibold(kF14), w), gfx::semibold(kF14), active ? c.warning : c.textMuted);
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

} // namespace

menu::MenuPtr makeDerogationDialog(ShellHost host, std::string adminKey, std::function<void()> done) {
    return std::make_unique<DerogationDialog>(std::move(host), std::move(adminKey), std::move(done));
}

} // namespace license_admin::gui
