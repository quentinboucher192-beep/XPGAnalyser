// =============================================================================
//  license_admin/gui/PageAccount.cpp - 1.47.0 : LA PAGE << Mon compte >>
// -----------------------------------------------------------------------------
//  La premiere entree de Securite (et le bloc d'identite en haut a gauche y
//  mene) : pour chaque role - l'admin+, un admin, un admin retire, en attente
//  d'acceptation, pas encore de cle - ce que ce poste sait de lui, sans reseau
//  (Workspace::account) :
//    - QUI : le nom, le courriel (sa demande d'admin), le role, ce PC ;
//    - L'IDENTIFIANT : l'identifiant de la cle (xpg-...), l'empreinte en 6
//      groupes (celle qui se lit au telephone), << Copier >> l'un et l'autre ;
//    - L'ETAT DU COMPTE : actif depuis, ajoute par l'admin+ dans la liste
//      signee n°, en attente depuis, retire le, la liste en vigueur ici, le
//      certificat du service (s'il a ete lu), ce PC, la version de l'Admin et
//      celle du service, et << Gerer le service >> (l'admin+ seul, ou une
//      derogation de courte duree : l'emplacement de l'agent S, plus bas) ;
//    - LES POUVOIRS : les 13 droits et la boutique - permis ou non, ce que
//      chacun permet, sa criticite (celle en vigueur), le profil (Admin LV1,
//      LV2...) et les ecarts au profil, la derniere fois qu'il a servi (le
//      journal des admins de ce poste : license_admin/JournalStats) ; l'admin+ :
//      tout, et ce que lui seul fait ;
//    - L'HISTOIRE DE SES DROITS : les listes signees vues sur ce poste, l'une
//      apres l'autre (ce qui a ete ajoute, retire, par l'admin+).
//  Les gestes sont ceux qui existent : copier l'identifiant et l'empreinte,
//  Sauvegarder ma cle... (l'admin+ : Parametres de securite), Demander a
//  devenir admin... (sans cle : Administrateurs), l'ecran d'attente (en attente).
// =============================================================================
#include "AdminsKit.hpp"
#include "Page.hpp"

#include "../JournalStats.hpp"
#include "../../deploy/Crypto.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/ScrollBar.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace {

namespace adm = licensing::admins;
namespace js = license_admin::journalstats;
using gfx::Color;
using gfx::FontId;
using gfx::Rect;
using kit::fit;

constexpr float kPad = 24.f;
constexpr float kGap = 16.f;
constexpr float kRowH = 26.f;
constexpr float kPowerH = 30.f;

// ------------------------------------------------------------ [EMPLACEMENT agent S]
// LA DEROGATION << Gerer le service >> (l'admin+ l'accorde a un admin, 72 h au plus, signee, tenue par le service) :
// l'agent S l'ecrit. Quand son API existe, la brancher ICI (et seulement ici) : rendre la derogation en cours (a qui,
// jusqu'a quand). Tant qu'elle n'y est pas, `known` reste faux et la page dit << l'admin+ seul gere le service >>.
struct Derogation {
    bool        known{false};    // l'API de S est branchee
    bool        active{false};   // une derogation court
    std::string who, whoKey;     // l'admin qui l'a
    Seconds     until{0};        // la fin (l'heure du service)
};
Derogation derogationOf(const Workspace& w) {
    (void)w;   // [EMPLACEMENT agent S] : ex. w.serviceDerogation() -> {known=true, active, who, whoKey, until}
    return {};
}
// ------------------------------------------------------------ [fin de l'emplacement]

// Ce que chaque droit permet (une phrase), et les actions du journal qui le disent (sa criticite, sa derniere fois).
struct PowerInfo {
    const char*              id;
    const char*              allows;
    std::vector<const char*> actions;
};
const std::vector<PowerInfo>& powerInfos() {
    static const std::vector<PowerInfo> v = {
        {"licence_creer", "signer une licence neuve (li\xC3\xA9" "e \xC3\xA0 des PC, ou en ligne avec un code)", {"licence_creer"}},
        {"licence_prolonger", "repousser la fin d'une licence", {"licence_prolonger"}},
        {"licence_option", "ajouter une option \xC3\xA0 une licence", {"licence_option"}},
        {"licence_revoquer", "r\xC3\xA9voquer une licence : ses PC la perdent", {"licence_revoquer"}},
        {"poste_retirer", "retirer un PC d'une licence li\xC3\xA9" "e", {"poste_retirer"}},
        {"offres", "changer les offres : prix, fonctions incluses (le registre)", {"offres"}},
        {"options", "cr\xC3\xA9" "er, modifier une option du catalogue", {"options"}},
        {"annonces", "publier une notification g\xC3\xA9n\xC3\xA9rale dans les Studios", {"annonces"}},
        {"demandes", "accepter, refuser une demande de licence en ligne", {"demande_accepter", "demande_refuser"}},
        {"codes", "revoir le code d'activation d'une licence en ligne", {"code_revoir"}},
        {"client_supprimer", "supprimer un client (il se restaure)", {"client_supprimer", "client_restaurer"}},
        {"clients_exporter", "exporter les clients (un CSV)", {"clients_exporter"}},
        {"courriels", "\xC3\xA9" "crire aux clients par courriel", {"courriel"}},
    };
    return v;
}
const PowerInfo kShopPower{"boutique", "livrer une commande pay\xC3\xA9" "e, la marquer \xC3\xA0 revoir (il faut Accepter une demande et Cr\xC3\xA9" "er une licence)",
                           {"commande_livrer", "commande_revoir"}};

