// =============================================================================
//  license_admin/gui/DeployDialog.cpp - 1.45.0 : LE DIALOGUE << Deployer le
//  service >> dans XPG-License-Manager-Admin (l'outil de l'EDITEUR ; jamais livre)
// -----------------------------------------------------------------------------
//  L'editeur oubliait de redeployer son service apres une mise a jour de l'Admin
//  (menu Demarrer > Deployer le service en ligne) : ses sessions par Internet
//  echouaient. Le bouton << Deployer le service >> (Service en ligne, le bandeau
//  du tableau de bord) ouvre ce dialogue, qui :
//    - lance tout de suite le kit installe avec l'Admin (deployer::deploy, par
//      runSlow : un seul travail lent a la fois ; la pastille du haut le dit),
//      sans fenetre de console ; cle-publique.txt est d'abord ecrite dans le kit
//      si elle manque (l'admin+ : la cle PUBLIQUE de l'editeur) ;
//    - montre sa sortie EN DIRECT (le journal qui defile, la molette pour
//      remonter), l'etape (Node.js, wrangler, l'envoi a Cloudflare, la
//      verification) et le temps ; Annuler (ou Echap, ou la croix) l'arrete ;
//    - puis le resultat : en ligne -> l'adresse (reglee dans l'Admin si le poste
//      n'en avait pas) et l'etat relu (/v1/etat) : << Service en ligne : version
//      1.44.0 >> ; sinon la raison en francais et l'etape suivante : Node.js absent
//      (Ouvrir nodejs.org), pas connecte a Cloudflare (<< Se connecter a
//      Cloudflare... >> : npx wrangler login, qui ouvre le navigateur, puis
//      << Deployer le service >> de nouveau), un autre echec (les dernieres
//      lignes du journal ; Copier le journal).
//  Aucune ligne n'est montree sans passer par deployer::scrub (un jeton, un mot
//  de passe : masques) ; le journal n'est ecrit nulle part.
//  Et serviceVersion (Page.hpp) : la version en ligne face a l'Admin.
// =============================================================================
#include "AdminsKit.hpp"
#include "Page.hpp"

#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace license_admin::gui {

// 1.45.0 : la version en ligne (l'etat lu dans cette seance) face a ce qu'il faut et au kit installe.
deployer::VersionCheck serviceVersion(const Workspace& w) {
    const auto& s = w.service();
    const deployer::Kit* kit = deployer::installedKit();
    const std::string online = s.ready() && s.hasState && s.state.reached ? s.state.version : std::string();
    return deployer::checkVersion(online, kit ? kit->version : std::string());
}

