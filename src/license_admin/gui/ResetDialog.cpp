// =============================================================================
//  license_admin/gui/ResetDialog.cpp - 1.49.0 : LA FENETRE << Repartir de zero >>
//  (l'admin+ seul ; docs/ADMINS.md, par. 13)
// -----------------------------------------------------------------------------
//  Ce qui est efface (le service ET ce poste) et ce qui est garde (deux colonnes :
//  Workspace::startOverErased, startOverKept) ; que les Studios deja actives
//  perdent leur licence a leur prochain controle ; que la sauvegarde complete
//  exacte est faite d'abord, ou elle va, et que << Restaurer une sauvegarde... >>
//  remet tout ; le mot << REPARTIR >> a taper (le bouton rouge reste gris sans
//  lui). << Repartir de zero >> : Workspace::startOver (runSlow) ; le resultat est
//  dit par la coquille (report : tout est relu). Ouverte par Service en ligne (la
//  carte << Repartir de zero >>) et par le bandeau du premier lancement de l'Admin+
//  1.49 (le tableau de bord). XPG Admin (l'edition classique) ne l'ouvre jamais.
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

class ResetDialog;

class ResetBody final : public ui::Widget {
public:
    ResetBody(ResetDialog& owner, ShellHost host, std::function<void()> done);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    [[nodiscard]] Workspace& ws() const { return host_.workspace(); }
    void sync();
    void run();
    void closeMe();

    ResetDialog&          owner_;
    ShellHost             host_;
    std::function<void()> done_;
    ui::InputText*        word_{nullptr};
    ui::Button*           cancel_{nullptr};
    ui::Button*           ok_{nullptr};
    std::string           error_;
    bool                  running_{false};
    Frame                 frame_;
    float                 textTop_{0.f}, colTop_{0.f}, colH_{0.f}, warnTop_{0.f}, warnH_{0.f}, backupTop_{0.f};
    core::ConnectionScope links_;
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

class ResetDialog final : public Dialog {
public:
    ResetDialog(ShellHost host, std::function<void()> done) : Dialog("dialog.repartir"), host_(std::move(host)), done_(std::move(done)) {}
    [[nodiscard]] std::string title() const override { return "Repartir de z\xC3\xA9ro"; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<ResetBody>(*this, host_, done_));
        return core::ok();
    }

private:
    ShellHost             host_;
    std::function<void()> done_;
};

const char* const kIntro =
    "Tout ce qui touche aux clients, \xC3\xA0 leurs PC et aux admins est effac\xC3\xA9, sur le service ET sur ce poste. Ta cl\xC3\xA9 d'\xC3\xA9" "diteur, "
    "ton catalogue et ton service restent : tu repars avec une base vide, pr\xC3\xAAte \xC3\xA0 vendre.";
const char* const kStudios =
    "Les Studios d\xC3\xA9j\xC3\xA0 activ\xC3\xA9s perdent leur licence \xC3\xA0 leur prochain contr\xC3\xB4le (quelques minutes s'ils sont en ligne) : le service "
    "ne la conna\xC3\xAEt plus. Les admins ne peuvent plus rien faire (leur cl\xC3\xA9 n'est plus dans la liste).";
constexpr float kColLine = 18.f;

float listHeight(const std::vector<std::string>& lines, float w) {
    float h = 24.f;
    for (const auto& l : lines) h += wrappedHeight(l, kF13, w - 14.f, kColLine) + 4.f;
    return h;
}

ResetBody::ResetBody(ResetDialog& owner, ShellHost host, std::function<void()> done)
    : ui::Widget("repartir.corps"), owner_(owner), host_(std::move(host)), done_(std::move(done)) {
    word_ = &adopt(*this, std::make_unique<ui::InputText>("repartir.mot"));
    word_->setPlaceholder("REPARTIR");
    word_->setMaxLength(20);
    cancel_ = &button(*this, "Fermer", "repartir.annuler");
    ok_ = &button(*this, "Repartir de z\xC3\xA9ro", "repartir.ok", ui::Button::Style::Danger);
    links_ += cancel_->clicked->connect([this] { closeMe(); });
    links_ += ok_->clicked->connect([this] { run(); });
    links_ += word_->textChanged->connect([this](const std::string&) {
        error_.clear();
        sync();
        invalidate();
    });
    sync();
}

void ResetBody::sync() {
    const std::string why = ws().whyNotStartOver();
    const bool busy = running_ || (host_.busy && host_.busy());
    ok_->setEnabled(why.empty() && !busy && word_->text() == kStartOverWord);
    ok_->setTooltip(!why.empty() ? why : word_->text() != kStartOverWord ? std::string("Tape REPARTIR pour confirmer.") : std::string());
}

void ResetBody::closeMe() {
    if (done_) done_();
    owner_.close();
}

void ResetBody::run() {
    if (running_) return;
    if (const std::string why = ws().whyNotStartOver(); !why.empty()) {
        error_ = why;
        invalidate();
        return;
    }
    if (word_->text() != kStartOverWord) {
        error_ = "Tape REPARTIR (en majuscules) pour confirmer.";
        invalidate();
        return;
    }
    const Workspace* w = &ws();
    const std::string word = word_->text();
    std::weak_ptr<bool> alive = alive_;
    running_ = true;
    error_.clear();
    sync();
    const bool started = host_.runSlow && host_.runSlow("Repartir de z\xC3\xA9ro (la sauvegarde, puis le service)", [w, word] { return w->startOver(word); },
                                                        [this, alive](const OpResult& r) {
                                                            if (alive.expired()) return;
                                                            running_ = false;
                                                            if (!r.ok) {
                                                                error_ = r.message;
                                                                sync();
                                                                invalidate();
                                                                return;
                                                            }
                                                            closeMe();
                                                        });
    if (!started) {
        running_ = false;
        error_ = "Un travail est d\xC3\xA9j\xC3\xA0 en cours : attends qu'il finisse, puis recommence.";
        sync();
    }
    invalidate();
}

