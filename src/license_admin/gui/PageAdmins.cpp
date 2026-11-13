// =============================================================================
//  license_admin/gui/PageAdmins.cpp - 1.44.0 : LA PAGE << ADMINISTRATEURS >>
//  (maquette 1.43.0, planches 01 et 02 ; docs/ADMINS.md)
// -----------------------------------------------------------------------------
//  1.47.0 (maquette 1.47.0, planches 01 a 05 ; AdminsBoard.hpp) : l'ADMIN+ voit une periode commune comparee a celle
//  d'avant, et deux onglets - LA VUE D'ENSEMBLE (les tuiles, le classement trie par colonne avec sa barre de filtres,
//  son clic droit et sa ligne depliee, les alertes et leur geste : PageAdminsOverview.cpp) et QUI FAIT QUOI
//  (PageAdminsWho.cpp) ; Droits et Journal des admins y menent. Une ligne : << Ouvrir sa fiche >> (double-clic,
//  Entree) - LA FICHE D'UN ADMIN (PageAdminCard.cpp) sous son en-tete (ses ancres, la periode). Un admin : la liste en
//  lecture seule et << Ma fiche >> (la sienne seulement). Ce qui suit (1.44.0) reste : ajouter, retirer, les droits.
//  1.51.0 : PLUS DE DEMANDE D'ADMIN - << Ajouter un admin... >> est une seule etape (son nom, son profil, le PC de son
//  Studio trouve par une recherche, ou un lien) : son code d'invitation fait de lui un admin des l'installation de XPG
//  Admin (l'Admin+ signe sa cle d'office, Workspace::admitInvitedAdmins).
//  Une page, quatre visages selon le role du poste (Workspace::role) :
//    - l'ADMIN+ (la cle de l'editeur) : le tableau des admins (lui d'abord, puis
//      chaque admin de la liste signee), la fiche de celui choisi (ses chiffres
//      au journal, sa cle en 6 groupes, son certificat, ses droits, << Voir son
//      journal >>, << Ses droits... >>, << Retirer cet admin... >> et sa
//      confirmation qui propose << Revoquer aussi ses licences >>), et << Ajouter
//      un admin... >> : une etape (1.51.0) - l'invitation part, son code fait de
//      lui un admin des l'installation ;
//    - un ADMIN (ou un admin retire) : la meme liste en LECTURE SEULE (qui est
//      admin, sur quel PC, son etat), et ses droits a lui ;
//    - PAS ENCORE ADMIN (aucune cle) : l'Admin+, << Creer la cle de l'editeur >>
//      (le premier PC, Parametres de securite) ; XPG Admin (1.51.0), << Ton code
//      d'invitation >> (celui de l'installateur, sinon a taper :
//      Workspace::joinWithInvitation) - accepte, il est admin ;
//    - AJOUT EN COURS (1.51.0) : son code est accepte, l'Admin+ signe sa cle
//      d'office des qu'il est en ligne ; la coquille regarde toutes les 15 s et
//      passe le poste admin des que c'est fait. Rien a lire, rien a dire.
//  Ce qui parle au service (la liste et ce qu'il en sait, inviter, retirer, les
//  inscrits signes d'office) passe par runSlow (ou runQuiet : le releve
//  automatique), jamais sur le fil de l'interface. Les tableaux sont dessines ;
//  un tableau cache (<< admins.table >>) porte la meme liste pour les scripts
//  (--choisir admins.table=N) et les essais.
// =============================================================================
#include "AdminsBoard.hpp"   // 1.47.0 : les pages des admins (planches 01 a 05)
#include "AdminsKit.hpp"
#include "ListKit.hpp"
#include "Page.hpp"
#include "PcPicker.hpp"   // 1.51.0 : chercher le PC d'un futur admin

#include "../../deploy/DevKey.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/DatePicker.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace license_admin::gui {

// ============================================================ la page voulue ====
namespace {
std::map<std::string, std::string>& focusRequests() {
    static std::map<std::string, std::string> m;
    return m;
}
} // namespace

void requestPageFocus(const std::string& pageId, const std::string& what) { focusRequests()[pageId] = what; }

std::string takePageFocus(const std::string& pageId) {
    auto& m = focusRequests();
    const auto it = m.find(pageId);
    if (it == m.end()) return {};
    std::string out = std::move(it->second);
    m.erase(it);
    return out;
}

namespace {

using namespace kit;
using gfx::Color;
using gfx::FontId;
using gfx::Rect;

constexpr FontId kF28{28}, kF16{16}, kF15{15}, kF14{14}, kF13{13}, kF12{12}, kF11{11};

std::string trimmed(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}
std::string count(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// Un tableau de textes (le tableau cache des scripts).
class TextRows final : public ui::ITableModel {
public:
    TextRows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows) : headers_(std::move(headers)), rows_(std::move(rows)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string(); }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string();
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }

private:
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;
};

// Ce qu'un droit permet, en quelques mots (la fiche : << Licences : creer, prolonger >>).
std::string shortRight(const std::string& id) {
    if (id == "licence_creer") return "cr\xC3\xA9" "er";
    if (id == "licence_prolonger") return "prolonger";
    if (id == "licence_option") return "ajouter une option";
    if (id == "licence_revoquer") return "r\xC3\xA9voquer";
    if (id == "poste_retirer") return "retirer un PC";
    if (id == "offres") return "les offres";
    if (id == "options") return "les options";
    if (id == "annonces") return "les annonces";
    if (id == "demandes") return "les commandes et demandes";   // 1.48.1 : une seule page
    if (id == "codes") return "revoir les codes";
    if (id == "client_supprimer") return "supprimer un client";
    if (id == "clients_exporter") return "exporter les clients";
    if (id == "courriels") return "\xC3\xA9" "crire aux clients";
    return adm::rightLabel(id);
}

// Une ligne du tableau : l'admin+, un admin de la liste (actif ou retire). 1.51.0 : plus de demande recue.
struct AdminRow {
    enum class Kind { Plus, Admin } kind{Kind::Admin};
    std::string              key, id, name, pc, machine, fingerprint, profile, email, software, version;
    Seconds                  since{0}, retired{0}, lastAction{0}, seen{0};
    std::vector<std::string> rights;
    std::string              lastSummary;
    int                      actions30{0}, licences{0};
    bool                     me{false}, mismatch{false};
    int                      color{0};
    int                      order{0};   // 1.47.0 : son rang dans la liste (retires compris) : sa couleur dans les pages des admins
    [[nodiscard]] bool active() const { return retired == 0; }
};

// Ce que le service a dit (GET /admin/admins ; 1.51.0, l'admin+ : les inscrits signes d'office juste avant).
struct Fetched {
    bool        haveView{false};
    AdminsView  view;
    std::string problem;
};

// 1.47.0 : le nom de l'admin+ sans la marque d'une cle d'essai (<< CLE D'ESSAI ... (Camille Essai) >> -> << Camille Essai >>).
std::string keyOwnerName(const std::string& keyName) {
    if (keyName.rfind("CLE D'ESSAI", 0) != 0) return keyName;
    const auto a = keyName.rfind('('), b = keyName.rfind(')');
    if (a != std::string::npos && b != std::string::npos && b > a + 1) return keyName.substr(a + 1, b - a - 1);
    return "admin+ (cl\xC3\xA9 d'essai)";
}

std::string thisPc() {
    std::string n = deploy::DevKey::computerName();
    return n.empty() ? std::string("ce PC") : n;
}

// La fiche et la lecture seule : les droits d'un admin par groupe (permis, puis << pas : ... >>).
struct RightsLine {
    std::string group, allowed, missing;
};
std::vector<RightsLine> rightsLines(const std::vector<std::string>& rights) {
    std::vector<RightsLine> out;
    for (const char* g : {"licences", "offres", "clients"}) {
        RightsLine l;
        l.group = adm::groupLabel(g);
        for (const auto& r : adm::rights()) {
            if (std::string(r.group) != g) continue;
            const bool has = std::find(rights.begin(), rights.end(), r.id) != rights.end();
            std::string& s = has ? l.allowed : l.missing;
            s += (s.empty() ? "" : ", ") + shortRight(r.id);
        }
        out.push_back(std::move(l));
    }
    return out;
}
// Dessine les droits ; rend la hauteur prise.
float paintRights(gfx::IRenderer& r, const ui::Theme& th, float x, float y, float w, const std::vector<std::string>& rights, bool plus) {
    const auto& c = th.color;
    const float lh = 22.f;
    float h = 0.f;
    for (const auto& l : rightsLines(plus ? adm::allRights() : rights)) {
        if (!l.allowed.empty()) {
            drawTick(r, x, y + h + 3.f, c.ok);
            r.drawText({x + 22.f, y + h}, fit(r, l.group + " : " + l.allowed, kF13, w - 22.f), kF13, c.text);
            h += lh;
        }
        if (!l.missing.empty()) {
            drawCross(r, x + 1.f, y + h + 4.f, c.textDisabled);
            r.drawText({x + 22.f, y + h}, fit(r, l.group + " : pas " + l.missing, kF13, w - 22.f), kF13, c.textMuted);
            h += lh;
        }
    }
    drawTick(r, x, y + h + 3.f, c.ok);
    r.drawText({x + 22.f, y + h}, "Voir la liste des admins", kF13, c.text);
    h += lh + 6.f;
    r.fillRect({x, y + h, w, 1.f}, c.gridLine);
    h += 8.f;
    const FontId fp = gfx::semibold(kF11);
    const std::string tag = plus ? "\xC3\xA0 toi" : "r\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+";
    const float tw = pillWidth(r, tag, fp);
    for (const auto& s : plusOnly()) {
        if (plus) drawTick(r, x, y + h + 3.f, c.accent);
        else ui::drawIcon(r, ui::Icon::Lock, {x, y + h + 1.f, 14.f, 14.f}, c.textDisabled);
        r.drawText({x + 22.f, y + h}, fit(r, s, kF13, w - 30.f - tw), kF13, plus ? c.text : c.textMuted);
        (void)pill(r, x + w - tw, y + h - 1.f, tag, fp, c.accent, c.accent.withAlpha(22), 20.f);
        h += lh;
    }
    return h;
}