namespace {

using namespace kit;

constexpr FontId kF13{13}, kF14{14}, kF15{15};
constexpr FontId kLogFont{13};
constexpr float  kLogLine = 18.f;
constexpr std::size_t kMaxLog = 4000;

// Ce que le fil du travail et l'interface partagent : les lignes recues (videes a chaque image), l'annulation.
struct Live {
    std::mutex               m;
    std::vector<std::string> fresh;
    std::atomic<bool>        cancel{false};
    void push(const std::string& l) {
        std::lock_guard<std::mutex> lk(m);
        fresh.push_back(l);
    }
    std::vector<std::string> drain() {
        std::lock_guard<std::mutex> lk(m);
        std::vector<std::string> out;
        out.swap(fresh);
        return out;
    }
};

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

std::string clockOf(double seconds) {
    const int s = std::max(0, static_cast<int>(seconds));
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

OpResult resultOf(const deployer::Report& rep, bool login) {
    OpResult r;
    r.ok = rep.ok();
    r.message = std::string(login ? "Connexion \xC3\xA0 Cloudflare : " : "Mise en ligne du service : ") + rep.reason;
    if (!rep.next.empty()) r.details.push_back(rep.next);
    if (!rep.ok())
        for (const auto& l : rep.tail) r.details.push_back(l);
    return r;
}

class DeployDialog;

class DeployBody final : public ui::Widget {
public:
    enum class Action : std::uint8_t { Deploy, Login };
    enum class Stage : std::uint8_t { Idle, Running, Verifying, Done };

    DeployBody(DeployDialog& owner, ShellHost host, std::function<void()> done);
    ~DeployBody() override {
        links_.clear();
        if (live_) live_->cancel.store(true);   // la fenetre se ferme pendant le travail : il s'arrete
    }

    void start(Action a);
    void tick();
    void cancel() {
        if (live_ && stage_ == Stage::Running) {
            live_->cancel.store(true);
            cancelAsked_ = true;
            syncButtons();
            invalidate();
        }
    }
    [[nodiscard]] bool working() const noexcept { return stage_ == Stage::Running || stage_ == Stage::Verifying; }
    [[nodiscard]] bool running() const noexcept { return stage_ == Stage::Running; }   // le kit tourne (Annuler l'arrete)

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    [[nodiscard]] Workspace& ws() const { return host_.workspace(); }
    [[nodiscard]] std::function<bool()> aliveCheck() const {
        std::weak_ptr<bool> w = alive_;
        return [w] { return !w.expired(); };
    }
    void add(const std::string& line);
    void finished(const deployer::Report& rep);
    void verified(const OpResult& r, const online::ServiceState& st);
    void syncButtons();
    void notify() {
        if (done_) done_();
    }
    // Le resultat, en lignes (le titre d'abord) : le texte et son ton.
    struct Said {
        std::string text;
        Tone        tone{Tone::Info};
        bool        strong{false};
    };
    [[nodiscard]] std::vector<Said> resultText() const;

    DeployDialog&                 owner_;
    ShellHost                     host_;
    std::function<void()>         done_;
    Action                        action_{Action::Deploy};
    Stage                         stage_{Stage::Idle};
    std::shared_ptr<Live>         live_;
    std::vector<std::string>      log_;
    deployer::Report              report_;
    bool                          hasReport_{false};
    bool                          cancelAsked_{false};
    int                           phase_{0};        // 0 Node.js, 1 wrangler, 2 l'envoi, 3 la verification
    bool                          installing_{false};
    double                        started_{0.0}, ended_{0.0};
    std::string                   kitFolder_, kitVersion_;
    std::string                   version_;         // la version relue apres la mise en ligne
    std::string                   verifyProblem_;
    float                         scroll_{0.f};     // en lignes, depuis le bas (0 : le journal suit)
    Frame                         frame_;
    Rect                          statusRow_{}, stepsRow_{}, logBox_{}, resultBox_{};
    std::vector<std::pair<std::string, Said>> resultLines_;   // coupees a la largeur
    ui::Button*                   cancel_{nullptr};
    ui::Button*                   close_{nullptr};
    ui::Button*                   retry_{nullptr};
    ui::Button*                   login_{nullptr};
    ui::Button*                   again_{nullptr};
    ui::Button*                   link_{nullptr};
    ui::Button*                   copy_{nullptr};
    core::ConnectionScope         links_;
    std::shared_ptr<bool>         alive_{std::make_shared<bool>(true)};
};

class DeployDialog final : public Dialog {
public:
    DeployDialog(ShellHost host, std::function<void()> done) : Dialog("dialog.deploiement"), host_(std::move(host)), done_(std::move(done)) {}
    [[nodiscard]] std::string title() const override { return "D\xC3\xA9ployer le service en ligne"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        // Echap pendant le travail : l'arreter (le dialogue reste pour dire ce qui s'est passe).
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape && body_ && body_->running()) {
            body_->cancel();
            return ui::EventResult::Consumed;
        }
        return Dialog::HandleEvent(ev);
    }

protected:
    core::Status buildUi() override {
        auto body = std::make_unique<DeployBody>(*this, host_, done_);
        body_ = body.get();
        setRoot(std::move(body));
        body_->start(DeployBody::Action::Deploy);   // le clic sur << Deployer le service >> : c'est parti
        return core::ok();
    }
    void onTick() override {
        if (body_) body_->tick();
    }

private:
    ShellHost             host_;
    std::function<void()> done_;
    DeployBody*           body_{nullptr};
};

DeployBody::DeployBody(DeployDialog& owner, ShellHost host, std::function<void()> done)
    : ui::Widget("deploiement.corps"), owner_(owner), host_(std::move(host)), done_(std::move(done)) {
    cancel_ = &button(*this, "Annuler", "deploiement.annuler");
    close_ = &button(*this, "Fermer", "deploiement.fermer");
    retry_ = &button(*this, "R\xC3\xA9" "essayer", "deploiement.reessayer", ui::Button::Style::Default, ui::Icon::Refresh);
    login_ = &button(*this, "Se connecter \xC3\xA0 Cloudflare\xE2\x80\xA6", "deploiement.connecter", ui::Button::Style::Primary, ui::Icon::Lock);
    login_->setTooltip("npx wrangler login : ton navigateur s'ouvre sur Cloudflare ; connecte-toi, puis clique \xC2\xAB Allow \xC2\xBB. Une fois par PC.");
    again_ = &button(*this, "D\xC3\xA9ployer le service", "deploiement.deployer", ui::Button::Style::Primary, ui::Icon::ArrowUp);
    link_ = &button(*this, "Ouvrir nodejs.org", "deploiement.lien", ui::Button::Style::Primary, ui::Icon::Globe);
    copy_ = &button(*this, "Copier le journal", "deploiement.copier", ui::Button::Style::Default, ui::Icon::Copy);
    copy_->setTooltip("Le journal de cette mise en ligne (sans aucun secret : les jetons sont masqu\xC3\xA9s), pour ton int\xC3\xA9grateur.");
    links_ += cancel_->clicked->connect([this] { cancel(); });
    links_ += close_->clicked->connect([this] { owner_.close(); });
    links_ += retry_->clicked->connect([this] { start(Action::Deploy); });
    links_ += again_->clicked->connect([this] { start(Action::Deploy); });
    links_ += login_->clicked->connect([this] { start(Action::Login); });
    links_ += link_->clicked->connect([this] {
        if (report_.link.empty()) return;
        const std::string e = deployer::openLink(report_.link);
        if (!e.empty() && host_.flash) host_.flash(e, Tone::Warning);
    });
    links_ += copy_->clicked->connect([this] {
        std::string all;
        for (const auto& l : log_) all += l + "\n";
        ui::setClipboardText(all);
        if (host_.flash) host_.flash("Journal de la mise en ligne copi\xC3\xA9 (sans secret).", Tone::Info);
    });
    syncButtons();
}

void DeployBody::add(const std::string& line) {
    log_.push_back(line);
    if (log_.size() > kMaxLog) log_.erase(log_.begin(), log_.begin() + static_cast<std::ptrdiff_t>(log_.size() - kMaxLog));
    if (scroll_ > 0.f) scroll_ += 1.f;   // remonte par la molette : la vue reste ou elle est
    // L'etape, d'apres ce que dit deployer.mjs.
    if (has(line, "Node.js : ")) phase_ = std::max(phase_, 1);
    if (has(line, "Installation de wrangler")) {
        phase_ = std::max(phase_, 1);
        installing_ = true;
    }
    if (has(line, "Envoi \xC3\xA0 Cloudflare") || has(line, "Essai (rien n'est envoy")) phase_ = std::max(phase_, 2);
}

void DeployBody::start(Action a) {
    if (working()) return;
    action_ = a;
    report_ = deployer::Report{};
    hasReport_ = false;
    cancelAsked_ = false;
    version_.clear();
    verifyProblem_.clear();
    phase_ = 0;
    installing_ = false;
    scroll_ = 0.f;
    started_ = steadySeconds();
    ended_ = 0.0;
    if (!log_.empty()) add("");
    deployer::Options o;
    const deployer::Kit* k = deployer::installedKit(true);
    if (k) {
        kitFolder_ = k->folder;
        kitVersion_ = k->version;
        o.kit = k->folder;
        // cle-publique.txt : sans elle le service refuserait l'Admin ; l'admin+ l'ecrit (sa cle PUBLIQUE, rien de secret).
        if (a == Action::Deploy && !k->publicKey && ws().key().present) {
            const OpResult w = ws().writePublicKeyFile(k->folder);
            add(w.ok ? std::string("cle-publique.txt \xC3\xA9" "crite dans le kit (ta cl\xC3\xA9 PUBLIQUE, rien de secret).") : "cle-publique.txt : " + w.message);
            (void)deployer::installedKit(true);
        }
    }
    auto live = std::make_shared<Live>();
    live_ = live;
    auto rep = std::make_shared<deployer::Report>();
    const bool login = a == Action::Login;
    const auto alive = aliveCheck();
    stage_ = Stage::Running;
    syncButtons();
    const bool ok = host_.runSlow && host_.runSlow(login ? "Service : connexion \xC3\xA0 Cloudflare" : "Service : mise en ligne",
                                                   [o, live, rep, login] {
                                                       const deployer::LineSink sink = [live](const std::string& l) { live->push(l); };
                                                       *rep = login ? deployer::login(o, sink, &live->cancel) : deployer::deploy(o, sink, &live->cancel);
                                                       return resultOf(*rep, login);
                                                   },
                                                   [this, alive, rep](const OpResult&) {
                                                       if (alive()) finished(*rep);
                                                   });
    if (!ok && stage_ == Stage::Running) {   // un autre travail tourne (une synchro, une verification)
        stage_ = Stage::Done;
        report_.outcome = deployer::Outcome::Busy;
        report_.reason = "Un autre travail tourne (une synchro, une v\xC3\xA9rification) : attends qu'il finisse, puis R\xC3\xA9" "essayer.";
        hasReport_ = true;
        ended_ = steadySeconds();
    }
    syncButtons();
    invalidateLayout();
    invalidate();
}

void DeployBody::tick() {
    if (!live_) return;
    const auto fresh = live_->drain();
    for (const auto& l : fresh) add(l);
    if (!fresh.empty() || working()) invalidate();   // le temps qui passe, le point qui bat
}

void DeployBody::finished(const deployer::Report& rep) {
    tick();   // les dernieres lignes
    report_ = rep;
    hasReport_ = true;
    ended_ = steadySeconds();
    (void)deployer::installedKit(true);
    if (action_ == Action::Login || !rep.ok()) {
        stage_ = Stage::Done;
        syncButtons();
        invalidateLayout();
        notify();
        return;
    }
    // En ligne : l'adresse (reglee si le poste n'en avait pas), puis l'etat relu.
    Workspace& w = ws();
    if (!rep.url.empty() && !w.service().configured && w.service().problem.empty()) {
        const OpResult c = w.serviceConfigure(rep.url);
        add(c.ok ? "Adresse du service enregistr\xC3\xA9" "e dans l'Admin : " + w.service().config.url : "L'adresse n'est pas enregistr\xC3\xA9" "e : " + c.message);
    } else if (!rep.url.empty() && w.service().ready() && online::normalizeUrl(rep.url) != w.service().config.url) {
        add("Attention : la mise en ligne a donn\xC3\xA9 " + rep.url + ", et l'Admin parle \xC3\xA0 " + w.service().config.url + " (Service en ligne \xE2\x80\xBA Adresse).");
    }
    if (!w.service().ready() || !w.signer()) {
        verifyProblem_ = !w.service().ready() ? std::string("son adresse n'est pas r\xC3\xA9gl\xC3\xA9" "e dans l'Admin : colle-la dans Service en ligne, puis V\xC3\xA9rifier")
                                              : std::string("il faut la cl\xC3\xA9 de l'\xC3\xA9" "diteur ou une cl\xC3\xA9 d'admin pour lire son \xC3\xA9tat");
        stage_ = Stage::Done;
        syncButtons();
        invalidateLayout();
        notify();
        return;
    }
    stage_ = Stage::Verifying;
    phase_ = 3;
    syncButtons();
    add("Lecture de l'\xC3\xA9tat du service (/v1/etat)\xE2\x80\xA6");
    const Workspace* wp = &w;
    auto st = std::make_shared<online::ServiceState>();
    const auto alive = aliveCheck();
    ShellHost h = host_;
    const bool started = host_.runSlow && host_.runSlow("Service : v\xC3\xA9rification", [wp, st] { return wp->serviceVerify(st.get()); },
                                                        [this, h, alive, st, after = done_](const OpResult& r) {
                                                            h.workspace().adoptServiceState(*st);   // meme dialogue ferme : l'Admin le sait
                                                            if (alive()) verified(r, *st);
                                                            else if (after) after();
                                                        });
    if (!started && stage_ == Stage::Verifying) {
        verifyProblem_ = "un autre travail tourne : V\xC3\xA9rifier, dans Service en ligne, quand il a fini";
        stage_ = Stage::Done;
        syncButtons();
        notify();
    }
    invalidateLayout();
}

void DeployBody::verified(const OpResult& r, const online::ServiceState& st) {
    if (st.reached) {
        version_ = st.version.empty() ? std::string("?") : st.version;
        add("Service en ligne : version " + version_ + ".");
    } else {
        verifyProblem_ = r.message.rfind("Refus\xC3\xA9 : ", 0) == 0 ? r.message.substr(std::string("Refus\xC3\xA9 : ").size()) : r.message;
        add("L'\xC3\xA9tat du service ne se lit pas : " + verifyProblem_);
    }
    stage_ = Stage::Done;
    syncButtons();
    invalidateLayout();
    notify();
}

void DeployBody::syncButtons() {
    const bool done = stage_ == Stage::Done && hasReport_;
    const auto o = report_.outcome;
    const bool ok = done && report_.ok();
    const bool needLogin = done && o == deployer::Outcome::NotLoggedIn;
    const bool loginOk = ok && action_ == Action::Login;
    const bool hasLink = done && !report_.link.empty();
    show(cancel_, stage_ == Stage::Running);
    cancel_->setEnabled(!cancelAsked_);
    show(close_, done || stage_ == Stage::Idle || stage_ == Stage::Verifying);   // la verification : quelques secondes, fermer ne perd rien
    close_->setStyle(ok && !loginOk ? ui::Button::Style::Primary : ui::Button::Style::Default);
    show(login_, needLogin);
    show(again_, loginOk);
    show(link_, hasLink);
    if (hasLink) link_->setText(has(report_.link, "nodejs.org") ? "Ouvrir nodejs.org" : "Ouvrir Cloudflare");
    show(retry_, done && !ok && !needLogin);
    retry_->setStyle(hasLink ? ui::Button::Style::Default : ui::Button::Style::Primary);
    show(copy_, done && !log_.empty());
    invalidateLayout();
}

std::vector<DeployBody::Said> DeployBody::resultText() const {
    std::vector<Said> out;
    if (stage_ != Stage::Done || !hasReport_) return out;
    if (report_.ok() && action_ == Action::Deploy) {
        if (!version_.empty()) {
            out.push_back({"Service en ligne : version " + version_, Tone::Ok, true});
            const auto v = deployer::checkVersion(version_, kitVersion_);
            if (v.outdated) out.push_back({deployer::outdatedMessage(v), Tone::Warning, false});
        } else {
            out.push_back({"Service mis en ligne" + std::string(report_.url.empty() ? "" : " : " + report_.url), Tone::Ok, true});
            if (!verifyProblem_.empty()) out.push_back({"Son \xC3\xA9tat ne se lit pas encore : " + verifyProblem_ + ".", Tone::Warning, false});
        }
        if (!version_.empty() && !report_.url.empty()) out.push_back({"Adresse : " + report_.url, Tone::Info, false});
        if (report_.url.empty() && version_.empty() && verifyProblem_.empty()) out.push_back({report_.reason, Tone::Info, false});
        return out;
    }
    out.push_back({report_.reason, report_.ok() ? Tone::Ok : report_.outcome == deployer::Outcome::Cancelled ? Tone::Warning : Tone::Error, true});
    if (!report_.next.empty()) out.push_back({report_.next, Tone::Info, false});
    return out;
}

void DeployBody::onLayout() {
    frame_.place(*this, 820.f, 640.f);
    const Rect& p = frame_.panel;
    const float top = p.y + 76.f;   // le bas de l'en-tete (un sous-titre)
    statusRow_ = {p.x + 24.f, top + 14.f, p.w - 48.f, 22.f};
    stepsRow_ = {p.x + 24.f, top + 44.f, p.w - 48.f, 24.f};
    // Les boutons, en bas a droite, l'action principale tout a droite (Copier le journal a gauche).
    const float by = p.bottom() - 54.f;
    float x = p.right() - 24.f;
    for (ui::Button* b : {again_, login_, link_, retry_, close_, cancel_}) {
        if (!b->visible()) continue;
        const float w = buttonWidth(*b, 100.f);
        x -= w;
        b->setBounds({x, by, w, 34.f});
        x -= 8.f;
    }
    if (copy_->visible()) copy_->setBounds({p.x + 24.f, by, buttonWidth(*copy_, 120.f), 34.f});
    // Le resultat (fin), au-dessus des boutons.
    resultLines_.clear();
    const float textW = p.w - 48.f;
    float resultH = 0.f;
    for (const auto& s : resultText()) {
        const FontId f = s.strong ? gfx::semibold(kF15) : kF14;
        for (auto& l : wrap(s.text, f, textW)) {
            resultLines_.push_back({std::move(l), s});
            resultH += 21.f;
        }
        resultH += 4.f;
    }
    resultBox_ = resultLines_.empty() ? Rect{} : Rect{p.x + 24.f, by - 14.f - resultH, textW, resultH};
    const float logTop = stepsRow_.bottom() + 12.f;
    const float logBottom = (resultLines_.empty() ? by : resultBox_.y) - 12.f;
    logBox_ = {p.x + 24.f, logTop, p.w - 48.f, std::max(60.f, logBottom - logTop)};
}

void DeployBody::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto& c = th.color;
    std::string sub = kitFolder_.empty() ? std::string("Le kit install\xC3\xA9 avec l'Admin \xE2\x86\x92 ton compte Cloudflare")
                                         : "Le kit : " + kitFolder_ + (kitVersion_.empty() ? std::string() : " \xC2\xB7 service " + kitVersion_);
    (void)frame_.paint(r, th, owner_.title(), sub, ui::Icon::ArrowUp);