// Une ligne de l'etat du compte.
struct StateRow {
    std::string label, value;
    ui::Tone    tone{ui::Tone::None};
    bool        mono{false};
};
// Un pouvoir (un droit, la boutique) tel que la page le montre.
struct PowerRow {
    std::string id, label, allows;
    bool        held{false};
    adm::Level  level{adm::Level::Info};
    std::string profile;      // << dans le profil >>, << en plus du profil >>, << retir\xC3\xA9 du profil >>, vide
    bool        deviation{false};
    std::string lastUse;      // << auj. 10:12 >>, << jamais >>, << \xE2\x80\x94 >>
};
// Un pas de l'histoire des droits.
struct StepRow {
    std::string when, text;
    ui::Tone    tone{ui::Tone::None};
};

std::string rightName(const std::string& id) {
    if (id == "boutique") return "Boutique : livrer, revoir une commande";
    const std::string l = adm::rightLabel(id);
    return l.empty() ? id : l;
}

adm::Level levelOf(const Criticality& crit, const std::vector<const char*>& actions) {
    adm::Level best = adm::Level::Info;
    for (const char* a : actions) {
        adm::Level l = adm::Level::Info;
        if (const auto it = crit.actions.find(a); it != crit.actions.end()) l = it->second.level;
        else if (const adm::ActionType* t = adm::actionType(a)) l = t->level;
        if (static_cast<int>(l) > static_cast<int>(best)) best = l;
    }
    return best;
}

std::string joinRights(const std::vector<std::string>& ids) {
    std::string t;
    for (const auto& id : ids) t += (t.empty() ? "" : ", ") + rightName(id);
    return t;
}

void paintGroups(gfx::IRenderer& r, const ui::Theme& th, const Rect& box, const std::string& fingerprint, FontId f) {
    const auto g = kit::groups(fingerprint);
    if (g.empty()) return;
    const float gap = 8.f;
    const float cw = (box.w - gap * static_cast<float>(g.size() - 1)) / static_cast<float>(g.size());
    for (std::size_t i = 0; i < g.size(); ++i) {
        const Rect cell{box.x + static_cast<float>(i) * (cw + gap), box.y, cw, box.h};
        ui::shapes::fillFramedRect(r, cell, th.color.panelBg, th.color.borderStrong, 8.f);
        r.drawText({cell.x + (cell.w - r.measure(g[i], f).width) * 0.5f, cell.y + (cell.h - r.lineHeight(f)) * 0.5f}, g[i], f, th.color.text);
    }
}

// =================================================================================== la page
class AccountPage final : public Page {
public:
    explicit AccountPage(ShellHost host) : Page("page.compte", std::move(host)) {
        copyId_ = &kit::button(*this, "Copier", "compte.copier-id", ui::Button::Style::Default, ui::Icon::Copy);
        copyId_->setTooltip("L'identifiant de ta cl\xC3\xA9 (xpg-\xE2\x80\xA6) au presse-papiers.");
        copyFp_ = &kit::button(*this, "Copier l'empreinte", "compte.copier-empreinte", ui::Button::Style::Default, ui::Icon::Copy);
        copyFp_->setTooltip("Les 6 groupes de ton empreinte au presse-papiers (celle que l'admin+ lit au t\xC3\xA9l\xC3\xA9phone).");
        backup_ = &kit::button(*this, "Sauvegarder ma cl\xC3\xA9\xE2\x80\xA6", "compte.sauvegarder", ui::Button::Style::Default, ui::Icon::Save);
        backup_->setTooltip("Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9 : la sauvegarde chiffr\xC3\xA9" "e de la cl\xC3\xA9 de l'\xC3\xA9" "diteur (une phrase).");
        ask_ = &kit::button(*this, "Demander \xC3\xA0 devenir admin\xE2\x80\xA6", "compte.devenir-admin", ui::Button::Style::Primary, ui::Icon::User);
        ask_->setTooltip("Administrateurs : ton nom, ton courriel ; ta cl\xC3\xA9 est cr\xC3\xA9" "\xC3\xA9" "e sur ce PC et l'admin+ re\xC3\xA7oit ta demande.");
        createKey_ = &kit::button(*this, "Cr\xC3\xA9" "er la cl\xC3\xA9 de l'\xC3\xA9" "diteur\xE2\x80\xA6", "compte.creer-cle", ui::Button::Style::Default, ui::Icon::Lock);
        createKey_->setTooltip("Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9 : la cl\xC3\xA9 qui signe les licences (l'admin+, une seule fois).");
        waiting_ = &kit::button(*this, "Voir ma demande", "compte.attente", ui::Button::Style::Default, ui::Icon::History);
        waiting_->setTooltip("L'\xC3\xA9" "cran d'attente : ton empreinte en grand, \xC3\xA0 lire \xC3\xA0 l'admin+ au t\xC3\xA9l\xC3\xA9phone.");
        links_ += copyId_->clicked->connect([this] { copy("Identifiant copi\xC3\xA9 : ", info_.keyId); });
        links_ += copyFp_->clicked->connect([this] { copy("Empreinte copi\xC3\xA9" "e : ", info_.fingerprint); });
        links_ += backup_->clicked->connect([this] { go("securite"); });
        links_ += ask_->clicked->connect([this] { go("admins"); });
        links_ += createKey_->clicked->connect([this] { go("securite"); });
        links_ += waiting_->clicked->connect([this] { go("admins"); });
        refresh();
    }
    ~AccountPage() override { links_.clear(); }

    [[nodiscard]] std::string title() const override { return "Mon compte"; }

    void refresh() override {
        info_ = ws().account();
        build();
        invalidateLayout();
        invalidate();
    }