// =================================================================== retirer ====
class RetireBody final : public ui::Widget {
public:
    RetireBody(Dialog& owner, ShellHost host, std::string key, std::string name, std::function<void()> done)
        : ui::Widget("dialog.retirer-admin.corps"), owner_(owner), host_(std::move(host)), key_(std::move(key)), name_(std::move(name)), done_(std::move(done)) {
        licences_ = &adopt(*this, std::make_unique<ui::Checkbox>("R\xC3\xA9voquer aussi ses licences", "admins.retirer.licences"));
        cancel_ = &button(*this, "Annuler", "admins.retirer.annuler");
        ok_ = &button(*this, "Retirer " + name_, "admins.retirer.ok", ui::Button::Style::Danger);
        links_ += cancel_->clicked->connect([this] { owner_.close(); });
        links_ += ok_->clicked->connect([this] { go(); });
    }
    ~RetireBody() override { *alive_ = false; }

protected:
    void onLayout() override {
        frame_.place(*this, 600.f, 420.f);
        const auto& p = frame_.panel;
        licences_->setBounds({p.x + 24.f, p.bottom() - 132.f, p.w - 48.f, 28.f});
        const float by = p.bottom() - 58.f;
        const float ow = buttonWidth(*ok_, 140.f), cw = buttonWidth(*cancel_, 100.f);
        ok_->setBounds({p.right() - 24.f - ow, by, ow, 36.f});
        cancel_->setBounds({ok_->bounds().x - 8.f - cw, by, cw, 36.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& p = frame_.panel;
        const auto& c = ctx.theme.color;
        const float y = frame_.paint(ctx.r, ctx.theme, "Retirer " + name_, "Son certificat d'admin est r\xC3\xA9voqu\xC3\xA9 : ses nouvelles signatures sont refus\xC3\xA9" "es partout.",
                                     ui::Icon::User);
        const std::string text =
            "La cl\xC3\xA9 de l'\xC3\xA9" "diteur signe une nouvelle liste des admins o\xC3\xB9 " + name_ +
            " est retir\xC3\xA9, et la publie. Ce qu'il a d\xC3\xA9j\xC3\xA0 sign\xC3\xA9 reste valide (ses licences chez les clients), sauf si tu "
            "coches la case ci-dessous. Le registre et la liste de r\xC3\xA9vocation qu'il aurait sign\xC3\xA9s sont re-sign\xC3\xA9s par ta cl\xC3\xA9.\n"
            "Un admin n'a qu'un PC : pour le reprendre sur un autre PC, retire-le puis ajoute-le \xC3\xA0 nouveau (une nouvelle cl\xC3\xA9).";
        (void)drawWrapped(ctx.r, p.x + 24.f, y + 18.f, p.w - 48.f, text, kF14, c.text, 20.f);
        const Rect lb = licences_->bounds();
        ctx.r.drawText({lb.x + 28.f, lb.bottom() + 2.f}, fit(ctx.r, "Par d\xC3\xA9" "faut non : ses clients gardent leurs licences.", kF12, p.w - 76.f), kF12, c.textMuted);
        ctx.r.fillRect({p.x + 1.f, p.bottom() - 74.f, p.w - 2.f, 1.f}, c.border);
        ctx.r.drawText({p.x + 24.f, p.bottom() - 48.f}, "\xC3\x89" "crit dans le journal des admins (Critique).", kF13, c.textMuted);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (frame_.hitClose(ev)) {
            owner_.close();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    void go() {
        if (sent_) return;
        if (host_.busy && host_.busy()) {
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
            return;
        }
        sent_ = true;
        const Workspace* w = &host_.workspace();
        const std::string key = key_;
        const bool revoke = licences_->isChecked();
        auto alive = alive_;
        auto done = done_;
        Dialog* owner = &owner_;
        if (!host_.runSlow("Retirer " + name_, [w, key, revoke] { return w->retireAdmin(key, revoke); },
                           [alive, done, owner](const OpResult& r) {
                               if (*alive) owner->close();
                               if (r.ok && done) done();
                           })) {
            sent_ = false;
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        }
    }
    Dialog&               owner_;
    ShellHost             host_;
    std::string           key_, name_;
    std::function<void()> done_;
    bool                  sent_{false};
    ui::Checkbox*         licences_{nullptr};
    ui::Button*           cancel_{nullptr};
    ui::Button*           ok_{nullptr};
    Frame                 frame_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    core::ConnectionScope links_;
};

class RetireDialog final : public Dialog {
public:
    RetireDialog(ShellHost host, std::string key, std::string name, std::function<void()> done)
        : Dialog("dialog.retirer-admin"), host_(std::move(host)), key_(std::move(key)), name_(std::move(name)), done_(std::move(done)) {}
    [[nodiscard]] std::string title() const override { return "Retirer un admin"; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<RetireBody>(*this, host_, key_, name_, done_));
        return core::ok();
    }

private:
    ShellHost             host_;
    std::string           key_, name_;
    std::function<void()> done_;
};

// =========================================================== ajouter un admin ====
// 1.51.0 : UNE SEULE ETAPE (la demande : << supprimer les parties 2 et 3 >>). Plus de demande d'admin a attendre, ni
// d'empreinte a lire, ni de 6 chiffres : l'admin+ dit qui (son nom, son courriel), avec quels droits (son profil) et ou
// envoyer XPG Admin (le PC de son Studio, trouve par une recherche - PcPicker ; ou un lien de 48 h). L'envoi cree le code
// d'invitation (48 h, une seule fois) et le met dans le message ; le futur admin le tape dans l'installateur : il est
// admin des que son code est accepte (l'Admin+ signe sa cle d'office). Le dialogue dit alors << Invitation envoyee >>,
// montre le code une fois (Copier) et se ferme sur << Fermer >>.
class AddAdminBody final : public ui::Widget {
public:
    AddAdminBody(Dialog& owner, ShellHost host, std::function<void()> done)
        : ui::Widget("dialog.ajouter-admin.corps"), owner_(owner), host_(std::move(host)), done_(std::move(done)) {
        name_ = &adopt(*this, std::make_unique<ui::InputText>("admins.ajouter.nom"));
        name_->setPlaceholder("Nina Perret");
        name_->setMaxLength(60);
        email_ = &adopt(*this, std::make_unique<ui::InputText>("admins.ajouter.courriel"));
        email_->setPlaceholder("nina@exemple.fr (facultatif)");
        email_->setMaxLength(120);
        group_ = std::make_shared<ui::RadioGroup>();
        studio_ = &adopt(*this, std::make_unique<ui::RadioButton>("Dans les notifications de son Studio", group_, 0, "admins.ajouter.envoi.studio"));
        link_ = &adopt(*this, std::make_unique<ui::RadioButton>("Par un lien, dans un courriel", group_, 1, "admins.ajouter.envoi.lien"));
        group_->setValue(0);
        profile_ = &adopt(*this, std::make_unique<ui::DropDown>("admins.ajouter.profil"));
        std::vector<ui::DropDown::Item> items;
        int pick = 0;
        const auto profiles = host_.workspace().profiles();
        for (std::size_t i = 0; i < profiles.size(); ++i) {
            ui::DropDown::Item it;
            it.label = profiles[i].name;
            it.value = profiles[i].name;
            it.note = std::to_string(profiles[i].rights.size()) + " sur 13";
            items.push_back(std::move(it));
            if (pick == 0 && !profiles[i].builtin) pick = static_cast<int>(i);
        }
        profile_->setItems(std::move(items));
        profile_->setSelectedIndex(pick);
        // Le PC de son Studio : une recherche (les PC que connait ce poste, les plus recemment actifs d'abord).
        pc_ = &adopt(*this, std::make_unique<PcPicker>("admins.ajouter.pc"));
        pc_->setPcs(host_.workspace().pcsForMessages(), host_.workspace().now());
        links_ += pc_->selectionChanged->connect([this] { update(); });
        links_ += pc_->picked->connect([this] {
            if (const MessagePc* p = pc_->selected())
                host_.flash("PC choisi : " + PcPicker::labelOf(*p) + (p->customer.empty() ? std::string() : " (" + p->customer + ")") +
                                " \xE2\x80\x94 Envoyer l'invitation.",
                            Tone::Info);
        });
        send_ = &button(*this, "Envoyer l'invitation", "admins.ajouter.envoyer", ui::Button::Style::Primary, ui::Icon::Mail);
        makeLink_ = &button(*this, "Cr\xC3\xA9" "er le lien", "admins.ajouter.lien.creer", ui::Button::Style::Primary, ui::Icon::Link);
        copyLink_ = &button(*this, "Copier le lien et le code", "admins.ajouter.lien.copier", ui::Button::Style::Default, ui::Icon::Copy);
        chooseFile_ = &button(*this, "Choisir l'installateur\xE2\x80\xA6", "admins.ajouter.fichier", ui::Button::Style::Default, ui::Icon::Open);
        chooseFile_->setTooltip("Aucune version publi\xC3\xA9" "e n'a l'installateur de XPG Admin : choisis le fichier (XPG-Admin-Setup-....exe), il est d\xC3\xA9pos\xC3\xA9 pour l'occasion.");
        copyCode_ = &button(*this, "Copier", "admins.ajouter.code.copier", ui::Button::Style::Default, ui::Icon::Copy);
        cancel_ = &button(*this, "Annuler", "admins.ajouter.annuler");
        close_ = &button(*this, "Fermer", "admins.ajouter.fermer", ui::Button::Style::Primary);
        links_ += send_->clicked->connect([this] { sendInstaller(false); });
        links_ += makeLink_->clicked->connect([this] { sendInstaller(true); });
        links_ += copyLink_->clicked->connect([this] {
            if (url_.empty()) return;
            ui::setClipboardText(code_.empty() ? url_ : url_ + "\n" + codeSentence());
            host_.flash("Lien et code d'invitation copi\xC3\xA9s : colle-les dans ton courriel au futur admin (48 h, une seule fois).", Tone::Info);
        });
        links_ += copyCode_->clicked->connect([this] {
            if (code_.empty()) return;
            ui::setClipboardText(code_);
            host_.flash("Code d'invitation copi\xC3\xA9 : valable 48 h, une seule fois.", Tone::Info);
        });
        links_ += chooseFile_->clicked->connect([this] { chooseInstaller(lastWasLink_); });
        links_ += group_->valueChanged->connect([this](int) { update(); });
        links_ += name_->textChanged->connect([this](const std::string&) { update(); });
        links_ += cancel_->clicked->connect([this] { owner_.close(); });
        links_ += close_->clicked->connect([this] { owner_.close(); });
        update();
    }
    ~AddAdminBody() override { *alive_ = false; }

    // Les essais.
    [[nodiscard]] PcPicker& picker() noexcept { return *pc_; }
    [[nodiscard]] bool sent() const noexcept { return sentAt_ != 0; }
    [[nodiscard]] const std::string& code() const noexcept { return code_; }

protected:
    void onLayout() override {
        frame_.place(*this, 1040.f, 660.f);
        const auto& p = frame_.panel;
        const float top = p.y + 77.f + 18.f, bottom = p.bottom() - 76.f;
        const float gap = 20.f, x0 = p.x + 24.f;
        const float leftW = std::clamp((p.w - 48.f - gap) * 0.42f, 300.f, 400.f);
        left_ = {x0, top, leftW, bottom - top - 14.f};
        right_ = {x0 + leftW + gap, top, p.right() - 24.f - (x0 + leftW + gap), bottom - top - 14.f};
        // A gauche : qui, ses droits, comment lui envoyer.
        name_->setBounds({left_.x, left_.y + 22.f, left_.w, 34.f});
        email_->setBounds({left_.x, left_.y + 90.f, left_.w, 34.f});
        profile_->setBounds({left_.x, left_.y + 158.f, left_.w, 32.f});
        studio_->setBounds({left_.x + 14.f, left_.y + 260.f, left_.w - 28.f, 26.f});
        link_->setBounds({left_.x + 14.f, left_.y + 316.f, left_.w - 28.f, 26.f});
        // A droite : le PC (une recherche), ou le lien ; envoye : l'invitation et son code.
        const bool byLink = group_->value() == 1, done = sentAt_ != 0;
        show(pc_, !byLink && !done);
        pc_->setBounds({right_.x, right_.y + 26.f, right_.w, std::max(120.f, right_.h - 26.f - (needFile_ ? 46.f : 0.f))});
        show(chooseFile_, needFile_ && !done);
        chooseFile_->setBounds({right_.x, right_.bottom() - 34.f, buttonWidth(*chooseFile_, 160.f), 32.f});
        show(copyCode_, done && !code_.empty());
        codeBox_ = {right_.x, right_.y + 64.f, right_.w, 76.f};
        const float cw = buttonWidth(*copyCode_, 90.f);
        copyCode_->setBounds({codeBox_.right() - 14.f - cw, codeBox_.y + 22.f, cw, 32.f});
        show(copyLink_, done && !url_.empty());
        copyLink_->setBounds({right_.x, codeBox_.bottom() + 70.f, buttonWidth(*copyLink_, 170.f), 32.f});
        // Le pied : Annuler + Envoyer l'invitation (ou Creer le lien) ; envoye : Fermer.
        const float by = p.bottom() - 58.f;
        show(send_, !byLink && !done);
        show(makeLink_, byLink && !done);
        show(cancel_, !done);
        show(close_, done);
        ui::Button* primary = done ? close_ : byLink ? makeLink_ : send_;
        const float pw = buttonWidth(*primary, 170.f);
        primary->setBounds({p.right() - 24.f - pw, by, pw, 38.f});
        const float aw = buttonWidth(*cancel_, 100.f);
        cancel_->setBounds({primary->bounds().x - 10.f - aw, by, aw, 38.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const auto& p = frame_.panel;
        (void)frame_.paint(r, th, "Ajouter un admin",
                           "Son code d'invitation fait de lui un admin d\xC3\xA8s l'installation de XPG Admin : rien d'autre \xC3\xA0 faire.", ui::Icon::User);
        const bool byLink = group_->value() == 1, done = sentAt_ != 0;
        // ---- a gauche ----
        r.drawText({left_.x, name_->bounds().y - 20.f}, "Pr\xC3\xA9nom et nom", gfx::semibold(kF13), c.text);
        r.drawText({left_.x, email_->bounds().y - 20.f}, "Courriel", gfx::semibold(kF13), c.text);
        r.drawText({left_.x, profile_->bounds().y - 20.f}, "Son profil (ses droits)", gfx::semibold(kF13), c.text);
        r.drawText({left_.x, profile_->bounds().bottom() + 6.f},
                   fit(r, "Une copie du profil : tu l'ajustes ensuite pour lui seul (Droits).", kF12, left_.w), kF12, c.textMuted);
        r.drawText({left_.x, studio_->bounds().y - 30.f}, "Comment lui envoyer XPG Admin", gfx::semibold(kF13), c.text);
        for (ui::RadioButton* rb : {studio_, link_}) {
            const Rect box{left_.x, rb->bounds().y - 6.f, left_.w, 48.f};
            ui::shapes::strokeRoundedRect(r, box, rb->selected() ? c.accent : c.border, 10.f, rb->selected() ? 1.5f : 1.f);
            const std::string sub = rb == studio_ ? std::string("le PC de son Studio, choisi \xC3\xA0 droite") : std::string("valable 48 h, une seule fois");
            r.drawText({rb->bounds().x + 24.f, rb->bounds().bottom() - 2.f}, fit(r, sub, kF12, box.w - 60.f), kF12, c.textMuted);
        }
        {
            const Rect note{left_.x, link_->bounds().bottom() + 34.f, left_.w, 0.f};
            const float h = std::max(0.f, left_.bottom() - note.y);
            if (h >= 56.f) {
                const Rect box{note.x, note.y, note.w, std::min(h, 76.f)};
                r.fillRoundedRect(box, c.accent.withAlpha(16), 10.f);
                (void)drawWrapped(r, box.x + 12.f, box.y + 10.f, box.w - 24.f,
                                  "Le message porte son code d'invitation (48 h, une seule fois). Il le tape dans l'installateur : il est admin d\xC3\xA8s qu'il est "
                                  "accept\xC3\xA9, avec ce profil.",
                                  kF12, c.accent, 17.f, 3);
            }
        }
        // ---- a droite ----
        if (done) {
            paintSent(ctx);
        } else if (!byLink) {
            r.drawText({right_.x, right_.y}, "Le PC de son Studio", gfx::semibold(kF13), c.text);
            const std::string hint = "Haut / Bas pour choisir, Echap efface la recherche";
            const float hw = r.measure(hint, kF12).width;
            if (hw < right_.w - 160.f) r.drawText({right_.right() - hw, right_.y + 1.f}, hint, kF12, c.textMuted);
        } else {
            r.drawText({right_.x, right_.y}, "Par un lien", gfx::semibold(kF13), c.text);
            (void)drawWrapped(r, right_.x, right_.y + 30.f, right_.w,
                              "\xC2\xAB Cr\xC3\xA9" "er le lien \xC2\xBB pr\xC3\xA9pare un lien vers l'installateur de XPG Admin (valable 48 h, une seule fois) et son "
                              "code d'invitation. Tu colles les deux dans ton courriel : il ouvre le lien, installe XPG Admin et y tape le code.",
                              kF14, c.text, 21.f, 6);
        }
        if (needFile_ && !done)
            r.drawText({chooseFile_->bounds().right() + 12.f, chooseFile_->bounds().y + 8.f},
                       fit(r, "aucune version publi\xC3\xA9" "e n'a l'installateur de XPG Admin", kF12, std::max(10.f, right_.right() - chooseFile_->bounds().right() - 12.f)),
                       kF12, c.warning);
        // ---- le pied ----
        r.fillRect({p.x + 1.f, p.bottom() - 76.f, p.w - 2.f, 1.f}, c.border);
        std::string status;
        Color ink = c.textMuted;
        if (sending_) status = "Envoi en cours\xE2\x80\xA6";
        else if (!sendProblem_.empty()) {
            status = sendProblem_;
            ink = c.error;
        } else if (done) {
            status = "\xC3\x89" "crit dans le journal des admins (invitation, puis admin ajout\xC3\xA9 par son code).";
        } else if (!byLink) {
            if (const MessagePc* s = pc_->selected())
                status = "Pour " + PcPicker::labelOf(*s) + (s->customer.empty() ? std::string() : " (" + s->customer + ")") + " \xC2\xB7 l'installateur arrive sign\xC3\xA9 par l'\xC3\xA9" "diteur.";
            else status = pc_->pcs().empty() ? std::string("Aucun PC connu sur ce poste : choisis \xC2\xAB Par un lien \xC2\xBB.") : std::string("Choisis le PC dans la liste.");
        } else {
            status = "Un lien \xC3\xA0 coller dans ton courriel, avec son code d'invitation.";
        }
        const float sx = p.x + 24.f;
        const float room = (cancel_->visible() ? cancel_->bounds().x : close_->bounds().x) - 16.f - sx;
        (void)drawWrapped(r, sx, p.bottom() - 52.f, std::max(80.f, room), status, kF13, ink, 18.f, 2);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (frame_.hitClose(ev)) {
            owner_.close();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    // L'invitation partie : le code (montre ici une seule fois), ou va le message (ou le lien), ce qui se passe ensuite.
    void paintSent(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        drawTick(r, right_.x, right_.y + 4.f, c.ok);
        r.drawText({right_.x + 24.f, right_.y}, url_.empty() ? std::string("Invitation envoy\xC3\xA9" "e") : std::string("Invitation pr\xC3\xAAte"),
                   gfx::semibold(FontId{18}), c.ok);
        r.drawText({right_.x, right_.y + 30.f}, fit(r, sentWhat_, kF13, right_.w), kF13, c.text);
        if (!code_.empty()) {
            r.fillRoundedRect(codeBox_, c.accent.withAlpha(16), 10.f);
            ui::shapes::strokeRoundedRect(r, codeBox_, c.accent, 10.f, 1.5f);
            r.drawText({codeBox_.x + 14.f, codeBox_.y + 10.f}, code_, gfx::semibold(FontId{26}), c.text);
            r.drawText({codeBox_.x + 14.f, codeBox_.y + 50.f}, fit(r, "son code d'invitation \xC2\xB7 48 h, une seule fois \xC2\xB7 montr\xC3\xA9 ici une seule fois", kF12,
                                                                  copyCode_->bounds().x - codeBox_.x - 24.f),
                       kF12, c.textMuted);
        }
        float y = codeBox_.bottom() + 16.f;
        if (!url_.empty()) {
            r.drawText({right_.x, y}, fit(r, "Lien (48 h, une seule fois) : " + url_, kF12, right_.w), kF12, c.text);
            y = copyLink_->bounds().bottom() + 16.f;
        } else {
            y += 8.f;
        }
        (void)drawWrapped(r, right_.x, y, right_.w,
                          "Il installe XPG Admin et tape ce code dans l'installateur (la page \xC2\xAB Code d'invitation \xC2\xBB). D\xC3\xA8s que le service l'accepte, "
                          "ta cl\xC3\xA9 de l'\xC3\xA9" "diteur signe la sienne d'office, sans rien te demander : il appara\xC3\xAEt dans Administrateurs, avec le profil " +
                              cite(sentProfile_) + ".",
                          kF13, c.textMuted, 19.f, 5);
    }
    static std::string cite(const std::string& s) { return "\xC2\xAB " + s + " \xC2\xBB"; }
    // la phrase du code, a coller avec le lien (la meme que dans le message du Studio)
    [[nodiscard]] std::string codeSentence() const { return "Ton code d'invitation : " + code_ + " (\xC3\xA0 saisir dans l'installateur, valable 48 h)"; }
    [[nodiscard]] std::string selectedProfile() const {
        const auto* it = profile_->selectedItem();
        return it ? it->value : std::string(adm::kAllRightsProfile);
    }

    void update() {
        const bool busy = host_.busy && host_.busy();
        const bool named = !trimmed(name_->text()).empty();
        send_->setEnabled(!sending_ && !busy && named && pc_->selected() != nullptr);
        send_->setTooltip(!named ? std::string("Son nom d'abord : c'est son nom dans la liste des admins.")
                          : !pc_->selected() ? std::string("Choisis le PC de son Studio (la recherche), ou \xC2\xAB Par un lien \xC2\xBB.")
                                             : std::string("Cr\xC3\xA9" "e son code d'invitation et envoie XPG Admin dans les notifications de ce Studio."));
        makeLink_->setEnabled(!sending_ && !busy && named);
        makeLink_->setTooltip(named ? std::string("Cr\xC3\xA9" "e son code d'invitation et un lien de 48 h vers l'installateur de XPG Admin.")
                                    : std::string("Son nom d'abord : c'est son nom dans la liste des admins."));
        chooseFile_->setEnabled(!sending_ && !busy);
        name_->setReadOnly(sentAt_ != 0);
        email_->setReadOnly(sentAt_ != 0);
        invalidateLayout();
        invalidate();
    }

    // Envoyer XPG Admin : dans les notifications du Studio du PC choisi, ou un lien (48 h, une fois). Le code d'invitation
    // est cree d'abord (une fois par dialogue) et part dans le message. Aucune version publiee n'a l'installateur : le
    // fichier est demande, puis l'envoi repart.
    void sendInstaller(bool byLink) {
        if (sending_ || sentAt_) return;
        lastWasLink_ = byLink;
        const MessagePc* chosen = pc_->selected();
        if (!byLink && !chosen) {
            sendProblem_ = "Choisis le PC de son Studio (la recherche), ou \xC2\xAB Par un lien \xC2\xBB.";
            update();
            return;
        }
        const std::string name = trimmed(name_->text());
        if (name.empty()) {
            sendProblem_ = "Son nom d'abord : c'est son nom dans la liste des admins.";
            update();
            return;
        }
        const Workspace* w = &host_.workspace();
        const std::string device = byLink ? std::string() : pc_->selectedValue(), file = fallback_;
        const std::string email = trimmed(email_->text()), profile = selectedProfile();
        auto url = std::make_shared<std::string>();
        auto code = std::make_shared<std::string>(code_);
        auto inv = std::make_shared<AdminInvitation>();
        auto alive = alive_;
        sending_ = true;
        sendProblem_.clear();
        const auto work = [w, byLink, device, name, email, profile, file, url, code, inv] {
            if (code->empty()) {
                OpResult ir = w->inviteAdmin(name, email, profile, *code, inv.get());
                if (!ir.ok) return ir;
            }
            return byLink ? w->adminInstallerLink(file, *url, {}, *code) : w->sendAdminInstaller(device, name, file, {}, *code);
        };
        const auto done = [this, alive, url, byLink, code, profile](const OpResult& r) {
            if (!*alive) return;
            sending_ = false;
            if (code_.empty() && !code->empty()) code_ = *code;   // montre une fois, dans ce dialogue seulement
            if (r.ok) {
                sentAt_ = host_.workspace().now();
                sentWhat_ = r.details.empty() ? r.message : r.details.front();
                if (sentWhat_.rfind("Fichier : ", 0) == 0) sentWhat_ = sentWhat_.substr(10);
                sentProfile_ = profile;
                if (byLink) url_ = *url;
                needFile_ = false;
                if (done_) done_();
            } else {
                sendProblem_ = r.message;
                if (r.message.find("choisis le fichier") != std::string::npos) {
                    needFile_ = true;
                    update();
                    chooseInstaller(byLink);
                    return;
                }
            }
            update();
        };
        if (!host_.runSlow(byLink ? "Lien vers XPG Admin et son code d'invitation" : "Envoyer XPG Admin et son code d'invitation", work, done)) {
            sending_ = false;
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        }
        update();
    }
    void chooseInstaller(bool byLink) {
        ui::FilePick p;
        p.title = "L'installateur de XPG Admin (XPG-Admin-Setup-....exe)";
        p.filters = {{"Installateur Windows (.exe)", "exe"}};
        auto alive = alive_;
        host_.pickFile(p, [this, alive, byLink](std::string path) {
            if (!*alive || path.empty()) return;
            fallback_ = path;
            sendInstaller(byLink);
        });
    }

    Dialog&                         owner_;
    ShellHost                       host_;
    std::function<void()>           done_;
    ui::InputText*                  name_{nullptr};
    ui::InputText*                  email_{nullptr};
    std::shared_ptr<ui::RadioGroup> group_;
    ui::RadioButton*                studio_{nullptr};
    ui::RadioButton*                link_{nullptr};
    ui::DropDown*                   profile_{nullptr};
    PcPicker*                       pc_{nullptr};
    ui::Button*                     send_{nullptr};
    ui::Button*                     makeLink_{nullptr};
    ui::Button*                     copyLink_{nullptr};
    ui::Button*                     chooseFile_{nullptr};
    ui::Button*                     copyCode_{nullptr};
    ui::Button*                     cancel_{nullptr};
    ui::Button*                     close_{nullptr};
    std::string                     fallback_, url_, sentWhat_, sendProblem_, code_, sentProfile_;
    Seconds                         sentAt_{0};
    bool                            sending_{false}, needFile_{false}, lastWasLink_{false};
    Frame                           frame_;
    Rect                            left_{}, right_{}, codeBox_{};
    std::shared_ptr<bool>           alive_ = std::make_shared<bool>(true);
    core::ConnectionScope           links_;
};

class AddAdminDialog final : public Dialog {
public:
    AddAdminDialog(ShellHost host, std::function<void()> done) : Dialog("dialog.ajouter-admin"), host_(std::move(host)), done_(std::move(done)) {}
    [[nodiscard]] std::string title() const override { return "Ajouter un admin"; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<AddAdminBody>(*this, host_, done_));
        return core::ok();
    }

private:
    ShellHost             host_;
    std::function<void()> done_;
};

// ===================================================================== la page ====
//  1.47.0 (maquette 1.47.0, planches 01 a 05) : L'ADMIN+ voit LE TABLEAU DES ADMINS - la periode commune et la
//  periode d'avant, les onglets (Vue d'ensemble, Qui fait quoi, Droits, Journal des admins) ; la vue d'ensemble et
//  qui fait quoi sont dans PageAdminsOverview.cpp et PageAdminsWho.cpp ; la fiche d'un admin (double-clic, Entree,
//  << Ouvrir sa fiche >>) remplace la vue sous son propre en-tete (PageAdminCard.cpp). UN ADMIN garde la liste en
//  lecture seule, et << Ma fiche >> : la sienne, la seule qu'il voit. Les chiffres : AdminsBoard.hpp.
namespace ab = adminsboard;
namespace js = license_admin::journalstats;
namespace ch = ui::charts;

const char* const kAnchorLabels[7] = {"Vue d'ensemble", "Activit\xC3\xA9", "Licences et clients", "Demandes", "Boutique", "S\xC3\xA9" "curit\xC3\xA9", "Droits"};

class AdminsPage final : public Page {
public:
    explicit AdminsPage(ShellHost host) : Page("admins", std::move(host)) {
        statskit::installSamplesFromEnvironment([this] { return ws().now(); });
        data_ = std::make_shared<ab::Data>();
        choice_.range = statskit::Range::Days30;
        choice_.compare = stats::Compare::Previous;
        choice_.step = stats::Step::Day;
        // L'admin+ : l'en-tete, les onglets.
        add_ = &button(*this, "Ajouter un admin\xE2\x80\xA6", "admins.ajouter", ui::Button::Style::Primary, ui::Icon::Plus);
        tabVue_ = &adopt(*this, std::make_unique<statskit::TabButton>("Vue d'ensemble", "admins.onglet.vue", ui::Icon::Chart));
        tabQui_ = &adopt(*this, std::make_unique<statskit::TabButton>("Qui fait quoi", "admins.onglet.qui", ui::Icon::User));
        rightsPage_ = &adopt(*this, std::make_unique<statskit::TabButton>("Droits", "admins.droits-page", ui::Icon::Lock));
        rightsPage_->setTooltip("Les profils de droits (Admin LV1, LV2\xE2\x80\xA6), copi\xC3\xA9s vers un admin puis ajust\xC3\xA9s pour lui seul.");
        tabJournal_ = &adopt(*this, std::make_unique<statskit::TabButton>("Journal des admins", "admins.onglet.journal", ui::Icon::History));
        reload_ = &button(*this, "Relever", "admins.relever", ui::Button::Style::Default, ui::Icon::Refresh);
        reload_->setTooltip("Relire tout de suite la liste des admins et les chiffres du service (l'Admin le fait seul en arrivant sur la page).");
        range_ = &adopt(*this, std::make_unique<statskit::SegGroup>(
                                   "admins.periode", std::vector<statskit::SegGroup::Item>{{statskit::rangeKey(statskit::Range::Days7), statskit::rangeLabel(statskit::Range::Days7)},
                                                                                          {statskit::rangeKey(statskit::Range::Days30), statskit::rangeLabel(statskit::Range::Days30)},
                                                                                          {statskit::rangeKey(statskit::Range::Days90), statskit::rangeLabel(statskit::Range::Days90)},
                                                                                          {statskit::rangeKey(statskit::Range::Months12), statskit::rangeLabel(statskit::Range::Months12)},
                                                                                          {statskit::rangeKey(statskit::Range::All), statskit::rangeLabel(statskit::Range::All)},
                                                                                          {statskit::rangeKey(statskit::Range::Dates), statskit::rangeLabel(statskit::Range::Dates),
                                                                                           ui::Icon::History}}));
        range_->onPick = [this](const std::string& k) { pickRange(k); };
        from_ = &adopt(*this, std::make_unique<ui::DatePicker>("admins.du"));
        to_ = &adopt(*this, std::make_unique<ui::DatePicker>("admins.au"));
        deploy_ = &button(*this, "D\xC3\xA9ployer le service", "admins.deployer", ui::Button::Style::Primary, ui::Icon::ArrowUp);
        deploy_->setTooltip("Lance le kit install\xC3\xA9 avec l'Admin : ton service part \xC3\xA0 jour sur ton compte Cloudflare (la sortie s'affiche en direct).");
        // La fiche : son en-tete.
        back_ = &adopt(*this, std::make_unique<ab::LinkButton>("\xE2\x80\xB9 Tous les admins", "admins.fiche.retour", false));
        cardJournal_ = &button(*this, "Son journal", "admins.fiche.journal", ui::Button::Style::Default, ui::Icon::History);
        cardRights_ = &button(*this, "Ses droits\xE2\x80\xA6", "admins.fiche.droits", ui::Button::Style::Default, ui::Icon::Lock);
        cardRetire_ = &button(*this, "Retirer\xE2\x80\xA6", "admins.fiche.retirer", ui::Button::Style::Danger, ui::Icon::User);
        // 1.47.0 (agent S) : la derogation << Gerer le service >> a cet admin (DerogationDialog.cpp)
        cardDerog_ = &button(*this, "D\xC3\xA9rogation\xE2\x80\xA6", "admins.fiche.derogation", ui::Button::Style::Default, ui::Icon::Lock);
        cardDerog_->setTooltip("G\xC3\xA9rer le service : \xC3\xA0 cet admin, pour un temps court (1 h \xC3\xA0 72 h), r\xC3\xA9vocable");
        for (int i = 0; i < 7; ++i) {
            anchors_[i] = &adopt(*this, std::make_unique<statskit::TabButton>(kAnchorLabels[i], "admins.fiche.ancre." + std::to_string(i), ui::Icon::None));
            links_ += anchors_[i]->clicked->connect([this, i] {
                if (card_) ab::cardAnchor(*card_, i);
                for (int k = 0; k < 7; ++k) anchors_[k]->setSelected(k == i);
            });
        }
        // Un admin : sa fiche.
        myCard_ = &button(*this, "Ma fiche", "admins.ma-fiche", ui::Button::Style::Default, ui::Icon::User);
        myCard_->setTooltip("Tes chiffres : tes actions, tes jours, tes droits utilis\xC3\xA9s ou non, ce que tu produis. Tu ne vois que ta fiche.");
        // Pas encore admin : les deux chemins.
        createKey_ = &button(*this, "Cr\xC3\xA9" "er la cl\xC3\xA9 de l'\xC3\xA9" "diteur\xE2\x80\xA6", "admins.creer-cle", ui::Button::Style::Primary, ui::Icon::Lock);
        restore_ = &button(*this, "Restaurer la cl\xC3\xA9 de l'\xC3\xA9" "diteur\xE2\x80\xA6", "admins.restaurer", ui::Button::Style::Default, ui::Icon::Open);   // 1.49.0
        // 1.51.0 (docs/ADMINS.md, par. 4) : XPG Admin - le code d'invitation (pose par l'installateur ; sinon tape ici) : il
        // fait de ce PC un admin, sans demande.
        reqCode_ = &adopt(*this, std::make_unique<ui::InputText>("admins.invitation.code"));
        reqCode_->setPlaceholder("INV-XXXX-XXXX");
        reqCode_->setMaxLength(20);
        send_ = &button(*this, "Devenir admin", "admins.invitation.valider", ui::Button::Style::Primary, ui::Icon::User);
        service_ = &button(*this, "R\xC3\xA9gler le service\xE2\x80\xA6", "admins.regler-service", ui::Button::Style::Default, ui::Icon::Globe);
        // Ajout en cours.
        check_ = &button(*this, "V\xC3\xA9rifier maintenant", "admins.attente.verifier", ui::Button::Style::Default, ui::Icon::Refresh);

        table_ = &adopt(*this, std::make_unique<ui::TableView>("admins.table"));
        table_->setSelectionMode(ui::SelectionMode::Single);
        table_->setVisibility(ui::Visibility::Collapsed);   // le tableau des scripts : la page dessine le sien

        // Les vues de l'admin+ (la vue d'ensemble, qui fait quoi).
        actions_ = makeActions();
        overview_ = &adopt(*this, ab::makeOverview(data_, actions_, host_));
        who_ = &adopt(*this, ab::makeWho(data_, actions_, host_));

        links_ += add_->clicked->connect([this] { openAdd(); });
        links_ += tabVue_->clicked->connect([this] { showTab(0); });
        links_ += tabQui_->clicked->connect([this] { showTab(1); });
        links_ += rightsPage_->clicked->connect([this] { host_.navigate("droits"); });
        links_ += tabJournal_->clicked->connect([this] { host_.navigate("journal"); });
        links_ += reload_->clicked->connect([this] {
            fetch(true);
            serviceSig_.clear();
            data_->cards.clear();
            loadService();
        });
        links_ += from_->changed->connect([this](ui::DatePicker::Seconds) { datesChanged(); });
        links_ += to_->changed->connect([this](ui::DatePicker::Seconds) { datesChanged(); });
        links_ += deploy_->clicked->connect([this] {
            if (actions_.deploy) actions_.deploy();
        });
        links_ += back_->clicked->connect([this] { closeCard(); });
        links_ += cardJournal_->clicked->connect([this] {
            if (actions_.journalOf && !cardKey_.empty()) actions_.journalOf(cardKey_);
        });
        links_ += cardRights_->clicked->connect([this] {
            if (actions_.rightsOf && !cardKey_.empty()) actions_.rightsOf(cardKey_);
        });
        links_ += cardRetire_->clicked->connect([this] {
            if (!cardKey_.empty()) askRetire(cardKey_);
        });
        links_ += cardDerog_->clicked->connect([this] {   // 1.47.0 (agent S)
            if (!cardKey_.empty() && host_.openDialog) host_.openDialog(makeDerogationDialog(host_, cardKey_));
        });
        links_ += myCard_->clicked->connect([this] {
            if (!ws().adminKey().keyHex.empty()) openCard(ws().adminKey().keyHex, true);
        });
        links_ += createKey_->clicked->connect([this] { host_.navigate("securite"); });
        links_ += restore_->clicked->connect([this] { host_.navigate("securite"); });
        links_ += service_->clicked->connect([this] { host_.navigate("service"); });
        links_ += send_->clicked->connect([this] { join(); });
        links_ += reqCode_->editingDone->connect([this](const std::string&) { join(); });
        links_ += check_->clicked->connect([this] { checkJoin(); });
        links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& sel) {
            if (syncing_) return;
            if (!sel.empty() && sel.front() < rows_.size()) selectedKey_ = rows_[sel.front()].key;
            if (overview_) ab::overviewSelect(*overview_, selectedKey_);
            showButtons();
            invalidateLayout();   // la ligne depliee change
            invalidate();
        });
        syncBar();
        showTab(0);
        refresh();
    }
    ~AdminsPage() override { *alive_ = false; }

    [[nodiscard]] std::string title() const override { return "Administrateurs"; }

    void refresh() override {
        const std::string f = takePageFocus("admins");
        if (f.rfind("admin:", 0) == 0) selectedKey_ = f.substr(6);
        if (f.rfind("code-refuse:", 0) == 0) {   // 1.51.0 : le code de l'installateur refuse au premier lancement (Shell::autoJoin)
            invitationProblem_ = sentence(f.substr(12));
            lastState_ = "invitation";
        }
        const Role role = ws().role();
        if (role != shownRole_) {
            shownRole_ = role;
            fetched_ = Fetched{};
            lastFetch_ = -1;
            closeCard();
        }
        rebuild();
        if (f.rfind("fiche:", 0) == 0 && role == Role::Plus) openCard(f.substr(6), false);
        // Arriver sur la page : relire ce que le service sait (sans bruit ; au plus une fois par minute).
        if ((role == Role::Plus || role == Role::Admin) && ws().service().ready() && !fetching_ && (lastFetch_ < 0 || steadySeconds() - lastFetch_ > 60.0))
            fetch(false);
        loadService();
    }

    // Les essais.
    [[nodiscard]] std::size_t rowCount() const noexcept { return rows_.size(); }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float pad = b.w < 900.f ? 20.f : 28.f;
        x0_ = b.x + pad;
        w_ = std::max(300.f, b.w - 2.f * pad);
        const Role role = shownRole_;
        for (ui::Widget* wd : {static_cast<ui::Widget*>(add_), static_cast<ui::Widget*>(rightsPage_), static_cast<ui::Widget*>(reload_),
                               static_cast<ui::Widget*>(tabVue_), static_cast<ui::Widget*>(tabQui_), static_cast<ui::Widget*>(tabJournal_),
                               static_cast<ui::Widget*>(range_), static_cast<ui::Widget*>(from_), static_cast<ui::Widget*>(to_),
                               static_cast<ui::Widget*>(deploy_), static_cast<ui::Widget*>(back_), static_cast<ui::Widget*>(cardJournal_),
                               static_cast<ui::Widget*>(cardRights_), static_cast<ui::Widget*>(cardRetire_), static_cast<ui::Widget*>(myCard_),
                               static_cast<ui::Widget*>(cardDerog_),
                               static_cast<ui::Widget*>(overview_), static_cast<ui::Widget*>(who_), static_cast<ui::Widget*>(createKey_),
                               static_cast<ui::Widget*>(restore_), static_cast<ui::Widget*>(reqCode_), static_cast<ui::Widget*>(send_),
                               static_cast<ui::Widget*>(service_), static_cast<ui::Widget*>(check_)})
            wd->setVisibility(ui::Visibility::Collapsed);
        for (auto* a : anchors_) a->setVisibility(ui::Visibility::Collapsed);
        if (card_) card_->setVisibility(ui::Visibility::Collapsed);
        if (role == Role::None) layoutNone(b);
        else if (role == Role::Pending) layoutPending(b);
        else if (card_ && !cardKey_.empty()) layoutCard(b);
        else if (role == Role::Plus) layoutBoard(b);
        else layoutList(b);
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const Rect b = bounds();
        r.fillRect(b, ctx.theme.color.windowBg);
        const Role role = shownRole_;
        if (role == Role::None) paintNone(ctx);
        else if (role == Role::Pending) paintPending(ctx);
        else if (card_ && !cardKey_.empty()) paintCardHead(ctx);
        else if (role == Role::Plus) paintBoard(ctx);
        else paintList(ctx);
        if (!bannerRect_.empty()) statskit::paintBanner(r, ctx.theme, bannerRect_, bannerText(), data_->serviceTooOld, deploy_->visible() ? deploy_->bounds().w + 28.f : 0.f);
        // un travail qui n'a pas pu partir (un autre tournait) : il part des que possible
        if (wantService_ && !serviceBusy_) loadService();
        if (wantCard_ && !cardKey_.empty()) {
            wantCard_ = false;
            loadCard(cardKey_);
        }
        // l'ancre de la fiche suit le defilement
        if (card_ && !cardKey_.empty()) {
            const int cur = ab::cardCurrentAnchor(*card_);
            for (int k = 0; k < 7; ++k)
                if (anchors_[k]->selected() != (k == cur)) anchors_[k]->setSelected(k == cur);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape && !cardKey_.empty()) {
            closeCard();
            return ui::EventResult::Consumed;
        }
        if (shownRole_ == Role::Plus || !cardKey_.empty()) return ui::EventResult::Ignored;
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && rowsRect_.contains(d->pos)) {
            const int i = rowAt(d->pos);
            if (i >= 0) {
                select(static_cast<std::size_t>(i));
                return ui::EventResult::Consumed;
            }
        }
        if (const auto* wh = std::get_if<ui::MouseWheel>(&ev); wh && rowsRect_.contains(wh->pos)) {
            const int before = scroll_;
            scroll_ = std::clamp(scroll_ + (wh->dy > 0 ? -1 : 1), 0, std::max(0, static_cast<int>(rows_.size()) - visibleRows_));
            if (scroll_ != before) {
                invalidateLayout();
                invalidate();
            }
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    // ------------------------------------------------------------------ les gestes des vues
    ab::Actions makeActions() {
        ab::Actions a;
        a.openCard = [this](const std::string& key) { openCard(key, false); };
        a.journalOf = [this](const std::string& key) {
            requestPageFocus("journal", "admin:" + key);
            host_.navigate("journal");
        };
        a.journalLines = [this](std::vector<std::uint64_t> lines, const std::string& key) {
            if (!lines.empty()) requestPageFocus("journal", "ligne:" + std::to_string(lines.back()));
            else if (!key.empty()) requestPageFocus("journal", "admin:" + key);
            host_.navigate("journal");
        };
        a.rightsOf = [this](const std::string& key) {
            requestPageFocus("droits", "admin:" + key);
            host_.navigate("droits");
        };
        a.retire = [this](const std::string& key) { askRetire(key); };
        a.select = [this](const std::string& key) { syncTable(key); };
        a.addAdmin = [this] { openAdd(); };
        a.criticality = [this] {
            auto alive = alive_;
            host_.openDialog(makeCriticalityDialog(host_, [this, alive] {
                if (*alive) rebuild();
            }));
        };
        a.deploy = [this] {
            if (host_.busy && host_.busy()) {
                host_.report(OpResult::fail("un travail est en cours : attends qu'il finisse, puis D\xC3\xA9ployer le service"));
                return;
            }
            if (deployer::running()) return;
            auto alive = alive_;
            if (host_.openDialog) host_.openDialog(makeDeployDialog(host_, [this, alive] {
                if (*alive) refresh();
            }));
        };
        a.versions = [this] { host_.navigate("versions"); };
        a.removeRights = [this](const std::string& key, std::vector<std::string> rights) {
            std::string names;
            for (const auto& r : rights) names += (names.empty() ? "" : ", ") + ab::rightShort(r);
            const ab::Person* p = data_->person(key);
            host_.flash("Droits des admins : d\xC3\xA9" "coche " + names + (p ? " pour " + p->name : std::string()) + ", puis Signer et publier.", Tone::Info);
            requestPageFocus("droits", "admin:" + key);
            host_.navigate("droits");
        };
        a.whyNotRetire = [this](const std::string&) -> std::string {
            if (!ws().service().ready()) return "Il faut le service des licences (Service en ligne) : la nouvelle liste y est publi\xC3\xA9" "e.";
            if (host_.busy && host_.busy()) return "Un travail est en cours : attends qu'il finisse.";
            return {};
        };
        a.exportRows = [this](std::vector<std::string> keys) {
            std::vector<ui::RowIndex> rows;
            for (const auto& k : keys)
                for (std::size_t i = 0; i < rows_.size(); ++i)
                    if (rows_[i].key == k) rows.push_back(static_cast<ui::RowIndex>(i));
            listkit::exportRows(host_, *table_, rows, "administrateurs.csv", "les admins");
        };
        a.busy = [this] { return host_.busy && host_.busy(); };
        return a;
    }

    // ------------------------------------------------------------------ la periode
    [[nodiscard]] std::string allFrom() const {
        const js::TimeZone tz = js::TimeZone::paris();
        if (!data_->lines.empty()) return js::dayText(js::localDay(data_->lines.front().at(), tz));
        return statskit::allFromOf(ws());
    }
    void pickRange(const std::string& k) {
        for (statskit::Range r : {statskit::Range::Days7, statskit::Range::Days30, statskit::Range::Days90, statskit::Range::Months12, statskit::Range::All, statskit::Range::Dates})
            if (k == statskit::rangeKey(r)) choice_.range = r;
        if (choice_.range == statskit::Range::Dates) {
            const stats::Period p = stats::lastDays(ws().now(), 30);
            if (choice_.from.empty()) choice_.from = p.from;
            if (choice_.to.empty()) choice_.to = p.to;
            from_->setDate(stats::dayStart(choice_.from));
            to_->setDate(stats::dayStart(choice_.to));
            to_->setRangeStart(stats::dayStart(choice_.from));
        }
        periodChanged();
    }
    void datesChanged() {
        if (from_->date() > 0) choice_.from = stats::dayOf(from_->date());
        if (to_->date() > 0) choice_.to = stats::dayOf(to_->date());
        to_->setRangeStart(from_->date());
        choice_.range = statskit::Range::Dates;
        periodChanged();
    }
    void periodChanged() {
        syncBar();
        data_->cards.clear();
        rebuild();
        loadService();
    }
    void syncBar() { range_->setCurrent(statskit::rangeKey(choice_.range)); }
    void showTab(int t) {
        tab_ = t;
        tabVue_->setSelected(t == 0);
        tabQui_->setSelected(t == 1);
        invalidateLayout();
        invalidate();
    }
    // Le texte de la comparaison (<< compares aux 30 jours d'avant >>).
    [[nodiscard]] std::string compareText() const {
        switch (choice_.range) {
            case statskit::Range::Days7: return "compar\xC3\xA9 aux 7 jours d'avant";
            case statskit::Range::Days30: return "compar\xC3\xA9 aux 30 jours d'avant";
            case statskit::Range::Days90: return "compar\xC3\xA9 aux 90 jours d'avant";
            case statskit::Range::Months12: return "compar\xC3\xA9 aux 12 mois d'avant";
            default: return "compar\xC3\xA9 \xC3\xA0 la p\xC3\xA9riode d'avant";
        }
    }

    // ------------------------------------------------------------------ la fiche
    void openCard(const std::string& key, bool own) {
        const ab::Person* p = data_->person(key);
        if (!p) return;
        if (shownRole_ != Role::Plus && !(own && key == ws().adminKey().keyHex)) return;   // un admin : sa fiche seulement
        if (card_) {
            (void)removeChild(*card_);
            card_ = nullptr;
        }
        cardKey_ = key;
        cardOwn_ = own;
        selectedKey_ = key;
        card_ = &adopt(*this, ab::makeCard(data_, actions_, host_, key, own));
        for (int k = 0; k < 7; ++k) anchors_[k]->setSelected(k == 0);
        loadCard(key);
        invalidateLayout();
        invalidate();
    }
    void closeCard() {
        if (!card_ && cardKey_.empty()) return;
        cardKey_.clear();
        if (card_) {
            (void)removeChild(*card_);
            card_ = nullptr;
        }
        if (overview_) ab::overviewSelect(*overview_, selectedKey_);
        invalidateLayout();
        invalidate();
    }

    // ------------------------------------------------------------------ les chiffres du service (en arriere-plan)
    void loadService() {
        const Role role = ws().role();
        if (role != Role::Plus && role != Role::Admin) return;
        bool old = false;
        const std::string note = statskit::blocked(ws(), &old);
        if (!note.empty()) {
            data_->serviceNote = note;
            data_->serviceTooOld = old;
            data_->serviceLoading = false;
            data_->serviceLoaded = false;
            refreshViews();
            return;
        }
        ab::ServiceAsk ask;
        ask.period = data_->statsPeriod;
        ask.before = data_->statsBefore;
        std::string sig = ask.period.from + ">" + ask.period.to + "#" + std::to_string(changes::current());   // 1.48.1 : un changement : relus
        for (const auto& p : data_->people) {
            if (p.key.size() != 64) continue;
            ask.keys.push_back(p.key);
            ask.ids.push_back(p.id);
            ask.names.push_back(p.name);
            sig += "|" + p.key;
        }
        if (sig == serviceSig_ && (data_->serviceLoaded || serviceBusy_ || !data_->serviceNote.empty())) return;
        if (serviceBusy_) {
            wantService_ = true;
            return;
        }
        wantService_ = false;
        serviceBusy_ = true;
        if (sig != serviceSig_) data_->cards.clear();   // 1.48.1 : les fiches aussi
        serviceSig_ = sig;
        data_->serviceLoading = true;
        auto out = std::make_shared<ab::ServiceReply>();
        const Workspace* w = &ws();
        auto alive = alive_;
        const bool started = statskit::runQuietly(
            host_, "Administrateurs : les chiffres du service", [w, ask, out] { return ab::fetchService(*w, ask, *out); },
            [this, alive, out, sig](const OpResult& r) {
                if (!*alive) return;
                serviceBusy_ = false;
                if (sig == serviceSig_) {
                    if (!r.ok && out->note.empty()) out->note = r.message;
                    ab::applyService(*data_, std::move(*out));
                    ab::computeLocal(ws(), *data_);
                    fillTable();
                    refreshViews();
                    if (!cardKey_.empty()) loadCard(cardKey_);
                }
                if (wantService_) loadService();
            });
        if (!started) {
            serviceBusy_ = false;
            data_->serviceLoading = false;
            serviceSig_.clear();
            wantService_ = true;
        }
        refreshViews();
    }
    void loadCard(const std::string& key) {
        if (!data_->serviceLoaded || key.empty()) return;
        ab::CardFigures& c = data_->cards[key];
        if (c.loaded || c.loading || !c.problem.empty()) return;
        const ab::AdminFigures* f = data_->fig(key);
        const ab::Person* p = data_->person(key);
        ab::CardAsk ask;
        ask.period = data_->statsPeriod;
        ask.before = data_->statsBefore;
        ask.key = key;
        ask.serviceId = f && !f->serviceId.empty() ? f->serviceId : p ? p->id : std::string();
        ask.now = ws().now();
        c.loading = true;
        auto out = std::make_shared<ab::CardFigures>();
        const Workspace* w = &ws();
        auto alive = alive_;
        const std::string sig = serviceSig_;
        const bool started = statskit::runQuietly(
            host_, "Administrateurs : sa fiche", [w, ask, out] { return ab::fetchCard(*w, ask, *out); },
            [this, alive, out, key, sig](const OpResult& r) {
                if (!*alive) return;
                if (sig != serviceSig_) return;   // la periode a change : une autre lecture viendra
                if (!r.ok && out->problem.empty()) out->problem = r.message;
                out->loading = false;
                data_->cards[key] = std::move(*out);
                refreshViews();
            });
        if (!started) {
            data_->cards[key].loading = false;
            wantCard_ = true;
        }
        refreshViews();
    }
    void refreshViews() {
        if (overview_) ab::refreshView(*overview_);
        if (who_) ab::refreshView(*who_);
        if (card_) ab::refreshView(*card_);
        invalidateLayout();
        invalidate();
    }
    [[nodiscard]] std::string bannerText() const {
        if (data_->serviceTooOld) return data_->serviceNote;
        if (!data_->serviceNote.empty() && shownRole_ == Role::Plus)
            return "Les chiffres du service (licences, demandes, montant) ne se lisent pas : " + data_->serviceNote +
                   ". Ceux du journal des admins sont l\xC3\xA0.";
        return {};
    }
    // Le bandeau sous l'en-tete ; rend le haut de la vue.
    float placeBanner(float y) {
        bannerRect_ = {};
        const std::string text = bannerText();
        if (text.empty()) return y;
        const bool old = data_->serviceTooOld;
        const float bw = old ? buttonWidth(*deploy_, 150.f) : 0.f;
        const float bh = statskit::bannerHeight(text, w_, old ? bw + 16.f : 0.f);
        bannerRect_ = {x0_, y, w_, bh};
        if (old) {
            deploy_->setBounds({x0_ + w_ - bw - 12.f, y + (bh - 32.f) * 0.5f, bw, 32.f});
            deploy_->setVisibility(ui::Visibility::Visible);
        }
        return y + bh + 10.f;
    }

    // ------------------------------------------------------------------ l'admin+ : le tableau
    void layoutBoard(const Rect& b) {
        titleY_ = b.y + 18.f;
        float right = x0_ + w_;
        const float aw = buttonWidth(*add_, 150.f);
        add_->setBounds({right - aw, titleY_ + 2.f, aw, 38.f});
        add_->setVisibility(ui::Visibility::Visible);
        right -= aw + 12.f;
        const float rw = range_->preferredWidth();
        const bool roomy = right - x0_ - rw > 280.f;
        range_->setVisibility(ui::Visibility::Visible);
        if (roomy) {
            range_->setBounds({right - rw, titleY_ + 4.f, rw, 34.f});
            right -= rw + 10.f;
        }
        titleRight_ = right;
        float y = titleY_ + 72.f;
        // les onglets ; a droite : les dates, Relever
        float tx = x0_;
        for (statskit::TabButton* t : {tabVue_, tabQui_, static_cast<statskit::TabButton*>(rightsPage_), tabJournal_}) {
            const float tw = t->preferredWidth();
            t->setBounds({tx, y, tw, 34.f});
            t->setVisibility(ui::Visibility::Visible);
            tx += tw + 22.f;
        }
        float tr = x0_ + w_;
        const float lw = buttonWidth(*reload_, 90.f);
        reload_->setBounds({tr - lw, y, lw, 32.f});
        reload_->setVisibility(ui::Visibility::Visible);
        tr -= lw + 10.f;
        if (!roomy) {
            range_->setBounds({std::max(tx, tr - rw), y, std::min(rw, tr - tx), 32.f});
            tr -= rw + 10.f;
        }
        if (choice_.range == statskit::Range::Dates) {
            const float dw = 136.f;
            to_->setBounds({tr - dw, y + 1.f, dw, 30.f});
            from_->setBounds({tr - 2.f * dw - 22.f, y + 1.f, dw, 30.f});
            arrowX_ = tr - dw - 17.f;
            const bool fits = tr - 2.f * dw - 22.f > tx;
            from_->setVisibility(fits ? ui::Visibility::Visible : ui::Visibility::Collapsed);
            to_->setVisibility(fits ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        }
        y += 42.f;
        tabsBottom_ = y;
        y = placeBanner(y + 12.f);
        const Rect area{x0_, y, w_, std::max(120.f, b.bottom() - 6.f - y)};
        overview_->setBounds(area);
        who_->setBounds(area);
        overview_->setVisibility(tab_ == 0 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        who_->setVisibility(tab_ == 1 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        rightsPage_->setEnabled(true);
        add_->setEnabled(ws().service().ready() && !(host_.busy && host_.busy()));
        const int profiles = static_cast<int>(ws().profiles().size());
        static_cast<statskit::TabButton*>(rightsPage_)->setCount(profiles ? std::to_string(profiles) + (profiles > 1 ? " profils" : " profil") : std::string());
        tabJournal_->setCount(data_->journalLines ? ch::fr::integer(data_->journalLines) : std::string());
    }

    void paintBoard(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        r.drawText({x0_, titleY_}, "Administrateurs", gfx::semibold(kF28), ab::kInk);
        int active = 0, gone = 0;
        for (const auto& row : rows_) {
            if (row.retired) ++gone;
            else ++active;
        }
        std::string lead = count(static_cast<std::size_t>(active), "actif", "actifs");
        if (gone) lead += ", " + count(static_cast<std::size_t>(gone), "retir\xC3\xA9", "retir\xC3\xA9s");
        lead += " \xC2\xB7 du " + ch::fr::ddmm(data_->period.first) + " au " + ch::fr::ddmm(data_->period.last) + ", " + compareText();
        Color lc = ab::kMuted;
        if (fetching_) lead += " \xC2\xB7 relev\xC3\xA9 en cours\xE2\x80\xA6";
        else if (data_->serviceLoading) lead += " \xC2\xB7 lecture du service\xE2\x80\xA6";
        else if (!fetched_.problem.empty()) {
            lead += " \xC2\xB7 service : " + fetched_.problem;
            lc = ab::kAmber;
        }
        r.drawText({x0_, titleY_ + 42.f}, fit(r, lead, kF14, w_), kF14, lc);
        r.fillRect({x0_, tabsBottom_ - 1.f, w_, 1.f}, ab::kLine);
        if (choice_.range == statskit::Range::Dates && from_->visible()) r.drawText({arrowX_, tabsBottom_ - 33.f}, ch::fr::kArrow, kF13, ab::kMuted);
    }

    // ------------------------------------------------------------------ la fiche : son en-tete
    void layoutCard(const Rect& b) {
        const ab::Person* p = data_->person(cardKey_);
        titleY_ = b.y + 14.f;
        const float bw = back_->naturalWidth();
        back_->setBounds({x0_, titleY_, bw, 20.f});
        back_->setVisibility(ui::Visibility::Visible);
        float y = titleY_ + 28.f;
        cardTop_ = y;
        float right = x0_ + w_;
        const bool plus = shownRole_ == Role::Plus && !cardOwn_;
        const bool admin = p && p->kind == ab::Person::Kind::Admin && p->active();
        for (ui::Button* btn : {cardRetire_, cardDerog_, cardRights_, cardJournal_}) {
            const bool on = plus && (btn == cardJournal_ ? (p && p->key.size() == 64) : admin);
            if (!on) continue;
            const float w = buttonWidth(*btn, 96.f);
            if (right - w < x0_ + w_ * 0.45f) break;
            right -= w;
            btn->setBounds({right, y + 8.f, w, 36.f});
            btn->setVisibility(ui::Visibility::Visible);
            right -= 8.f;
        }
        const std::string why = actions_.whyNotRetire ? actions_.whyNotRetire(cardKey_) : std::string();
        cardRetire_->setEnabled(why.empty());
        cardRetire_->setTooltip(why.empty() ? std::string("Son certificat est r\xC3\xA9voqu\xC3\xA9. Ce qu'il a sign\xC3\xA9 reste valide, sauf si tu choisis de r\xC3\xA9voquer aussi ses licences.") : why);
        cardHeadRight_ = right - 12.f;
        y += 70.f;
        // les ancres ; a droite la periode
        const float rw = range_->preferredWidth();
        range_->setVisibility(ui::Visibility::Visible);
        const float tabsRoom = w_ - rw - 16.f;
        float natural = 0.f;
        for (auto* a : anchors_) natural += a->preferredWidth() + 18.f;
        const float k = natural > tabsRoom && natural > 0.f ? std::max(0.55f, tabsRoom / natural) : 1.f;
        float tx = x0_;
        for (auto* a : anchors_) {
            const float tw = a->preferredWidth() * k;
            a->setBounds({tx, y, tw, 34.f});
            a->setVisibility(ui::Visibility::Visible);
            tx += tw + 18.f * k;
        }
        range_->setBounds({x0_ + w_ - rw, y, rw, 32.f});
        if (choice_.range == statskit::Range::Dates) {
            const float dw = 130.f;
            const float dy = y - 40.f;
            to_->setBounds({x0_ + w_ - dw, dy, dw, 30.f});
            from_->setBounds({x0_ + w_ - 2.f * dw - 22.f, dy, dw, 30.f});
            const bool fits = !cardJournal_->visible() || cardJournal_->bounds().x > x0_ + w_ - 2.f * dw - 30.f;
            from_->setVisibility(fits ? ui::Visibility::Visible : ui::Visibility::Collapsed);
            to_->setVisibility(fits ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        }
        y += 42.f;
        tabsBottom_ = y;
        y = placeBanner(y + 12.f);
        card_->setBounds({x0_, y, w_, std::max(120.f, b.bottom() - 6.f - y)});
        card_->setVisibility(ui::Visibility::Visible);
    }
    void paintCardHead(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const ab::Person* p = data_->person(cardKey_);
        r.fillRect({x0_, tabsBottom_ - 1.f, w_, 1.f}, ab::kLine);
        if (!p) return;
        const Rect av{x0_, cardTop_, 56.f, 56.f};
        ab::avatarOf(r, av, *p);
        float x = av.right() + 16.f;
        const float room = cardHeadRight_ - x;
        const FontId fn = gfx::semibold(FontId{24});
        x += ab::text(r, x, cardTop_ + 2.f, p->name, fn, ab::kInk, std::max(80.f, room * 0.5f)) + 12.f;
        const auto chip = [&](const std::string& t, Color fg, Color bg, bool dot) {
            if (x + kit::pillWidth(r, t, kF13, dot) > cardHeadRight_) return;
            x += pill(r, x, cardTop_ + 6.f, t, kF13, fg, bg, 24.f, dot) + 8.f;
        };
        chip(p->kind == ab::Person::Kind::Plus ? "Admin+" : "Admin", p->kind == ab::Person::Kind::Plus ? ab::kVioletInk : ab::kInk2,
             p->kind == ab::Person::Kind::Plus ? ab::kVioletPale : ab::kSunken, false);
        chip(p->retired ? "Retir\xC3\xA9" : "Actif", p->retired ? ab::kRed : ab::kGreen, p->retired ? ab::kRedPale : ab::kGreenPale, !p->retired);
        if (p->kind == ab::Person::Kind::Admin && !p->profile.empty())
            chip(p->profile + (p->gaps > 0 ? " \xC2\xB7 " + std::to_string(p->gaps) + (p->gaps > 1 ? " \xC3\xA9" "carts" : " \xC3\xA9" "cart") : std::string()), ab::kVioletInk,
                 ab::kVioletPale, false);
        if (cardOwn_) chip("ta fiche", ab::kInk2, ab::kSunken, false);
        // la ligne sous le nom
        std::string sub = p->pc.empty() ? std::string() : p->pc;
        if (p->since > 0) sub += (sub.empty() ? "" : " \xC2\xB7 ") + std::string(p->kind == ab::Person::Kind::Plus ? "admin+ depuis le " : "admin depuis le ") + dateFr(p->since);
        if (p->kind == ab::Person::Kind::Admin && !p->addedBy.empty()) sub += ", ajout\xC3\xA9 par " + p->addedBy;
        if (const ab::AdminFigures* f = data_->fig(cardKey_); f && f->hasUsage && f->usage.lastSeen > 0) {
            sub += " \xC2\xB7 vu " + ab::ago(data_->now, f->usage.lastSeen);
            if (!f->usage.city.empty()) sub += " \xC3\xA0 " + f->usage.city + (f->usage.country.empty() ? "" : ", " + f->usage.country);
        }
        (void)ab::text(r, av.right() + 16.f, cardTop_ + 38.f, sub, kF14, ab::kMuted, cardHeadRight_ - av.right() - 16.f);
    }

    // ------------------------------------------------------------------ les donnees
    [[nodiscard]] const AdminRow* selected() const {
        for (const auto& r : rows_)
            if (r.key == selectedKey_) return &r;
        return nullptr;
    }

    void rebuild() {
        const Workspace& w = ws();
        const Role role = w.role();
        const Seconds now = w.now();
        rows_.clear();
        const auto infoOf = [this](const std::string& key) -> const AdminInfo* {
            if (!fetched_.haveView) return nullptr;
            for (const auto& i : fetched_.view.admins)
                if (i.admin.key == key) return &i;
            return nullptr;
        };
        // L'admin+ (ce poste, ou celui que nomme le service).
        {
            AdminRow p;
            p.kind = AdminRow::Kind::Plus;
            p.rights = adm::allRights();
            if (role == Role::Plus && w.signingKey()) {
                p.key = w.signingKey()->publicKeyHex();
                p.id = w.signingKey()->keyId();
                p.name = w.key().name.empty() ? std::string("admin+") : keyOwnerName(w.key().name);
                p.pc = thisPc();
                p.me = true;
            } else if (fetched_.haveView && !fetched_.view.editors.empty()) {
                const auto& e = fetched_.view.editors.front();
                p.key = e.admin.key;
                p.id = e.admin.id;
                p.name = e.admin.name.empty() ? std::string("L'admin+") : e.admin.name;
                p.pc = e.admin.pc;
            } else {
                p.name = "L'admin+";
                p.key = "admin+";
            }
            if (fetched_.haveView)
                for (const auto& e : fetched_.view.editors)
                    if (e.admin.key == p.key) {
                        p.software = e.software;
                        p.version = e.version;
                        p.seen = e.seen;
                        p.lastAction = e.lastAction;
                        if (p.pc.empty()) p.pc = e.admin.pc;
                        if (p.id.empty()) p.id = e.admin.id;
                        if (p.since == 0) p.since = e.admin.since;
                    }
            p.fingerprint = p.key.size() == 64 ? adm::fingerprint(p.key) : std::string();
            p.color = 0;
            rows_.push_back(std::move(p));
        }
        // Les admins : la liste signee de ce poste, ou celle du service si elle est plus recente.
        std::vector<adm::Admin> admins;
        int listNumber = 0;
        if (w.adminList()) {
            admins = w.adminList()->admins;
            listNumber = w.adminList()->number;
        }
        if (fetched_.haveView && fetched_.view.number > listNumber) {
            admins.clear();
            for (const auto& i : fetched_.view.admins) admins.push_back(i.admin);
        }
        int color = 1, order = 1;
        std::vector<AdminRow> retired;
        for (const auto& a : admins) {
            AdminRow r;
            r.kind = AdminRow::Kind::Admin;
            r.key = a.key;
            r.id = a.id;
            r.name = a.name;
            r.pc = a.pc;
            r.machine = a.machine;
            r.since = a.since;
            r.profile = a.profile;
            r.rights = adm::normalizeRights(a.rights);
            r.retired = a.retired;
            r.fingerprint = adm::fingerprint(a.key);
            r.me = (role == Role::Admin || role == Role::Retired) && a.key == w.adminKey().keyHex;
            if (const AdminInfo* i = infoOf(a.key)) {
                r.lastAction = i->lastAction;
                r.software = i->software;
                r.version = i->version;
                r.seen = i->seen;
            }
            r.color = a.active() ? color++ : -1;
            r.order = order++;
            (a.active() ? rows_ : retired).push_back(std::move(r));
        }
        for (auto& r : retired) rows_.push_back(std::move(r));
        // Le choix : garde, sinon le premier admin (la maquette), sinon l'admin+.
        if (!selected()) {
            selectedKey_.clear();
            for (const auto& r : rows_)
                if (r.kind != AdminRow::Kind::Plus) {
                    selectedKey_ = r.key;
                    break;
                }
            if (selectedKey_.empty() && !rows_.empty()) selectedKey_ = rows_.front().key;
        }
        // 1.47.0 : les chiffres des pages des admins (le journal : sur ce poste ; le service : en arriere-plan).
        {
            std::map<std::string, std::vector<std::string>> profileRights;
            for (const auto& pr : w.profiles()) profileRights[pr.name] = adm::normalizeRights(pr.rights);
            std::vector<ab::Person> people;
            for (const auto& r : rows_) {
                ab::Person p;
                p.kind = r.kind == AdminRow::Kind::Plus ? ab::Person::Kind::Plus : ab::Person::Kind::Admin;
                p.key = r.key;
                p.id = r.id;
                p.name = r.name;
                p.pc = r.pc;
                p.machine = r.machine;
                p.profile = r.profile;
                p.fingerprint = r.fingerprint;
                p.email = r.email;
                p.software = r.software;
                p.version = r.version;
                p.since = r.since;
                p.retired = r.retired;
                p.seen = r.seen;
                p.rights = r.rights;
                p.color = r.kind == AdminRow::Kind::Plus ? 0 : r.order;
                p.me = r.me;
                p.mismatch = r.mismatch;
                p.addedBy = r.kind == AdminRow::Kind::Admin ? (role == Role::Plus ? std::string("toi") : std::string("l'admin+")) : std::string();
                if (const auto it = profileRights.find(r.profile); r.kind == AdminRow::Kind::Admin && it != profileRights.end()) {
                    int gaps = 0;
                    for (const auto& x : r.rights) gaps += std::find(it->second.begin(), it->second.end(), x) == it->second.end() ? 1 : 0;
                    for (const auto& x : it->second) gaps += std::find(r.rights.begin(), r.rights.end(), x) == r.rights.end() ? 1 : 0;
                    p.gaps = gaps;
                }
                people.push_back(std::move(p));
            }
            data_->people = std::move(people);
            data_->latestAdmin = kAppVersion;
            ab::setPeriod(*data_, choice_, now, allFrom());
            if (role == Role::Plus || role == Role::Admin || role == Role::Retired) ab::computeLocal(w, *data_);
            if (choice_.range == statskit::Range::All) ab::setPeriod(*data_, choice_, now, allFrom());   // le debut du journal, maintenant lu
        }
        fillTable();
        if (overview_) ab::overviewSelect(*overview_, selectedKey_);
        if (!cardKey_.empty() && !data_->person(cardKey_)) closeCard();
        scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
        showButtons();
        refreshViews();
    }

    // Le tableau des scripts (et de l'export CSV) : une ligne par admin, dans l'ordre de rows_.
    void fillTable() {
        std::vector<std::vector<std::string>> t;
        for (const auto& r : rows_) {
            const js::AdminStats* s = data_->stats(r.key);
            const ab::AdminFigures* f = data_->fig(r.key);
            t.push_back({r.name, stateText(r), r.kind == AdminRow::Kind::Plus ? std::string("Admin+") : r.profile, r.pc, s ? std::to_string(s->actions) : std::string(),
                         s ? ab::num(s->licences) : std::string(), s ? ab::num(s->requests) : std::string(), f ? ab::hoursText(f->meanDelay) : std::string(),
                         f ? ab::euros(f->amount) : std::string(), s && s->last.number ? dateFr(s->last.when) + " " + ab::shortSummary(s->last) : std::string()});
        }
        syncing_ = true;
        table_->setModel(std::make_shared<TextRows>(std::vector<std::string>{"Admin", "\xC3\x89tat", "Profil", "PC", "Actions", "Licences", "Demandes", "D\xC3\xA9lai moyen",
                                                                              "Montant", "Derni\xC3\xA8re action"},
                                                    std::move(t)));
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].key == selectedKey_) table_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
        syncing_ = false;
    }
    void syncTable(const std::string& key) {
        selectedKey_ = key;
        syncing_ = true;
        std::vector<ui::RowIndex> sel;
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].key == key) sel.push_back(static_cast<ui::RowIndex>(i));
        table_->selectModelRows(sel, false);
        syncing_ = false;
        showButtons();
    }

    void select(std::size_t i) {
        if (i >= rows_.size()) return;
        selectedKey_ = rows_[i].key;
        syncing_ = true;
        table_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
        syncing_ = false;
        showButtons();
        invalidateLayout();
        invalidate();
    }

    static std::string stateText(const AdminRow& r) {
        if (r.retired) return "Retir\xC3\xA9";
        return "Actif";
    }

    void showButtons() {
        const bool busy = host_.busy && host_.busy();
        const bool ready = ws().service().ready();
        add_->setEnabled(ready && !busy);
        add_->setTooltip(ready ? std::string("Son nom, son profil, le PC de son Studio : son code d'invitation fait de lui un admin d\xC3\xA8s l'installation de XPG Admin.")
                               : std::string("Il faut d'abord le service des licences (Service en ligne) : le code d'invitation y est v\xC3\xA9rifi\xC3\xA9."));
        reload_->setEnabled(ready && !fetching_);
        const bool configured = ws().service().configured;
        send_->setEnabled(configured && !busy);
        send_->setTooltip(configured ? std::string("Ta cl\xC3\xA9 d'admin na\xC3\xAEt sur ce PC ; ton code d'invitation accept\xC3\xA9, tu es admin.")
                                     : std::string("Il faut d'abord l'adresse du service des licences (R\xC3\xA9gler le service\xE2\x80\xA6)."));
        check_->setEnabled(configured && !busy && !checking_);
    }

    void fetch(bool loud) {
        if (fetching_) return;
        const Role role = ws().role();
        if (role != Role::Plus && role != Role::Admin) return;
        if (!ws().service().ready()) {
            if (loud) host_.flash("Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne).", Tone::Warning);
            return;
        }
        fetching_ = true;
        lastFetch_ = steadySeconds();
        const Workspace* w = &ws();
        auto out = std::make_shared<Fetched>();
        auto alive = alive_;
        const auto work = [w, role, out] {
            // 1.51.0 : l'admin+ signe d'abord les admins inscrits par leur code (la liste les montre tout de suite)
            if (role == Role::Plus) (void)w->admitInvitedAdmins();
            OpResult r = w->serviceAdmins(out->view);
            out->haveView = r.ok;
            if (!r.ok) out->problem = r.message;
            return r;
        };
        const auto done = [this, alive, out](const OpResult&) {
            if (!*alive) return;
            fetching_ = false;
            fetched_ = std::move(*out);
            fetchedAt_ = ws().now();
            rebuild();
            loadService();
        };
        const bool started = loud ? host_.runSlow("Admins : relev\xC3\xA9", work, done) : runQuietly(host_, "Admins : relev\xC3\xA9", work, done);
        if (!started) {
            fetching_ = false;
            if (loud) host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        }
        showButtons();
    }

    void openAdd() {
        auto alive = alive_;
        host_.openDialog(makeAddAdminDialog(host_, [this, alive] {
            if (*alive) fetch(false);
        }));
    }

    void askRetire(const std::string& key) {
        const AdminRow* s = nullptr;
        for (const auto& r : rows_)
            if (r.key == key) s = &r;
        if (!s || s->kind != AdminRow::Kind::Admin || !s->active()) return;
        if (actions_.whyNotRetire && !actions_.whyNotRetire(key).empty()) {
            host_.flash(actions_.whyNotRetire(key), Tone::Warning);
            return;
        }
        auto alive = alive_;
        host_.openDialog(std::make_unique<RetireDialog>(host_, s->key, s->name, [this, alive] {
            if (*alive) fetch(false);
        }));
    }

    // 1.51.0 : XPG Admin - devenir admin avec le code d'invitation (celui de l'installateur, sinon le champ) ; un code
    // refuse (inconnu, expire, deja servi) : la phrase du service, le code se retape.
    void join() {
        const std::string typed = trimmed(reqCode_->text());
        const std::string code = typed.empty() ? ws().pendingInvitationCode() : typed;
        if (code.empty()) {
            invitationProblem_ = "Tape ton code d'invitation (INV-XXXX-XXXX) : il est dans le message de l'admin+.";
            lastState_ = "invitation";
            showButtons();
            invalidateLayout();
            return;
        }
        const Workspace* w = &ws();
        auto alive = alive_;
        auto refused = std::make_shared<bool>(false);
        if (!host_.runSlow("Devenir admin (ton code d'invitation)", [w, code, refused] { return w->joinWithInvitation(code, refused.get()); },
                           [this, alive, refused](const OpResult& r) {
                               if (!*alive) return;
                               if (r.ok) {
                                   invitationProblem_.clear();
                                   reqCode_->setText({});
                                   lastState_ = r.output == "admis" ? std::string("admis") : std::string("inscrit");
                                   lastCheck_ = ws().now();
                               } else {
                                   invitationProblem_ = sentence(r.message.rfind("Refus\xC3\xA9 : ", 0) == 0 ? r.message.substr(10) : r.message);
                                   lastState_ = "invitation";
                               }
                               (void)refused;
                               showButtons();
                               invalidateLayout();
                           }))
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
    }

    void checkJoin() {
        if (checking_) return;
        const Workspace* w = &ws();
        auto x = std::make_shared<AdminRefresh>();
        auto alive = alive_;
        checking_ = true;
        if (!host_.runSlow("Ton ajout comme admin", [w, x] { return w->refreshAdminState(nullptr, x.get()); },
                           [this, alive, x](const OpResult&) {
                               if (!*alive) return;
                               checking_ = false;
                               lastState_ = x->admitted ? std::string("admis") : x->joinState;
                               lastCheck_ = ws().now();
                               showButtons();
                               invalidate();
                           })) {
            checking_ = false;
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        }
        showButtons();
    }

    // Une phrase : une majuscule, un point.
    static std::string sentence(std::string s) {
        if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
        if (!s.empty() && s.back() != '.' && s.back() != '!' && s.back() != '?') s += '.';
        return s;
    }

    [[nodiscard]] int rowAt(gfx::Point p) const {
        if (!rowsRect_.contains(p)) return -1;
        const int i = static_cast<int>((p.y - rowsRect_.y) / rowH_) + scroll_;
        return i >= 0 && static_cast<std::size_t>(i) < rows_.size() && i < scroll_ + visibleRows_ ? i : -1;
    }

    // ------------------------------------------------------------------ un admin : la liste en lecture seule
    void layoutList(const Rect& b) {
        const bool compact = b.h < 680.f;
        titleY_ = b.y + (compact ? 18.f : 26.f);
        float bx = x0_ + w_;
        const float bw = buttonWidth(*reload_, 100.f);
        reload_->setBounds({bx - bw, titleY_ + 4.f, bw, 38.f});
        reload_->setVisibility(ui::Visibility::Visible);
        bx -= bw + 10.f;
        // 1.47.0 : sa fiche (un admin de la liste, pas retire : ses chiffres)
        const bool mine = ws().role() == Role::Admin && data_->person(ws().adminKey().keyHex);
        if (mine) {
            const float mw = buttonWidth(*myCard_, 100.f);
            myCard_->setBounds({bx - mw, titleY_ + 4.f, mw, 38.f});
            myCard_->setVisibility(ui::Visibility::Visible);
            bx -= mw + 10.f;
        }
        listRight_ = bx;
        const float top = titleY_ + (compact ? 74.f : 84.f), bottom = b.bottom() - 18.f;
        const float rightW = std::clamp(w_ * 0.32f, 330.f, 420.f);
        const float leftW = std::max(360.f, w_ - rightW - 16.f);
        listRect_ = {x0_, top, leftW, std::max(160.f, bottom - top)};
        cardRect_ = {x0_ + leftW + 16.f, top, rightW, std::max(160.f, bottom - top)};
        rowH_ = 48.f;
        const float headH = 50.f, footH = 64.f;
        const float avail = listRect_.h - headH - footH;
        visibleRows_ = std::max(1, static_cast<int>(avail / rowH_));
        scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(rows_.size()) - visibleRows_));
        const int shown = std::min(visibleRows_, static_cast<int>(rows_.size()) - scroll_);
        const float listH = headH + static_cast<float>(std::max(0, shown)) * rowH_ + footH;
        listRect_.h = std::min(listRect_.h, std::max(listH, 160.f));
        rowsRect_ = {listRect_.x, listRect_.y + headH, listRect_.w, static_cast<float>(std::max(0, shown)) * rowH_};
    }

    void paintList(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Workspace& w = ws();
        // Le titre et sa ligne.
        r.drawText({x0_, titleY_}, "Administrateurs", gfx::semibold(kF28), c.text);
        int active = 0, gone = 0;
        for (const auto& row : rows_) {
            if (row.retired) ++gone;
            else ++active;
        }
        std::string lead = "Les PC o\xC3\xB9 l'Admin est install\xC3\xA9 \xC2\xB7 " + count(static_cast<std::size_t>(active), "actif", "actifs");
        if (gone) lead += ", " + count(static_cast<std::size_t>(gone), "retir\xC3\xA9", "retir\xC3\xA9s");
        lead += w.role() == Role::Retired ? std::string(" \xC2\xB7 ton acc\xC3\xA8s d'admin a \xC3\xA9t\xC3\xA9 retir\xC3\xA9") : std::string(" \xC2\xB7 tu es admin");
        if (fetching_) lead += " \xC2\xB7 relev\xC3\xA9 en cours\xE2\x80\xA6";
        else if (!fetched_.problem.empty()) lead += " \xC2\xB7 service : " + fetched_.problem;
        else if (!w.service().ready()) lead += " \xC2\xB7 service pas r\xC3\xA9gl\xC3\xA9 : la liste de ce poste";
        r.drawText({x0_, titleY_ + 40.f}, fit(r, lead, kF14, std::max(100.f, listRight_ - 16.f - x0_)), kF14, !fetched_.problem.empty() ? c.warning : c.textMuted);
        const float tw = r.measure("Administrateurs", gfx::semibold(kF28)).width;
        (void)pill(r, x0_ + tw + 14.f, titleY_ + 8.f, "lecture seule", gfx::semibold(kF12), c.textMuted, c.rowAltBg, 24.f);
        cardBox(r, th, listRect_);
        cardBox(r, th, cardRect_);
        paintReadOnly(ctx);
        paintMine(ctx);
    }

    // Un admin : la liste en lecture seule (la maquette, en bas de la planche 01).
    void paintReadOnly(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        r.drawText({listRect_.x + 18.f, listRect_.y + 16.f}, "Administrateurs", gfx::semibold(kF14), c.text);
        (void)pill(r, listRect_.x + 140.f, listRect_.y + 14.f, "lecture seule", gfx::semibold(kF11), c.textMuted, c.rowAltBg, 20.f);
        const std::string right = "sans \xC2\xAB Ajouter un admin\xE2\x80\xA6 \xC2\xBB";
        r.drawText({listRect_.right() - 18.f - r.measure(right, kF12).width, listRect_.y + 17.f}, right, kF12, c.textMuted);
        r.fillRect({listRect_.x + 1.f, rowsRect_.y - 1.f, listRect_.w - 2.f, 1.f}, c.gridLine);
        r.pushClip(rowsRect_);
        for (int vi = 0; vi < visibleRows_; ++vi) {
            const std::size_t i = static_cast<std::size_t>(vi + scroll_);
            if (i >= rows_.size()) break;
            const AdminRow& a = rows_[i];
            const float y = rowsRect_.y + static_cast<float>(vi) * rowH_;
            const Rect row{listRect_.x + 1.f, y, listRect_.w - 2.f, rowH_};
            if (a.me) r.fillRect(row, c.ok.withAlpha(14));
            if (a.key == selectedKey_) r.fillRect({row.x, row.y, 3.f, row.h}, c.accent);
            r.fillRect({row.x, row.bottom() - 1.f, row.w, 1.f}, c.gridLine);
            const bool gone = a.retired != 0;
            avatar(r, {listRect_.x + 16.f, y + (rowH_ - 26.f) * 0.5f, 26.f, 26.f}, a.name, avatarColor(th, gone ? -1 : a.color), 7.f);
            const FontId fn = gfx::semibold(kF14);
            const float ty = y + (rowH_ - r.lineHeight(fn)) * 0.5f;
            float x = listRect_.x + 54.f;
            const std::string nm = fit(r, a.name, fn, listRect_.w * 0.4f);
            r.drawText({x, ty}, nm, fn, gone ? c.textMuted : c.text);
            x += r.measure(nm, fn).width + 8.f;
            if (a.me) {
                r.drawText({x, ty}, "(toi)", kF13, c.textMuted);
                x += r.measure("(toi)", kF13).width + 8.f;
            }
            const bool isPlus = a.kind == AdminRow::Kind::Plus;
            (void)pill(r, x, ty - 2.f, isPlus ? "Admin+" : "Admin", gfx::semibold(kF11), isPlus ? c.accent : c.textMuted, isPlus ? c.accent.withAlpha(22) : c.rowAltBg, 20.f);
            std::string where = gone ? "retir\xC3\xA9 le " + dateFr(a.retired) : (a.pc.empty() ? std::string("?") : a.pc) + " \xC2\xB7 actif";
            r.drawText({listRect_.right() - 18.f - r.measure(where, kF13).width, ty}, where, kF13, c.textMuted);
        }
        r.popClip();
        const Rect note{listRect_.x + 16.f, rowsRect_.bottom() + 12.f, listRect_.w - 32.f, 40.f};
        r.fillRoundedRect(note, c.info.withAlpha(16), 10.f);
        ui::drawIcon(r, ui::Icon::Info, {note.x + 12.f, note.y + 12.f, 15.f, 15.f}, c.info);
        r.drawText({note.x + 36.f, note.y + 11.f},
                   fit(r, "Tu vois qui est admin, sur quel PC, et son \xC3\xA9tat. Ni les cl\xC3\xA9s, ni le journal, ni les boutons : l'admin+ g\xC3\xA8re les admins.", kF13,
                       note.w - 48.f),
                   kF13, c.text);
    }

    // Un admin : ses droits a lui (la fiche de droite).
    void paintMine(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Workspace& w = ws();
        const float fx = cardRect_.x + 20.f, fw = cardRect_.w - 40.f;
        float y = cardRect_.y + 20.f;
        const adm::Admin* m = w.me();
        const std::string name = m ? m->name : w.adminKey().name;
        avatar(r, {fx, y, 44.f, 44.f}, name, avatarColor(th, w.role() == Role::Retired ? -1 : 1), 12.f);
        r.drawText({fx + 56.f, y + 2.f}, fit(r, name.empty() ? std::string("Toi") : name, gfx::semibold(FontId{17}), fw - 60.f), gfx::semibold(FontId{17}), c.text);
        r.drawText({fx + 56.f, y + 26.f}, fit(r, m && !m->profile.empty() ? "profil " + m->profile : std::string("admin"), kF13, fw - 60.f), kF13, c.textMuted);
        y += 66.f;
        if (w.role() == Role::Retired) {
            const Rect warn{fx, y, fw, 58.f};
            r.fillRoundedRect(warn, c.error.withAlpha(20), 10.f);
            (void)drawWrapped(r, warn.x + 12.f, warn.y + 10.f, warn.w - 24.f, w.whyNot("licence_creer"), kF13, c.error, 19.f, 2);
            y += 72.f;
        }
        r.drawText({fx, y}, "Tes droits", gfx::semibold(kF14), c.text);
        r.drawText({fx + 80.f, y + 1.f}, std::to_string(w.myRights().size()) + " sur 13, donn\xC3\xA9s par l'admin+", kF12, c.textMuted);
        y += 28.f;
        r.pushClip({cardRect_.x, y, cardRect_.w, std::max(0.f, cardRect_.bottom() - 70.f - y)});
        y += paintRights(r, th, fx, y, fw, w.myRights(), false);
        r.popClip();
        (void)drawWrapped(r, fx, cardRect_.bottom() - 58.f, fw,
                          "Un bouton gris\xC3\xA9 dit le droit qui manque. Tes droits changent ? L'Admin le voit seul (la liste des admins sign\xC3\xA9" "e).", kF12, c.textMuted,
                          16.f, 3);
    }

    // ------------------------------------------------------------ pas encore admin
    void layoutNone(const Rect& b) {
        titleY_ = b.y + 26.f;
        const float top = titleY_ + 84.f;
        // 1.49.0 : XPG Admin (l'Admin classique) n'a que le chemin de l'admin (1.51.0 : son code d'invitation) ;
        // l'Admin+ sans la cle de l'editeur n'a que la cle de l'editeur (la creer, la restaurer).
        if (license_admin::classique()) {
            noneA_ = {};
            noneB_ = {x0_, top, std::min(640.f, w_), 330.f};
            layoutForm(noneB_);
            return;
        }
        noneA_ = {x0_, top, std::min(640.f, w_), 330.f};
        noneB_ = {};
        const float bx = noneA_.x + 24.f;
        createKey_->setBounds({bx, noneA_.bottom() - 62.f, buttonWidth(*createKey_, 200.f), 38.f});
        restore_->setBounds({createKey_->bounds().right() + 10.f, noneA_.bottom() - 62.f, buttonWidth(*restore_, 160.f), 38.f});
        show(createKey_, true);
        show(restore_, true);
    }
    // 1.51.0 : la carte << Ton code d'invitation >> (XPG Admin) - le code de l'installateur (dit), sinon le champ.
    void layoutForm(const Rect& card) {
        const float fx = card.x + 24.f, fw = card.w - 48.f;
        fileCode_ = license_admin::classique() && lastState_ != "invitation" ? ws().pendingInvitationCode() : std::string();
        if (fileCode_.empty()) {
            reqCode_->setBounds({fx, card.y + 158.f, std::min(fw, 260.f), 36.f});
            show(reqCode_, true);
        }
        send_->setBounds({fx, card.bottom() - 62.f, buttonWidth(*send_, 170.f), 38.f});
        show(send_, true);
        if (!ws().service().configured) {
            service_->setBounds({send_->bounds().right() + 10.f, card.bottom() - 62.f, buttonWidth(*service_, 150.f), 38.f});
            show(service_, true);
        }
    }

    void paintNone(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        r.drawText({x0_, titleY_}, "Administrateurs", gfx::semibold(kF28), c.text);
        // 1.49.0 : l'Admin classique, le chemin de l'admin seul ; l'Admin+, la cle de l'editeur seule (et la phrase du contrat).
        const bool classic = license_admin::classique();
        r.drawText({x0_, titleY_ + 40.f},
                   fit(r, classic ? std::string("Ce PC n'a encore aucune cl\xC3\xA9 d'admin : ton code d'invitation fait de lui un admin.")
                                  : std::string("Ce programme est celui de l'admin+. Un admin installe XPG Admin, re\xC3\xA7u avec son invitation."),
                       kF14, w_),
                   kF14, c.textMuted);
        if (classic) {
            cardBox(r, th, noneB_);
            paintForm(ctx, noneB_);
            return;
        }
        // A : la cle de l'editeur
        cardBox(r, th, noneA_);
        {
            const float fx = noneA_.x + 24.f, fw = noneA_.w - 48.f;
            const Rect ib{fx, noneA_.y + 24.f, 40.f, 40.f};
            r.fillRoundedRect(ib, c.accent, 11.f);
            ui::drawIcon(r, ui::Icon::Lock, {ib.x + 11.f, ib.y + 11.f, 18.f, 18.f}, rgb(0xFFFFFF));
            r.drawText({fx + 54.f, noneA_.y + 26.f}, "Cr\xC3\xA9" "er la cl\xC3\xA9 de l'\xC3\xA9" "diteur", gfx::semibold(kF16), c.text);
            r.drawText({fx + 54.f, noneA_.y + 48.f}, "le premier PC \xC2\xB7 l'admin+", kF13, c.textMuted);
            (void)drawWrapped(r, fx, noneA_.y + 88.f, fw,
                              "Ce PC aura la cl\xC3\xA9 de l'\xC3\xA9" "diteur : il signe tout et certifie les autres admins. Il n'y en a qu'un. La cl\xC3\xA9 ne quitte jamais "
                              "ce PC (chiffr\xC3\xA9" "e pour ce compte) ; fais-en tout de suite une copie de secours.\nTu as d\xC3\xA9j\xC3\xA0 la cl\xC3\xA9 ailleurs ? Restaure sa sauvegarde.",
                              kF14, c.text, 21.f, 8);
        }
        // B (devenir admin) : jamais dans l'Admin+ (1.49.0) - un admin installe XPG Admin.
    }

    void paintForm(const ui::PaintContext& ctx, const Rect& card) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const float fx = card.x + 24.f, fw = card.w - 48.f;
        const Rect ib{fx, card.y + 24.f, 40.f, 40.f};
        r.fillRoundedRect(ib, rgb(0x0E7490), 11.f);
        ui::drawIcon(r, ui::Icon::User, {ib.x + 11.f, ib.y + 11.f, 18.f, 18.f}, rgb(0xFFFFFF));
        r.drawText({fx + 54.f, card.y + 26.f}, "Ton code d'invitation", gfx::semibold(kF16), c.text);
        r.drawText({fx + 54.f, card.y + 48.f}, "ta cl\xC3\xA9 na\xC3\xAEt sur ce PC \xC2\xB7 le code fait de toi un admin", kF13, c.textMuted);
        (void)drawWrapped(r, fx, card.y + 82.f, fw,
                          "Le code est dans le message de l'admin+ (INV-XXXX-XXXX, valable 48 h, une seule fois). Accept\xC3\xA9, tu es admin avec le profil qu'il "
                          "a choisi : ni demande, ni code \xC3\xA0 lui dire.",
                          kF14, c.text, 21.f, 3);
        float below = card.y + 206.f;
        if (!fileCode_.empty()) {
            r.drawText({fx, card.y + 140.f}, "Code d'invitation", gfx::semibold(kF13), c.text);
            r.drawText({fx, card.y + 162.f}, fit(r, fileCode_ + " (pos\xC3\xA9 par l'installateur)", kF14, fw), kF14, c.textMuted);
        } else {
            r.drawText({fx, reqCode_->bounds().y - 20.f}, "Code d'invitation", gfx::semibold(kF13), c.text);
            r.drawText({reqCode_->bounds().right() + 12.f, reqCode_->bounds().y + 10.f},
                       fit(r, "dans le message de l'admin+ (48 h)", kF12, std::max(10.f, fx + fw - reqCode_->bounds().right() - 12.f)), kF12, c.textMuted);
            below = reqCode_->bounds().bottom() + 10.f;
        }
        if (!invitationProblem_.empty()) {
            (void)drawWrapped(r, fx, below, fw, invitationProblem_, kF13, c.error, 18.f, 2);
            below += 38.f;
        }
        if (!ws().service().configured)
            (void)drawWrapped(r, fx, below, fw,
                              "Il faut d'abord l'adresse du service des licences (celle que l'admin+ t'a donn\xC3\xA9" "e) : \xC2\xAB R\xC3\xA9gler le service\xE2\x80\xA6 \xC2\xBB.",
                              kF13, c.warning, 19.f, 3);
    }

    // ------------------------------------------------------------------ ajout en cours (1.51.0)
    // Le code est accepte (ou une cle d'avant attend) : l'Admin+ signe la cle d'office des qu'il est en ligne. Rien a lire,
    // rien a dire ; la coquille regarde toutes les 15 s. Le service n'a pas d'inscription pour cette cle (une cle d'avant,
    // un code refuse) : la carte du code, pour le taper.
    void layoutPending(const Rect& b) {
        titleY_ = b.y + 26.f;
        const float top = titleY_ + 84.f;
        const float cw = std::min(640.f, w_);
        pendingCard_ = {x0_, top, cw, 330.f};
        check_->setBounds({pendingCard_.x + 24.f, pendingCard_.bottom() - 60.f, buttonWidth(*check_, 170.f), 38.f});
        show(check_, true);
        if (pendingUnknown()) {
            noneB_ = {pendingCard_.right() + 20.f, top, std::max(360.f, std::min(520.f, x0_ + w_ - pendingCard_.right() - 20.f)), 330.f};
            if (noneB_.right() > x0_ + w_ + 1.f) noneB_ = {x0_, pendingCard_.bottom() + 20.f, cw, 330.f};
            layoutForm(noneB_);
        }
    }

    // Une cle sans inscription connue : le service n'en a pas (<< aucune >>), un code refuse, ou une cle d'avant la 1.51
    // (aucune inscription notee sur ce poste) - la carte du code, pour le taper.
    [[nodiscard]] bool pendingUnknown() const {
        if (lastState_ == "aucune" || lastState_ == "invitation") return true;
        return lastState_.empty() && ws().account().joinedAt == 0;
    }

    void paintPending(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Workspace& w = ws();
        const AccountInfo a = w.account();
        const bool unknown = pendingUnknown();
        r.drawText({x0_, titleY_}, "Administrateurs", gfx::semibold(kF28), c.text);
        r.drawText({x0_, titleY_ + 40.f},
                   fit(r, unknown ? std::string("Ce PC a une cl\xC3\xA9 d'admin, pas encore dans la liste : ton code d'invitation l'y fait entrer.")
                                  : std::string("Ton code d'invitation est accept\xC3\xA9 : tu es admin d\xC3\xA8s que l'Admin+ a sign\xC3\xA9 ta cl\xC3\xA9 (tout seul)."),
                       kF14, w_),
                   kF14, c.textMuted);
        cardBox(r, th, pendingCard_);
        const float fx = pendingCard_.x + 24.f, fw = pendingCard_.w - 48.f;
        float y = pendingCard_.y + 24.f;
        drawTick(r, fx, y + 6.f, unknown ? c.textDisabled : c.ok);
        r.drawText({fx + 26.f, y}, unknown ? std::string("Pas encore de code accept\xC3\xA9 pour ta cl\xC3\xA9") : std::string("Tu es admin : ajout en cours"),
                   gfx::semibold(FontId{20}), c.text);
        y += 36.f;
        std::string who = a.name.empty() || a.name == "Admin XPG" ? std::string() : a.name;
        if (!a.profile.empty()) who += (who.empty() ? "" : " \xC2\xB7 ") + std::string("profil ") + a.profile;
        if (!a.invitationId.empty()) who += (who.empty() ? "" : " \xC2\xB7 ") + std::string("invitation ") + a.invitationId;
        if (!who.empty()) {
            r.drawText({fx, y}, fit(r, who, kF14, fw), kF14, c.text);
            y += 28.f;
        }
        (void)drawWrapped(r, fx, y, fw,
                          unknown ? std::string("Le service n'a pas d'inscription pour ta cl\xC3\xA9 : tape le code d'invitation que t'envoie l'admin+ (\xC3\xA0 droite).")
                                  : std::string("L'Admin+ de l'\xC3\xA9" "diteur ajoute ta cl\xC3\xA9 \xC3\xA0 la liste sign\xC3\xA9" "e des admins d'office, d\xC3\xA8s qu'il est en ligne : "
                                                "rien \xC3\xA0 faire, rien \xC3\xA0 lui dire. Tes droits s'ouvrent alors tout seuls, puis l'archive arrive."),
                          kF14, c.textMuted, 21.f, 4);
        y += 92.f;
        std::string st = "XPG Admin regarde seul, toutes les 15 s.";
        Color ink = c.textMuted;
        if (lastState_ == "invitation" && !invitationProblem_.empty()) {
            st = invitationProblem_;
            ink = c.error;
        } else if (lastCheck_) {
            st = "V\xC3\xA9rifi\xC3\xA9 " + agoFr(w.now(), lastCheck_) + ". " + st;
        }
        (void)drawWrapped(r, fx, y, fw, st, kF13, ink, 19.f, 2);
        r.drawText({check_->bounds().right() + 14.f, check_->bounds().y + 11.f}, fit(r, "cl\xC3\xA9 " + w.adminKey().id, kF12, fw - check_->bounds().w - 20.f), kF12,
                   c.textMuted);
        if (unknown) {
            cardBox(r, th, noneB_);
            paintForm(ctx, noneB_);
        }
    }

    // --------------------------------------------------------------------- l'etat
    std::vector<AdminRow>  rows_;
    std::string            selectedKey_;
    Fetched                fetched_;
    Seconds                fetchedAt_{0};
    bool                   fetching_{false}, checking_{false}, syncing_{false};
    double                 lastFetch_{-1};
    Role                   shownRole_{Role::None};
    std::string            lastState_;
    Seconds                lastCheck_{0};
    // 1.47.0 : les pages des admins (la periode, les chiffres, la fiche ouverte)
    std::shared_ptr<ab::Data> data_;
    statskit::Choice       choice_;
    ab::Actions            actions_;
    int                    tab_{0};
    std::string            cardKey_, serviceSig_;
    bool                   cardOwn_{false}, serviceBusy_{false}, wantService_{false}, wantCard_{false};
    // les widgets
    ui::Button*            add_{nullptr};
    ui::Button*            rightsPage_{nullptr};
    ui::Button*            reload_{nullptr};
    statskit::TabButton*   tabVue_{nullptr};
    statskit::TabButton*   tabQui_{nullptr};
    statskit::TabButton*   tabJournal_{nullptr};
    statskit::SegGroup*    range_{nullptr};
    ui::DatePicker*        from_{nullptr};
    ui::DatePicker*        to_{nullptr};
    ui::Button*            deploy_{nullptr};
    ab::LinkButton*        back_{nullptr};
    ui::Button*            cardJournal_{nullptr};
    ui::Button*            cardRights_{nullptr};
    ui::Button*            cardRetire_{nullptr};
    ui::Button*            cardDerog_{nullptr};   // 1.47.0 (agent S) : la derogation << Gerer le service >>
    statskit::TabButton*   anchors_[7]{};
    ui::Button*            myCard_{nullptr};
    ab::ScrolledView*      overview_{nullptr};
    ab::ScrolledView*      who_{nullptr};
    ab::ScrolledView*      card_{nullptr};
    ui::Button*            createKey_{nullptr};
    ui::Button*            restore_{nullptr};
    ui::InputText*         reqCode_{nullptr};      // 1.51.0 : le code d'invitation (XPG Admin, sans invitation.txt)
    std::string            fileCode_, invitationProblem_;   // celui de l'installateur ; la phrase d'un code refuse
    ui::Button*            send_{nullptr};
    ui::Button*            service_{nullptr};
    ui::Button*            check_{nullptr};
    ui::TableView*         table_{nullptr};
    // la mise en page
    float                  x0_{0.f}, w_{0.f}, titleY_{0.f}, rowH_{64.f}, titleRight_{0.f}, tabsBottom_{0.f}, arrowX_{0.f};
    float                  cardTop_{0.f}, cardHeadRight_{0.f}, listRight_{0.f};
    int                    visibleRows_{1}, scroll_{0};
    Rect                   listRect_{}, cardRect_{}, rowsRect_{}, noneA_{}, noneB_{}, pendingCard_{}, bannerRect_{};
    std::shared_ptr<bool>  alive_ = std::make_shared<bool>(true);
    core::ConnectionScope  links_;
};

} // namespace

PagePtr makeAdminsPage(ShellHost host) { return std::make_unique<AdminsPage>(std::move(host)); }

menu::MenuPtr makeAddAdminDialog(ShellHost host, std::function<void()> done) {
    return std::make_unique<AddAdminDialog>(std::move(host), std::move(done));
}

} // namespace license_admin::gui