    // ---- l'etat : ce qui se fait, depuis quand ----
    const double now = steadySeconds();
    const double spent = (ended_ > 0.0 ? ended_ : now) - started_;
    std::string status;
    Color ink = c.text;
    if (stage_ == Stage::Running) {
        if (cancelAsked_) status = "Arr\xC3\xAAt en cours\xE2\x80\xA6";
        else if (action_ == Action::Login) status = "Connexion \xC3\xA0 Cloudflare : connecte-toi dans ton navigateur, puis clique \xC2\xAB Allow \xC2\xBB\xE2\x80\xA6";
        else if (phase_ == 0) status = "Recherche de Node.js\xE2\x80\xA6";
        else if (phase_ == 1) status = installing_ ? "Installation de wrangler (une seule fois : quelques minutes)\xE2\x80\xA6" : "Pr\xC3\xA9paration (ta cl\xC3\xA9 publique, wrangler)\xE2\x80\xA6";
        else status = "Envoi \xC3\xA0 Cloudflare\xE2\x80\xA6";
    } else if (stage_ == Stage::Verifying) {
        status = "V\xC3\xA9rification du service (/v1/etat)\xE2\x80\xA6";
    } else if (hasReport_) {
        const bool ok = report_.ok();
        status = action_ == Action::Login ? (ok ? std::string("Connect\xC3\xA9 \xC3\xA0 Cloudflare") : std::string("Pas connect\xC3\xA9"))
                 : ok                       ? std::string("Termin\xC3\xA9")
                 : report_.outcome == deployer::Outcome::Cancelled ? std::string("Arr\xC3\xAAt\xC3\xA9")
                                                                   : std::string("\xC3\x89" "chec");
        ink = ok ? c.ok : report_.outcome == deployer::Outcome::Cancelled ? c.warning : c.error;
    }
    {
        const float cy = statusRow_.y + statusRow_.h * 0.5f;
        if (working()) {
            const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(ctx.time * 4.0));
            r.fillRoundedRect({statusRow_.x, cy - 7.f, 14.f, 14.f}, c.accent.withAlpha(static_cast<std::uint8_t>(40 + 60 * pulse)), 7.f);
            r.fillRoundedRect({statusRow_.x + 3.f, cy - 4.f, 8.f, 8.f}, c.accent, 4.f);
        } else if (hasReport_) {
            r.fillRoundedRect({statusRow_.x + 3.f, cy - 4.f, 8.f, 8.f}, ink, 4.f);
        }
        const std::string t = status + (started_ > 0.0 ? " \xC2\xB7 " + clockOf(spent) : std::string());
        const FontId f = gfx::semibold(kF14);
        r.drawText({statusRow_.x + 22.f, statusRow_.y + (statusRow_.h - r.lineHeight(f)) * 0.5f}, fit(r, t, f, statusRow_.w - 22.f), f, ink);
    }

    // ---- les etapes ----
    if (action_ == Action::Deploy) {
        static const char* const names[] = {"Node.js", "wrangler", "Envoi \xC3\xA0 Cloudflare", "V\xC3\xA9rification"};
        const bool failed = hasReport_ && stage_ == Stage::Done && !report_.ok();
        const bool allDone = hasReport_ && stage_ == Stage::Done && report_.ok();
        float x = stepsRow_.x;
        for (int i = 0; i < 4; ++i) {
            std::string mark;
            Color fg = c.textMuted, bg = c.rowAltBg;
            if (allDone || i < phase_) {
                mark = "\xE2\x9C\x93 ";
                fg = c.ok;
                bg = c.ok.withAlpha(24);
            } else if (i == phase_ && failed) {
                mark = "\xC3\x97 ";
                fg = c.error;
                bg = c.error.withAlpha(24);
            } else if (i == phase_ && working()) {
                mark = "\xE2\x80\xA2 ";
                fg = c.accent;
                bg = c.accent.withAlpha(24);
            }
            const std::string t = mark + std::to_string(i + 1) + ". " + names[i];
            if (x + pillWidth(r, t, kF13) > stepsRow_.right()) break;
            x += pill(r, x, stepsRow_.y + 1.f, t, kF13, fg, bg) + 8.f;
        }
    }

    // ---- le journal ----
    const Color logBg = th.brand.codeBg.a ? th.brand.codeBg : c.inputBg;
    r.fillRoundedRect(logBox_, c.border, 8.f);
    r.fillRoundedRect(logBox_.inset(1.f, 1.f), logBg, 7.f);
    {
        const Rect inner = logBox_.inset(12.f, 8.f);
        const int visible = std::max(1, static_cast<int>(inner.h / kLogLine));
        const int n = static_cast<int>(log_.size());
        const int maxScroll = std::max(0, n - visible);
        const int sc = std::clamp(static_cast<int>(scroll_), 0, maxScroll);
        const int first = std::max(0, n - visible - sc);
        r.pushClip(logBox_.inset(1.f, 1.f));
        if (log_.empty())
            r.drawText({inner.x, inner.y}, working() ? std::string("En attente de la premi\xC3\xA8re ligne\xE2\x80\xA6") : std::string("Rien \xC3\xA0 montrer."), kLogFont, c.textMuted);
        float y = inner.y;
        for (int i = first; i < n && i < first + visible; ++i) {
            const std::string& l = log_[static_cast<std::size_t>(i)];
            const bool bad = l.rfind("ERREUR", 0) == 0 || has(l, "[ERROR]") || has(l, "\xE2\x9C\x98");
            r.drawText({inner.x, y}, fit(r, l, kLogFont, inner.w), kLogFont, bad ? c.error : c.text);
            y += kLogLine;
        }
        r.popClip();
        if (sc > 0) {
            const std::string more = "\xE2\x86\x93 " + std::to_string(sc) + (sc > 1 ? " lignes plus bas" : " ligne plus bas");
            const float w = r.measure(more, kF13).width + 16.f;
            pill(r, logBox_.right() - w - 10.f, logBox_.bottom() - 30.f, more, kF13, c.textMuted, c.panelBg);
        }
    }

    // ---- le resultat ----
    float y = resultBox_.y;
    for (const auto& [line, s] : resultLines_) {
        const FontId f = s.strong ? gfx::semibold(kF15) : kF14;
        const Color col = s.tone == Tone::Ok ? c.ok : s.tone == Tone::Error ? c.error : s.tone == Tone::Warning ? c.warning : c.text;
        r.drawText({resultBox_.x, y}, line, f, col);
        y += 21.f;
    }
}

ui::EventResult DeployBody::onEvent(const ui::InputEvent& ev) {
    if (frame_.hitClose(ev)) {
        if (running()) cancel();   // la croix pendant la mise en ligne : l'arreter d'abord
        else owner_.close();
        return ui::EventResult::Consumed;
    }
    if (const auto* wh = std::get_if<ui::MouseWheel>(&ev); wh && logBox_.contains(wh->pos)) {
        const int visible = std::max(1, static_cast<int>((logBox_.h - 16.f) / kLogLine));
        const float maxScroll = static_cast<float>(std::max(0, static_cast<int>(log_.size()) - visible));
        scroll_ = std::clamp(scroll_ + wh->dy * 3.f, 0.f, maxScroll);
        invalidate();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace

menu::MenuPtr makeDeployDialog(ShellHost host, std::function<void()> done) { return std::make_unique<DeployDialog>(std::move(host), std::move(done)); }

} // namespace license_admin::gui