    // Les essais : ce que la page dit (une ligne par chose), les pouvoirs, l'etat.
    [[nodiscard]] const AccountInfo& info() const noexcept { return info_; }
    [[nodiscard]] const std::vector<PowerRow>& powers() const noexcept { return powers_; }
    [[nodiscard]] const std::vector<StateRow>& stateRows() const noexcept { return state_; }
    [[nodiscard]] const std::vector<StepRow>& history() const noexcept { return steps_; }
    [[nodiscard]] std::string text() const {
        std::string t = roleText_ + "\n" + info_.name + "\n";
        for (const auto& s : state_) t += s.label + " : " + s.value + "\n";
        t += powersHead_ + "\n" + profileLine_ + "\n";
        for (const auto& p : powers_) t += (p.held ? "[x] " : "[ ] ") + p.label + " | " + p.profile + " | " + p.lastUse + "\n";
        for (const auto& s : reserved_) t += "[admin+] " + s + "\n";
        for (const auto& s : steps_) t += s.when + " " + s.text + "\n";
        if (!historyNote_.empty()) t += historyNote_ + "\n";
        if (!lastNote_.empty()) t += lastNote_ + "\n";
        return t;
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float w = std::max(320.f, std::min(b.w - 2.f * kPad - 14.f, 1180.f));
        const float x = b.x + kPad;
        float y = b.y + 20.f - offset_;
        titleY_ = y;
        y += 70.f;
        // QUI, L'IDENTIFIANT : cote a cote (assez large), sinon l'un sous l'autre
        const bool side = w >= 860.f;
        const float hId = info_.keyHex.empty() ? 150.f : 196.f;
        if (side) {
            const float lw = std::floor(w * 0.40f);
            who_ = {x, y, lw, hId};
            id_ = {x + lw + kGap, y, w - lw - kGap, hId};
            y += hId + kGap;
        } else {
            who_ = {x, y, w, 150.f};
            y += 150.f + kGap;
            id_ = {x, y, w, hId};
            y += hId + kGap;
        }
        // les boutons de l'identifiant
        const bool hasKey = !info_.keyHex.empty();
        place(copyId_, {id_.right() - 20.f - kit::buttonWidth(*copyId_, 90.f), id_.y + 40.f, kit::buttonWidth(*copyId_, 90.f), 30.f}, hasKey);
        place(copyFp_, {id_.right() - 20.f - kit::buttonWidth(*copyFp_, 150.f), id_.bottom() - 46.f, kit::buttonWidth(*copyFp_, 150.f), 30.f}, hasKey);
        // L'ETAT DU COMPTE : ses lignes, puis les gestes du role
        const std::vector<ui::Button*> acts = actions();
        const float stateH = 52.f + static_cast<float>(state_.size()) * kRowH + (acts.empty() ? 12.f : 56.f);
        stateBox_ = {x, y, w, stateH};
        float bx = x + 20.f;
        for (ui::Button* btn : {backup_, ask_, createKey_, waiting_}) {
            const bool on = std::find(acts.begin(), acts.end(), btn) != acts.end();
            const float bw = kit::buttonWidth(*btn, 120.f);
            place(btn, {bx, stateBox_.bottom() - 46.f, bw, 32.f}, on);
            if (on) bx += bw + 8.f;
        }
        y += stateH + kGap;
        // LES POUVOIRS
        const float powersH = 92.f + static_cast<float>(powers_.size()) * kPowerH + (reserved_.empty() ? 0.f : 40.f + static_cast<float>(reserved_.size()) * 24.f) +
                              (lastNote_.empty() ? 8.f : 30.f);
        powersBox_ = {x, y, w, powersH};
        y += powersH + kGap;
        // L'HISTOIRE DES DROITS
        const float histH = 52.f + static_cast<float>(steps_.size()) * 24.f + (historyNote_.empty() ? 8.f : 30.f);
        historyBox_ = {x, y, w, histH};
        y += histH + 24.f;
        content_ = y + offset_ - b.y;
        scroll_.place(b, content_, b.h);
        const float c = scroll_.clamp(offset_);
        if (c != offset_) {
            offset_ = c;
            onLayout();
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Rect b = bounds();
        r.fillRect(b, c.windowBg);   // comme Administrateurs : le fond de la fenetre, des cartes blanches
        r.pushClip(b);
        const float x = b.x + kPad;
        r.drawText({x, titleY_}, "Mon compte", th.font.title, c.text);
        r.drawText({x, titleY_ + 38.f}, fit(r, lead(), FontId{14}, b.w - 2.f * kPad), FontId{14}, c.textMuted);
        paintWho(ctx);
        paintId(ctx);
        paintState(ctx);
        paintPowers(ctx);
        paintHistory(ctx);
        r.popClip();
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (scroll_.needed()) scroll_.paint(ctx, offset_);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        bool changed = false;
        if (scroll_.handle(*this, ev, offset_, &changed)) {
            if (changed) invalidateLayout();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    [[nodiscard]] std::string lead() const {
        switch (info_.role) {
            case Role::Plus: return "Toi, l'admin+ : la cl\xC3\xA9 de l'\xC3\xA9" "diteur, qui signe tout - les licences, la liste des admins. Tout se lit ici, sans r\xC3\xA9seau.";
            case Role::Admin: return "Ce que ce poste sait de toi : ta cl\xC3\xA9 d'admin, ton compte dans la liste sign\xC3\xA9" "e par l'admin+, tes pouvoirs.";
            case Role::Retired: return "Ta cl\xC3\xA9 a \xC3\xA9t\xC3\xA9 retir\xC3\xA9" "e de la liste des admins : elle ne signe plus rien. Ce qu'elle a sign\xC3\xA9 avant reste valable.";
            case Role::Pending: return "Ta demande pour devenir admin attend l'admin+ : il lit ton empreinte au t\xC3\xA9l\xC3\xA9phone, puis l'accepte.";
            case Role::None: break;
        }
        return "Pas encore de cl\xC3\xA9 sur ce poste : demande \xC3\xA0 devenir admin, ou cr\xC3\xA9" "e la cl\xC3\xA9 de l'\xC3\xA9" "diteur si tu es l'admin+.";
    }

    [[nodiscard]] std::vector<ui::Button*> actions() const {
        switch (info_.role) {
            case Role::Plus: return {backup_};
            case Role::Pending: return {waiting_};
            case Role::None: return {ask_, createKey_};
            case Role::Admin:
            case Role::Retired: break;
        }
        return {};
    }

    void place(ui::Widget* w, const Rect& r, bool on) {
        if (!on) {
            w->setVisibility(ui::Visibility::Collapsed);
            return;
        }
        w->setBounds(r);
        const Rect b = bounds();
        w->setVisibility(r.bottom() > b.y && r.y < b.bottom() ? ui::Visibility::Visible : ui::Visibility::Hidden);
    }

    void go(const std::string& page) {
        if (host_.navigate) host_.navigate(page);
    }
    void copy(const std::string& what, const std::string& text) {
        if (text.empty()) return;
        ui::setClipboardText(text);
        if (host_.flash) host_.flash(what + text, Tone::Ok);
    }

    // ---- ce qu'on montre (refresh) ----
    void build() {
        const Workspace& w = ws();
        const Seconds now = w.now();
        state_.clear();
        powers_.clear();
        reserved_.clear();
        steps_.clear();
        historyNote_.clear();
        lastNote_.clear();
        profileLine_.clear();
        const AccountInfo& a = info_;
        roleText_ = roleLabel(a.role);
        switch (a.role) {
            case Role::Plus: roleTone_ = ui::Tone::Info; break;
            case Role::Admin: roleTone_ = ui::Tone::Ok; break;
            case Role::Pending: roleTone_ = ui::Tone::Warning; break;
            case Role::Retired: roleTone_ = ui::Tone::Error; break;
            case Role::None: roleTone_ = ui::Tone::None; break;
        }
        const std::string listN = a.listNumber > 0 ? "n\xC2\xB0 " + std::to_string(a.listNumber) : std::string();
        // ---- l'etat du compte ----
        switch (a.role) {
            case Role::Plus:
                state_.push_back({"\xC3\x89tat", "actif \xE2\x80\x94 la cl\xC3\xA9 de l'\xC3\xA9" "diteur" + std::string(a.testKey ? " (cl\xC3\xA9 d'ESSAI : jamais pour des clients)" : ""),
                                  a.testKey ? ui::Tone::Warning : ui::Tone::Ok});
                state_.push_back({"Liste des admins", a.listNumber > 0 ? "tu signes la liste " + listN + " (en vigueur sur ce poste)"
                                                                       : std::string("pas encore de liste sign\xC3\xA9" "e (aucun admin)")});
                break;
            case Role::Admin:
                state_.push_back({"\xC3\x89tat", "actif", ui::Tone::Ok});
                if (a.since) state_.push_back({"Admin depuis", dateTimeFr(a.since) + " (" + kit::agoFr(now, a.since) + ")"});
                state_.push_back({"Ajout\xC3\xA9 par", "l'admin+, dans la liste des admins sign\xC3\xA9" "e " +
                                                           (a.addedInList > 0 ? "n\xC2\xB0 " + std::to_string(a.addedInList) : std::string("(son num\xC3\xA9ro n'est pas not\xC3\xA9 ici)"))});
                state_.push_back({"Liste en vigueur ici", listN.empty() ? std::string("\xE2\x80\x94") : listN});
                break;
            case Role::Retired:
                state_.push_back({"\xC3\x89tat", "retir\xC3\xA9 \xE2\x80\x94 ta cl\xC3\xA9 ne signe plus rien", ui::Tone::Error});
                if (a.since) state_.push_back({"Admin depuis", dateTimeFr(a.since)});
                if (a.addedInList > 0) state_.push_back({"Ajout\xC3\xA9 par", "l'admin+, liste n\xC2\xB0 " + std::to_string(a.addedInList)});
                state_.push_back({"Retir\xC3\xA9 le", (a.retired ? dateTimeFr(a.retired) : std::string("\xE2\x80\x94")) + ", par l'admin+" +
                                                          (listN.empty() ? std::string() : " (liste " + listN + ")"), ui::Tone::Error});
                break;
            case Role::Pending:
                state_.push_back({"\xC3\x89tat", "en attente d'acceptation par l'admin+", ui::Tone::Warning});
                if (a.requestSent) state_.push_back({"Demande envoy\xC3\xA9" "e le", dateTimeFr(a.requestSent) + " (" + kit::agoFr(now, a.requestSent) + ")"});
                state_.push_back({"Ce qu'il reste \xC3\xA0 faire", "lis ton empreinte \xC3\xA0 l'admin+ au t\xC3\xA9l\xC3\xA9phone : il l'accepte, ce poste le voit tout seul"});
                break;
            case Role::None:
                state_.push_back({"\xC3\x89tat", a.keyHex.empty() ? std::string("pas de cl\xC3\xA9 sur ce poste") : std::string("une cl\xC3\xA9 d'admin, pas dans la liste ni en attente"),
                                  ui::Tone::None});
                break;
        }
        // le certificat du service (lu par Verifier, ou au lancement)
        const auto& sv = w.service();
        if (sv.hasState && sv.state.reached) {
            if (sv.state.certOk && sv.state.certExpires)
                state_.push_back({"Certificat du service", "valable jusqu'au " + dateFr(sv.state.certExpires) + " (sign\xC3\xA9 par la cl\xC3\xA9 de l'\xC3\xA9" "diteur)", ui::Tone::Ok});
            else if (!sv.state.certProblem.empty())
                state_.push_back({"Certificat du service", sv.state.certProblem, ui::Tone::Warning});
            else
                state_.push_back({"Certificat du service", sv.state.certified ? std::string("d\xC3\xA9pos\xC3\xA9") : std::string("aucun"), sv.state.certified ? ui::Tone::None : ui::Tone::Warning});
        } else if (sv.configured) {
            state_.push_back({"Certificat du service", "pas encore lu dans cette s\xC3\xA9" "ance (Service en ligne \xE2\x80\xBA V\xC3\xA9rifier)"});
        }
        std::string pc = a.pc.empty() ? std::string("(sans nom)") : a.pc;
        if (!a.machine.empty()) pc += " \xC2\xB7 machine " + a.machine.substr(0, 8) + "\xE2\x80\xA6";
        if (!a.listPc.empty() && a.listPc != a.pc) pc += " (la liste conna\xC3\xAEt \xC2\xAB " + a.listPc + " \xC2\xBB)";
        state_.push_back({"Ce PC", pc});
        std::string versions = std::string("Admin ") + kAppVersion + " \xC2\xB7 ";
        if (sv.hasState && sv.state.reached && !sv.state.version.empty())
            versions += "service " + sv.state.version + (sv.stateAt ? " (lu " + kit::whenFr(now, sv.stateAt) + ")" : std::string());
        else versions += sv.configured ? std::string("service pas encore lu") : std::string("service pas r\xC3\xA9gl\xC3\xA9");
        state_.push_back({"Versions", versions});
        // [EMPLACEMENT agent S] la derogation << Gerer le service >>
        const Derogation d = derogationOf(w);
        if (d.known && d.active && d.until > now) {
            const Seconds left = d.until - now;
            const std::string rest = left >= 3600 ? std::to_string(left / 3600) + " h " + std::to_string(left % 3600 / 60) + " min" : std::to_string(std::max<Seconds>(1, left / 60)) + " min";
            const bool mine = !a.keyHex.empty() && d.whoKey == a.keyHex;
            state_.push_back({"G\xC3\xA9rer le service", (mine ? std::string("d\xC3\xA9rogation en cours pour toi") : "d\xC3\xA9rogation en cours pour " + d.who) +
                                                           " : encore " + rest + " (jusqu'\xC3\xA0 " + kit::clockFr(d.until) + ")", ui::Tone::Warning});
        } else {
            state_.push_back({"G\xC3\xA9rer le service", a.role == Role::Plus ? std::string("toi : l'admin+ seul g\xC3\xA8re le service (aucune d\xC3\xA9rogation en cours)")
                                                                               : std::string("l'admin+ seul g\xC3\xA8re le service")});
        }
        buildPowers(now);
        buildHistory(now);
    }

    void buildPowers(Seconds now) {
        const Workspace& w = ws();
        const AccountInfo& a = info_;
        const Criticality crit = w.criticality();
        const bool plus = a.role == Role::Plus;
        const std::set<std::string> held(a.rights.begin(), a.rights.end());
        // le profil et ses droits
        std::optional<std::set<std::string>> inProfile;
        if (plus) {
            profileLine_ = "Profil : \xC2\xAB " + std::string(adm::kAllRightsProfile) + " \xC2\xBB \xE2\x80\x94 l'admin+ a tous les pouvoirs, toujours.";
        } else if (a.role == Role::Admin || a.role == Role::Retired) {
            if (a.profile.empty()) profileLine_ = "Aucun profil : tes droits ont \xC3\xA9t\xC3\xA9 choisis un par un par l'admin+.";
            else {
                const auto all = w.profiles();
                const auto it = std::find_if(all.begin(), all.end(), [&](const adm::Profile& p) { return p.name == a.profile; });
                if (it == all.end()) profileLine_ = "Profil \xC2\xAB " + a.profile + " \xC2\xBB (pas connu sur ce poste : les \xC3\xA9" "carts ne se calculent pas).";
                else inProfile = std::set<std::string>(it->rights.begin(), it->rights.end());
            }
        } else {
            profileLine_ = a.role == Role::Pending ? "Aucun pouvoir tant que l'admin+ n'a pas accept\xC3\xA9 ta demande (il choisit ton profil)."
                                                   : "Aucun pouvoir : pas de cl\xC3\xA9 sur ce poste.";
        }
        // la derniere fois (le journal des admins de ce poste)
        js::Inputs in;
        in.lines = js::linesOf(w.journalLines());
        std::vector<std::string> editors;
        for (const auto& k : w.editorKeys()) editors.push_back(deploy::crypto::hex(k));
        in.admins = js::adminsOf(w.adminList() ? *w.adminList() : adm::AdminList{}, editors, a.name.empty() ? std::string("admin+") : a.name);
        in.now = now;
        in.period = js::lastDays(now, 30);
        std::string sid = plus ? std::string("admin+") : a.keyId;
        const bool journal = !in.lines.empty() && !a.keyHex.empty();
        js::AdminStats st;
        if (journal) st = js::adminStats(in, sid);
        std::set<std::string> keys;
        for (const auto& ad : in.admins)
            if (ad.id == sid) keys.insert(ad.keys.begin(), ad.keys.end());
        const auto lastOf = [&](const std::vector<const char*>& actions) -> Seconds {
            Seconds t = 0;
            for (const auto& l : in.lines)
                if (keys.count(l.key))
                    for (const char* x : actions)
                        if (l.action == x) t = std::max(t, l.at());
            return t;
        };
        int deviations = 0, count = 0;
        const auto add = [&](const PowerInfo& p, bool has, bool shop) {
            PowerRow row;
            row.id = p.id;
            row.label = rightName(p.id);
            row.allows = p.allows;
            row.held = has;
            row.level = levelOf(crit, p.actions);
            if (shop) row.profile = "d'apr\xC3\xA8s tes droits";
            else if (plus) row.profile = "tous les droits";
            else if (inProfile) {
                const bool in = inProfile->count(p.id) > 0;
                if (in && has) row.profile = "dans le profil";
                else if (!in && has) row.profile = "en plus du profil", row.deviation = true;
                else if (in && !has) row.profile = "retir\xC3\xA9 du profil", row.deviation = true;
                else row.profile = "pas dans le profil";
            } else row.profile = "\xE2\x80\x94";
            if (row.deviation) ++deviations;
            if (!journal) row.lastUse = "\xE2\x80\x94";
            else {
                Seconds t = 0;
                if (!shop)
                    for (const auto& u : st.rights)
                        if (u.id == p.id) t = u.last;
                if (t == 0) t = lastOf(p.actions);
                row.lastUse = t ? kit::whenFr(now, t) : std::string("jamais");
            }
            powers_.push_back(std::move(row));
        };
        for (const auto& p : powerInfos()) {
            const bool has = plus || held.count(p.id) > 0;
            count += has ? 1 : 0;
            add(p, has, false);
        }
        add(kShopPower, plus || (held.count("demandes") && held.count("licence_creer")), true);
        powersHead_ = std::to_string(count) + " sur 13" + (powers_.back().held ? std::string(" \xC2\xB7 la boutique aussi") : std::string());
        if (inProfile) {
            profileLine_ = "Profil \xC2\xAB " + a.profile + " \xC2\xBB" +
                           (deviations == 0 ? std::string(" \xE2\x80\x94 aucun \xC3\xA9" "cart") : " \xE2\x80\x94 " + std::to_string(deviations) + (deviations > 1 ? " \xC3\xA9" "carts" : " \xC3\xA9" "cart") + " (ajust\xC3\xA9 pour toi seul)");
        }
        if (plus) {
            for (const auto& s : kit::plusOnly()) reserved_.push_back(s);
            reserved_.push_back("G\xC3\xA9rer le service : d\xC3\xA9ployer, certifier, brancher Stripe, sauvegarder et restaurer");
        }
        if (!journal && (a.role == Role::Admin || a.role == Role::Retired))
            lastNote_ = "La derni\xC3\xA8re utilisation se lit au journal des admins : il est chez l'admin+ (pas sur ce poste).";
        else if (journal && plus)
            lastNote_ = "La derni\xC3\xA8re utilisation : le journal des admins relev\xC3\xA9 sur ce poste (" + std::to_string(in.lines.size()) + " lignes).";
        // l'admin+ : l'histoire des droits des autres est au journal (la sienne ne change pas)
        if (plus) historyChanges_ = {};
        else historyChanges_ = journal ? st.rightsChanges : std::vector<js::RightsChange>{};
    }

    void buildHistory(Seconds now) {
        (void)now;
        const AccountInfo& a = info_;
        if (a.role == Role::Plus) {
            historyNote_ = "Tes pouvoirs ne changent pas : l'admin+ a tout. Les droits des admins changent dans Administrateurs \xE2\x80\xBA Droits (au journal des admins).";
            return;
        }
        if (a.role == Role::None || a.role == Role::Pending) {
            historyNote_ = "Pas encore d'histoire : elle commence quand l'admin+ t'ajoute \xC3\xA0 la liste des admins.";
            return;
        }
        // les listes vues sur ce poste, l'une apres l'autre
        const AccountStep* prev = nullptr;
        for (const auto& s : a.steps) {
            if (!s.listed) {
                prev = nullptr;
                continue;
            }
            StepRow row;
            row.when = s.issued ? dateTimeFr(s.issued) : std::string("\xE2\x80\x94");
            const std::string head = "liste n\xC2\xB0 " + std::to_string(s.number) + ", par l'admin+ : ";
            if (!prev) {
                row.text = head + (s.active ? "admin, " + std::to_string(s.rights.size()) + " droits sur 13" + (s.profile.empty() ? std::string() : " (profil \xC2\xAB " + s.profile + " \xC2\xBB)")
                                            : std::string("retir\xC3\xA9"));
                row.tone = s.active ? ui::Tone::Ok : ui::Tone::Error;
            } else if (prev->active && !s.active) {
                row.text = head + "retir\xC3\xA9 de la liste des admins";
                row.tone = ui::Tone::Error;
            } else {
                std::vector<std::string> added, removed;
                for (const auto& r : s.rights)
                    if (std::find(prev->rights.begin(), prev->rights.end(), r) == prev->rights.end()) added.push_back(r);
                for (const auto& r : prev->rights)
                    if (std::find(s.rights.begin(), s.rights.end(), r) == s.rights.end()) removed.push_back(r);
                if (added.empty() && removed.empty() && s.profile == prev->profile) {
                    prev = &s;
                    continue;   // une liste qui ne le change pas (un autre admin)
                }
                std::string t;
                if (!added.empty()) t += "+ " + joinRights(added);
                if (!removed.empty()) t += (t.empty() ? "" : " ; ") + std::string("\xE2\x88\x92 ") + joinRights(removed);
                if (s.profile != prev->profile) t += (t.empty() ? "" : " ; ") + std::string("profil \xC2\xAB ") + (s.profile.empty() ? std::string("aucun") : s.profile) + " \xC2\xBB";
                row.text = head + t;
                row.tone = removed.empty() ? ui::Tone::Ok : ui::Tone::Warning;
            }
            steps_.push_back(std::move(row));
            prev = &s;
        }
        // le journal (s'il est la) : qui a change quoi, ce que les listes ne disent pas
        for (const auto& ch : historyChanges_) {
            if (ch.joined) continue;   // deja dit par sa premiere liste
            std::string t = "au journal, par " + (ch.by.empty() ? std::string("l'admin+") : ch.by) + " : ";
            if (ch.left) t += "retir\xC3\xA9";
            else {
                if (!ch.added.empty()) t += "+ " + joinRights(ch.added);
                if (!ch.removed.empty()) t += (ch.added.empty() ? "" : " ; ") + std::string("\xE2\x88\x92 ") + joinRights(ch.removed);
            }
            steps_.push_back({dateTimeFr(ch.line.when), t, ch.left ? ui::Tone::Error : ui::Tone::None});
        }
        if (steps_.empty()) historyNote_ = "Aucune liste des admins n'est not\xC3\xA9" "e sur ce poste pour l'instant.";
        else historyNote_ = "D'apr\xC3\xA8s les listes sign\xC3\xA9" "es vues sur ce poste (seul l'admin+ les signe).";
    }

    // ---- dessiner ----
    void cardTitle(gfx::IRenderer& r, const ui::Theme& th, const Rect& box, const std::string& title, const std::string& sub = {}) const {
        const FontId f = gfx::semibold(FontId{12});
        r.drawText({box.x + 20.f, box.y + 18.f}, title, f, th.color.textMuted);
        if (!sub.empty()) {
            const float tw = r.measure(title, f).width;
            r.drawText({box.x + 28.f + tw, box.y + 17.f}, fit(r, sub, FontId{13}, box.w - 60.f - tw), FontId{13}, th.color.text);
        }
    }

    void paintWho(const ui::PaintContext& ctx) const {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        kit::cardBox(r, th, who_);
        cardTitle(r, th, who_, "QUI");
        const Rect av{who_.x + 20.f, who_.y + 46.f, 52.f, 52.f};
        const std::string name = info_.name.empty() ? std::string("Toi") : info_.name;
        kit::avatar(r, av, name, kit::avatarColor(th, info_.role == Role::Plus ? 0 : info_.role == Role::Retired ? -1 : 1), 14.f);
        const float tx = av.right() + 16.f, tw = who_.right() - 20.f - tx;
        r.drawText({tx, av.y + 2.f}, fit(r, name, gfx::semibold(FontId{20}), tw), gfx::semibold(FontId{20}), c.text);
        const Color ink = th.tone(roleTone_, c.accent);
        kit::pill(r, tx, av.y + 32.f, roleText_, FontId{12}, ink, ink.withAlpha(26), 22.f, true);
        float y = av.bottom() + 16.f;
        const std::string mail = info_.email.empty() ? std::string(info_.role == Role::Plus ? "\xE2\x80\x94 (la cl\xC3\xA9 de l'\xC3\xA9" "diteur n'a pas de courriel)" : "\xE2\x80\x94") : info_.email;
        r.drawText({who_.x + 20.f, y}, "Courriel", FontId{13}, c.textMuted);
        r.drawText({who_.x + 110.f, y}, fit(r, mail, FontId{13}, who_.w - 130.f), FontId{13}, c.text);
    }

    void paintId(const ui::PaintContext& ctx) const {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        kit::cardBox(r, th, id_);
        cardTitle(r, th, id_, "TON IDENTIFIANT");
        if (info_.keyHex.empty()) {
            kit::drawWrapped(r, id_.x + 20.f, id_.y + 48.f, id_.w - 40.f,
                             "Pas encore de cl\xC3\xA9 sur ce poste. Demander \xC3\xA0 devenir admin la cr\xC3\xA9" "e (elle ne quitte jamais ce PC) ; l'admin+ cr\xC3\xA9" "e la cl\xC3\xA9 de l'\xC3\xA9" "diteur "
                             "dans Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9.",
                             FontId{14}, c.textMuted, 20.f, 4);
            return;
        }
        r.drawText({id_.x + 20.f, id_.y + 46.f}, "Identifiant de la cl\xC3\xA9", FontId{12}, c.textMuted);
        const float idw = copyId_->bounds().x - 12.f - (id_.x + 20.f);
        r.drawText({id_.x + 20.f, id_.y + 64.f}, fit(r, info_.keyId, FontId{15}, idw), FontId{15}, c.text);
        r.drawText({id_.x + 20.f, id_.y + 96.f},
                   info_.role == Role::Plus ? "Empreinte \xE2\x80\x94 6 groupes, celle que les admins comparent"
                                            : "Empreinte \xE2\x80\x94 6 groupes, \xC3\xA0 lire \xC3\xA0 l'admin+ au t\xC3\xA9l\xC3\xA9phone",
                   FontId{12}, c.textMuted);
        const float gw = copyFp_->bounds().x - 16.f - (id_.x + 20.f);
        paintGroups(r, th, {id_.x + 20.f, id_.y + 116.f, std::max(240.f, gw), 40.f}, info_.fingerprint, gfx::semibold(FontId{18}));
        if (info_.testKey) r.drawText({id_.x + 20.f, id_.y + 166.f}, "Cl\xC3\xA9 d'ESSAI : jamais pour des clients.", FontId{12}, c.warning);
    }

    void paintState(const ui::PaintContext& ctx) const {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        kit::cardBox(r, th, stateBox_);
        cardTitle(r, th, stateBox_, "L'\xC3\x89TAT DU COMPTE");
        float y = stateBox_.y + 48.f;
        const float lx = stateBox_.x + 20.f, vx = stateBox_.x + 230.f, vw = stateBox_.right() - 20.f - vx;
        for (const auto& s : state_) {
            r.drawText({lx, y}, s.label, FontId{13}, c.textMuted);
            const Color ink = s.tone == ui::Tone::None ? c.text : th.tone(s.tone, c.text);
            r.drawText({vx, y}, fit(r, s.value, s.tone == ui::Tone::None ? FontId{14} : gfx::semibold(FontId{14}), vw), s.tone == ui::Tone::None ? FontId{14} : gfx::semibold(FontId{14}), ink);
            y += kRowH;
        }
    }

    void paintPowers(const ui::PaintContext& ctx) const {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Rect& b = powersBox_;
        kit::cardBox(r, th, b);
        cardTitle(r, th, b, "TES POUVOIRS", powersHead_);
        r.drawText({b.x + 20.f, b.y + 42.f}, fit(r, profileLine_, FontId{13}, b.w - 40.f), FontId{13}, c.text);
        // les colonnes
        const float x0 = b.x + 20.f, wAll = b.w - 40.f;
        const float cLabel = x0 + 26.f, cAllows = x0 + 26.f + std::min(300.f, wAll * 0.27f);
        const float cLast = b.right() - 20.f - 130.f, cProfile = cLast - 170.f, cLevel = cProfile - 112.f;
        float y = b.y + 70.f;
        const FontId fh = gfx::semibold(FontId{11});
        r.drawText({cLabel, y}, "POUVOIR", fh, c.textMuted);
        r.drawText({cAllows, y}, "CE QU'IL PERMET", fh, c.textMuted);
        r.drawText({cLevel, y}, "CRITICIT\xC3\x89", fh, c.textMuted);
        r.drawText({cProfile, y}, "PROFIL", fh, c.textMuted);
        r.drawText({cLast, y}, "DERNI\xC3\x88RE FOIS", fh, c.textMuted);
        y += 22.f;
        for (std::size_t i = 0; i < powers_.size(); ++i) {
            const PowerRow& p = powers_[i];
            if (i == powers_.size() - 1) r.fillRect({x0, y - 1.f, wAll, 1.f}, c.border);   // la boutique, a part
            if (p.held) kit::drawTick(r, x0 + 2.f, y + 7.f, c.ok);
            else kit::drawCross(r, x0 + 3.f, y + 8.f, c.textMuted);
            const FontId fl = p.held ? gfx::semibold(FontId{13}) : FontId{13};
            r.drawText({cLabel, y + 5.f}, fit(r, p.label, fl, cAllows - cLabel - 10.f), fl, p.held ? c.text : c.textMuted);
            r.drawText({cAllows, y + 5.f}, fit(r, p.allows, FontId{13}, cLevel - cAllows - 10.f), FontId{13}, c.textMuted);
            const kit::Tint t = kit::levelTint(p.level);
            kit::pill(r, cLevel, y + 3.f, adm::levelLabel(p.level), FontId{12}, t.fg, t.bg, 22.f);
            r.drawText({cProfile, y + 5.f}, fit(r, p.profile, p.deviation ? gfx::semibold(FontId{13}) : FontId{13}, cLast - cProfile - 10.f),
                       p.deviation ? gfx::semibold(FontId{13}) : FontId{13}, p.deviation ? c.warning : c.textMuted);
            r.drawText({cLast, y + 5.f}, fit(r, p.lastUse, FontId{13}, b.right() - 20.f - cLast), FontId{13}, c.text);
            y += kPowerH;
        }
        if (!reserved_.empty()) {
            y += 12.f;
            r.drawText({x0, y}, "CE QUE L'ADMIN+ FAIT SEUL", fh, c.textMuted);
            y += 22.f;
            for (const auto& s : reserved_) {
                ui::drawIcon(r, ui::Icon::Lock, {x0, y + 2.f, 14.f, 14.f}, c.accent);
                r.drawText({cLabel, y}, fit(r, s, FontId{13}, wAll - 30.f), FontId{13}, c.text);
                y += 24.f;
            }
        }
        if (!lastNote_.empty()) r.drawText({x0, y + 8.f}, fit(r, lastNote_, FontId{12}, wAll), FontId{12}, c.textMuted);
    }

    void paintHistory(const ui::PaintContext& ctx) const {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Rect& b = historyBox_;
        kit::cardBox(r, th, b);
        cardTitle(r, th, b, "L'HISTOIRE DE TES DROITS");
        float y = b.y + 46.f;
        for (const auto& s : steps_) {
            r.drawText({b.x + 20.f, y}, s.when, FontId{13}, c.textMuted);
            const Color ink = s.tone == ui::Tone::None ? c.text : th.tone(s.tone, c.text);
            r.drawText({b.x + 170.f, y}, fit(r, s.text, FontId{13}, b.w - 190.f), FontId{13}, ink);
            y += 24.f;
        }
        if (!historyNote_.empty()) r.drawText({b.x + 20.f, y + 4.f}, fit(r, historyNote_, FontId{12}, b.w - 40.f), FontId{12}, c.textMuted);
    }

    core::ConnectionScope              links_;
    AccountInfo                        info_;
    std::string                        roleText_, powersHead_, profileLine_, historyNote_, lastNote_;
    ui::Tone                           roleTone_{ui::Tone::None};
    std::vector<StateRow>              state_;
    std::vector<PowerRow>              powers_;
    std::vector<std::string>           reserved_;
    std::vector<StepRow>               steps_;
    std::vector<js::RightsChange>      historyChanges_;
    ui::Button*                        copyId_{nullptr};
    ui::Button*                        copyFp_{nullptr};
    ui::Button*                        backup_{nullptr};
    ui::Button*                        ask_{nullptr};
    ui::Button*                        createKey_{nullptr};
    ui::Button*                        waiting_{nullptr};
    ui::ScrollArea                     scroll_;
    float                              offset_{0.f}, content_{0.f}, titleY_{0.f};
    Rect                               who_{}, id_{}, stateBox_{}, powersBox_{}, historyBox_{};
};

} // namespace

PagePtr makeAccountPage(ShellHost host) { return std::make_unique<AccountPage>(std::move(host)); }

} // namespace license_admin::gui