void ResetBody::onLayout() {
    frame_.place(*this, 820.f, 760.f);
    const auto& p = frame_.panel;
    const float x = p.x + 24.f, w = p.w - 48.f;
    textTop_ = p.y + 92.f;
    float y = textTop_ + wrappedHeight(kIntro, kF14, w, 20.f) + 12.f;
    colTop_ = y;
    const float cw = (w - 16.f) * 0.5f;
    colH_ = std::max(listHeight(Workspace::startOverErased(), cw), listHeight(Workspace::startOverKept(), cw)) + 8.f;
    y += colH_ + 10.f;
    warnTop_ = y;
    warnH_ = wrappedHeight(kStudios, kF13, w - 24.f, 18.f) + 18.f;
    y += warnH_ + 10.f;
    backupTop_ = y;
    y += 60.f;
    word_->setBounds({x + 250.f, y, std::min(220.f, w - 250.f), 32.f});
    const float by = p.bottom() - 54.f;
    const float ow = buttonWidth(*ok_, 170.f), cw2 = buttonWidth(*cancel_, 100.f);
    ok_->setBounds({p.right() - 24.f - ow, by, ow, 34.f});
    cancel_->setBounds({ok_->bounds().x - 8.f - cw2, by, cw2, 34.f});
}

void ResetBody::onPaint(const ui::PaintContext& ctx) {
    sync();
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto& p = frame_.panel;
    (void)frame_.paint(r, ctx.theme, "Repartir de z\xC3\xA9ro", "L'admin+ seul ; une sauvegarde compl\xC3\xA8te est faite d'abord", ui::Icon::Lock);
    const float x = p.x + 24.f, w = p.w - 48.f;
    (void)drawWrapped(r, x, textTop_, w, kIntro, kF14, c.text, 20.f);
    // les deux colonnes : efface (rouge), garde (vert)
    const float cw = (w - 16.f) * 0.5f;
    const auto column = [&](float cx, const char* head, const std::vector<std::string>& lines, Color tone) {
        r.fillRoundedRect({cx, colTop_, cw, colH_}, tone.withAlpha(18), 8.f);
        r.drawText({cx + 10.f, colTop_ + 6.f}, head, gfx::semibold(kF14), tone);
        float y = colTop_ + 30.f;
        for (const auto& l : lines) {
            r.drawText({cx + 10.f, y}, "\xE2\x80\xA2", kF13, tone);
            y += drawWrapped(r, cx + 22.f, y, cw - 32.f, l, kF13, c.text, kColLine) + 4.f;
        }
    };
    column(x, "Effac\xC3\xA9 (le service et ce poste)", Workspace::startOverErased(), c.error);
    column(x + cw + 16.f, "Gard\xC3\xA9", Workspace::startOverKept(), c.ok);
    // les Studios (un encadre ambre)
    r.fillRoundedRect({x, warnTop_, w, warnH_}, c.warning.withAlpha(30), 8.f);
    (void)drawWrapped(r, x + 12.f, warnTop_ + 9.f, w - 24.f, kStudios, kF13, c.text, 18.f);
    // la sauvegarde
    const BackupSettings bs = ws().backupSettings();
    std::string backup = bs.configured ? "D'abord, une sauvegarde compl\xC3\xA8te exacte (le service entier, ce dossier, ta cl\xC3\xA9) dans " + bs.folder +
                                             ". \xC2\xAB Restaurer une sauvegarde\xE2\x80\xA6 \xC2\xBB (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9) remet tout."
                                       : "La sauvegarde compl\xC3\xA8te n'est pas r\xC3\xA9gl\xC3\xA9" "e : r\xC3\xA8gle-la d'abord (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9 \xE2\x80\xBA "
                                         "Sauvegarde : son dossier et sa phrase). Elle est faite juste avant, et elle remet tout.";
    (void)drawWrapped(r, x, backupTop_, w, backup, kF13, bs.configured ? c.text : c.error, 18.f, 3);
    r.drawText({x, word_->bounds().y + 7.f}, "Tape REPARTIR pour confirmer", gfx::semibold(kF14), c.text);
    const float y = word_->bounds().bottom() + 10.f;
    if (running_) r.drawText({x, y}, "La sauvegarde, puis le service\xE2\x80\xA6 (ne ferme pas l'Admin)", kF13, c.textMuted);
    else if (!error_.empty()) (void)drawWrapped(r, x, y, w, error_, kF13, c.error, 18.f, 3);
    else if (const std::string why = ws().whyNotStartOver(); !why.empty()) (void)drawWrapped(r, x, y, w, why, kF13, c.error, 18.f, 3);
}

ui::EventResult ResetBody::onEvent(const ui::InputEvent& ev) {
    if (frame_.hitClose(ev)) {
        closeMe();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace

menu::MenuPtr makeStartOverDialog(ShellHost host, std::function<void()> done) {
    if (license_admin::classique()) return nullptr;   // XPG Admin ne repart jamais de zero
    return std::make_unique<ResetDialog>(std::move(host), std::move(done));
}

} // namespace license_admin::gui
