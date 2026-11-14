// =============================================================================
//  license_admin/gui/AccountInvite.cpp - 1.52.0 : MON COMPTE, << TON CODE
//  D'INVITATION >> ET << TON AJOUT >> (XPG Admin ; AccountInvite.hpp)
// -----------------------------------------------------------------------------
//  La carte qu'avait Administrateurs (PageAdmins.cpp, 1.51.0), venue dans Mon
//  compte : XPG Admin sans derogation n'a plus Administrateurs. Le code tape n'est
//  jamais dans un message ni un journal ; le champ se vide des que le code est
//  accepte.
// =============================================================================
#include "AccountInvite.hpp"

#include "AdminsKit.hpp"

#include "../Edition.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

namespace license_admin::gui {

namespace {

using namespace kit;

constexpr float kTop = 24.f;
constexpr FontId kF16{16}, kF14{14}, kF13{13}, kF12{12};

std::string trimmedCode(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

// Une phrase : une majuscule, un point.
std::string sentence(std::string s) {
    if (s.rfind("Refus\xC3\xA9 : ", 0) == 0) s = s.substr(10);
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    if (!s.empty() && s.back() != '.' && s.back() != '!' && s.back() != '?') s += '.';
    return s;
}

} // namespace

InviteCard::InviteCard(ShellHost host) : ui::Widget("compte.invitation"), host_(std::move(host)) {
    code_ = &adopt(*this, std::make_unique<ui::InputText>("compte.invitation.code"));
    code_->setPlaceholder("INV-XXXX-XXXX");
    code_->setMaxLength(20);
    join_ = &button(*this, "Devenir admin", "compte.invitation.valider", ui::Button::Style::Primary, ui::Icon::User);
    service_ = &button(*this, "R\xC3\xA9gler le service\xE2\x80\xA6", "compte.invitation.service", ui::Button::Style::Default, ui::Icon::Globe);
    service_->setTooltip("Mon compte \xE2\x80\xBA Ce PC : l'adresse du service de l'\xC3\xA9" "diteur (sa connexion sign\xC3\xA9" "e, ou l'adresse qu'il t'a donn\xC3\xA9" "e).");
    check_ = &button(*this, "V\xC3\xA9rifier maintenant", "compte.attente.verifier", ui::Button::Style::Default, ui::Icon::Refresh);
    check_->setTooltip("Demande au service o\xC3\xB9 en est ton ajout (XPG Admin le regarde d\xC3\xA9j\xC3\xA0 seul, toutes les 15 s).");
    links_ += join_->clicked->connect([this] { join(); });
    links_ += code_->editingDone->connect([this](const std::string&) { join(); });
    links_ += service_->clicked->connect([this] {
        if (host_.navigate) host_.navigate("poste");
    });
    links_ += check_->clicked->connect([this] { check(); });
    refresh();
}

InviteCard::~InviteCard() {
    *alive_ = false;
    links_.clear();
}

InviteCard::Mode InviteCard::mode() const {
    if (!license_admin::classique()) return Mode::Hidden;   // l'Admin+ : jamais (docs/ADMINS.md, par. 12)
    const Role role = ws().role();
    if (role == Role::None) return Mode::Code;
    if (state_ == "invitation" && !problem_.empty() && role != Role::Admin) return Mode::Code;   // un code refuse : il se retape
    if (role != Role::Pending) return Mode::Hidden;
    if (state_ == "aucune" || state_ == "invitation") return Mode::Code;
    if (state_.empty() && !ws().joined() && ws().account().joinedAt == 0) return Mode::Code;   // une cle sans inscription connue
    return Mode::Waiting;
}

bool InviteCard::wanted() const { return mode() != Mode::Hidden; }

void InviteCard::refresh() {
    const std::string f = takePageFocus("compte.invitation");
    if (f.rfind("code-refuse:", 0) == 0) {   // Shell::autoJoin : le code de l'installateur refuse au premier lancement
        problem_ = sentence(f.substr(12));
        state_ = "invitation";
    }
    if (ws().role() == Role::Admin) {   // admis : plus rien a dire
        problem_.clear();
        state_.clear();
    }
    sync();
}

void InviteCard::sync() {
    const Mode m = mode();
    if (m == Mode::Hidden)   // la carte cachee : ses widgets aussi (les essais, --cliquer : jamais un champ d'une carte absente)
        for (ui::Widget* wd : {static_cast<ui::Widget*>(code_), static_cast<ui::Widget*>(join_), static_cast<ui::Widget*>(service_), static_cast<ui::Widget*>(check_)})
            show(wd, false);
    fileCode_ = m == Mode::Code && state_ != "invitation" ? ws().pendingInvitationCode() : std::string();
    const bool busy = host_.busy && host_.busy();
    const bool configured = ws().service().configured;
    join_->setEnabled(configured && !busy);
    join_->setTooltip(configured ? std::string("Ta cl\xC3\xA9 d'admin na\xC3\xAEt sur ce PC ; ton code d'invitation accept\xC3\xA9, tu es admin.")
                                 : std::string("Il faut d'abord l'adresse du service des licences (R\xC3\xA9gler le service\xE2\x80\xA6)."));
    check_->setEnabled(configured && !busy && !checking_);
    invalidateLayout();
    invalidate();
}

float InviteCard::wantedHeight(float width) const {
    (void)width;
    switch (mode()) {
        case Mode::Hidden: return 0.f;
        case Mode::Waiting: return 236.f;
        case Mode::Code: break;
    }
    float h = 218.f;
    if (!problem_.empty()) h += 40.f;
    if (!ws().service().configured) h += 60.f;
    return h + 8.f + 38.f + 22.f;
}

void InviteCard::onLayout() {
    const Rect b = bounds();
    const float fx = b.x + 24.f;
    const Mode m = mode();
    show(code_, false);
    show(join_, false);
    show(service_, false);
    show(check_, false);
    if (m == Mode::Waiting) {
        check_->setBounds({fx, b.bottom() - 60.f, buttonWidth(*check_, 170.f), 38.f});
        show(check_, true);
        return;
    }
    if (m != Mode::Code) return;
    if (fileCode_.empty()) {
        code_->setBounds({fx, b.y + 172.f, std::min(b.w - 48.f, 260.f), 36.f});
        show(code_, true);
    }
    join_->setBounds({fx, b.bottom() - 60.f, buttonWidth(*join_, 170.f), 38.f});
    show(join_, true);
    if (!ws().service().configured) {
        service_->setBounds({join_->bounds().right() + 10.f, b.bottom() - 60.f, buttonWidth(*service_, 150.f), 38.f});
        show(service_, true);
    }
}

void InviteCard::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto& c = th.color;
    const Rect b = bounds();
    const Mode m = mode();
    if (m == Mode::Hidden) return;
    cardBox(r, th, b);
    const float fx = b.x + 24.f, fw = b.w - 48.f;
    const Rect ib{fx, b.y + kTop, 40.f, 40.f};
    r.fillRoundedRect(ib, rgb(0x0E7490), 11.f);
    ui::drawIcon(r, ui::Icon::User, {ib.x + 11.f, ib.y + 11.f, 18.f, 18.f}, rgb(0xFFFFFF));
    if (m == Mode::Waiting) {
        r.drawText({fx + 54.f, b.y + 26.f}, "Ton ajout comme admin", gfx::semibold(kF16), c.text);
        r.drawText({fx + 54.f, b.y + 48.f}, fit(r, "ton code d'invitation est accept\xC3\xA9 \xC2\xB7 l'Admin+ signe ta cl\xC3\xA9 d'office", kF13, fw - 54.f), kF13, c.textMuted);
        (void)drawWrapped(r, fx, b.y + 82.f, fw,
                          "L'Admin+ de l'\xC3\xA9" "diteur ajoute ta cl\xC3\xA9 \xC3\xA0 la liste sign\xC3\xA9" "e des admins d\xC3\xA8s qu'il est en ligne : rien \xC3\xA0 faire, rien "
                          "\xC3\xA0 lui dire. Tes droits s'ouvrent alors tout seuls, puis l'archive arrive.",
                          kF14, c.text, 21.f, 3);
        std::string st = "XPG Admin regarde seul, toutes les 15 s.";
        if (lastCheck_) st = "V\xC3\xA9rifi\xC3\xA9 " + agoFr(ws().now(), lastCheck_) + ". " + st;
        (void)drawWrapped(r, fx, b.y + 150.f, fw, st, kF13, c.textMuted, 19.f, 2);
        return;
    }
    r.drawText({fx + 54.f, b.y + 26.f}, "Ton code d'invitation", gfx::semibold(kF16), c.text);
    r.drawText({fx + 54.f, b.y + 48.f}, fit(r, "ta cl\xC3\xA9 na\xC3\xAEt sur ce PC \xC2\xB7 le code fait de toi un admin", kF13, fw - 54.f), kF13, c.textMuted);
    (void)drawWrapped(r, fx, b.y + 82.f, fw,
                      "Le code est dans le message de l'admin+ (INV-XXXX-XXXX, valable 48 h, une seule fois). Accept\xC3\xA9, tu es admin avec le profil qu'il "
                      "a choisi : ni demande, ni code \xC3\xA0 lui dire.",
                      kF14, c.text, 21.f, 3);
    float below = b.y + 218.f;
    if (!fileCode_.empty()) {
        r.drawText({fx, b.y + 150.f}, "Code d'invitation", gfx::semibold(kF13), c.text);
        r.drawText({fx, b.y + 172.f}, fit(r, "pos\xC3\xA9 par l'installateur : \xC2\xAB Devenir admin \xC2\xBB l'envoie", kF14, fw), kF14, c.textMuted);
    } else {
        r.drawText({fx, code_->bounds().y - 20.f}, "Code d'invitation", gfx::semibold(kF13), c.text);
        r.drawText({code_->bounds().right() + 12.f, code_->bounds().y + 10.f},
                   fit(r, "dans le message de l'admin+ (48 h)", kF12, std::max(10.f, fx + fw - code_->bounds().right() - 12.f)), kF12, c.textMuted);
    }
    if (!problem_.empty()) {
        (void)drawWrapped(r, fx, below, fw, problem_, kF13, c.error, 18.f, 2);
        below += 40.f;
    }
    if (!ws().service().configured)
        (void)drawWrapped(r, fx, below, fw,
                          "Il faut d'abord l'adresse du service des licences (celle que l'admin+ t'a donn\xC3\xA9" "e, ou sa connexion sign\xC3\xA9" "e) : "
                          "\xC2\xAB R\xC3\xA9gler le service\xE2\x80\xA6 \xC2\xBB.",
                          kF13, c.warning, 19.f, 3);
}

// XPG Admin - devenir admin avec le code d'invitation (celui de l'installateur, sinon le champ) ; un code refuse (inconnu,
// expire, deja servi) : la phrase du service, le code se retape.
void InviteCard::join() {
    const std::string typed = trimmedCode(code_->text());
    const std::string code = typed.empty() ? ws().pendingInvitationCode() : typed;
    if (code.empty()) {
        problem_ = "Tape ton code d'invitation (INV-XXXX-XXXX) : il est dans le message de l'admin+.";
        state_ = "invitation";
        sync();
        return;
    }
    const Workspace* w = &ws();
    auto alive = alive_;
    if (!host_.runSlow || !host_.runSlow("Devenir admin (ton code d'invitation)", [w, code] { return w->joinWithInvitation(code, nullptr); },
                                         [this, alive](const OpResult& r) {
                                             if (!*alive) return;
                                             if (r.ok) {
                                                 problem_.clear();
                                                 code_->setText({});
                                                 state_ = r.output == "admis" ? std::string("admis") : std::string("inscrit");
                                                 lastCheck_ = ws().now();
                                             } else {
                                                 problem_ = sentence(r.message);
                                                 state_ = "invitation";
                                             }
                                             sync();
                                         })) {
        if (host_.flash) host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
    }
}

void InviteCard::check() {
    if (checking_) return;
    const Workspace* w = &ws();
    auto x = std::make_shared<AdminRefresh>();
    auto alive = alive_;
    checking_ = true;
    if (!host_.runSlow || !host_.runSlow("Ton ajout comme admin", [w, x] { return w->refreshAdminState(nullptr, x.get()); },
                                         [this, alive, x](const OpResult&) {
                                             if (!*alive) return;
                                             checking_ = false;
                                             state_ = x->admitted ? std::string("admis") : x->joinState;
                                             lastCheck_ = ws().now();
                                             sync();
                                         })) {
        checking_ = false;
        if (host_.flash) host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
    }
    sync();
}

} // namespace license_admin::gui
