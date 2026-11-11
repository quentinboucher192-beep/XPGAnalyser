// =============================================================================
//  license_admin/gui/PageAdmins.cpp - 1.44.0 : LA PAGE << ADMINISTRATEURS >>
//  (maquette 1.43.0, planches 01 et 02 ; docs/ADMINS.md)
// -----------------------------------------------------------------------------
//  1.47.0 (maquette 1.47.0, planches 01 a 05 ; AdminsBoard.hpp) : l'ADMIN+ voit une periode commune comparee a celle
//  d'avant, et deux onglets - LA VUE D'ENSEMBLE (les tuiles, le classement trie par colonne avec sa barre de filtres,
//  son clic droit et sa ligne depliee, les alertes et leur geste : PageAdminsOverview.cpp) et QUI FAIT QUOI
//  (PageAdminsWho.cpp) ; Droits et Journal des admins y menent. Une ligne : << Ouvrir sa fiche >> (double-clic,
//  Entree) - LA FICHE D'UN ADMIN (PageAdminCard.cpp) sous son en-tete (ses ancres, la periode). Un admin : la liste en
//  lecture seule et << Ma fiche >> (la sienne seulement). Ce qui suit (1.44.0) reste : ajouter, retirer, les
//  demandes recues, les droits.
//  Une page, quatre visages selon le role du poste (Workspace::role) :
//    - l'ADMIN+ (la cle de l'editeur) : le tableau des admins (lui d'abord, puis
//      chaque admin de la liste signee, puis les demandes recues << En attente
//      d'acceptation >>), la fiche de celui choisi (ses chiffres au journal, sa
//      cle en 6 groupes, son certificat, ses droits, << Voir son journal >>,
//      << Ses droits... >>, << Retirer cet admin... >> et sa confirmation qui
//      propose << Revoquer aussi ses licences >>), et << Ajouter un admin... >> :
//      le dialogue en 3 etapes (son nom ; il installe l'Admin, sa demande arrive ;
//      l'empreinte lue au telephone, cochee, puis accepter avec un profil) ;
//    - un ADMIN (ou un admin retire) : la meme liste en LECTURE SEULE (qui est
//      admin, sur quel PC, son etat), et ses droits a lui ;
//    - PAS ENCORE ADMIN (aucune cle) : les deux chemins - << Creer la cle de
//      l'editeur >> (le premier PC, Parametres de securite) ou << Demander a
//      devenir admin >> (son nom, son courriel : Workspace::requestAdmin) ;
//    - EN ATTENTE : << L'admin+ doit accepter ta cle >>, l'empreinte en 6 grands
//      groupes a lui lire au telephone ; la coquille regarde la demande toutes
//      les 15 s et passe le poste admin des qu'elle est acceptee.
//  Ce qui parle au service (la liste et ce qu'il en sait, les demandes d'admin,
//  accepter, refuser, retirer) passe par runSlow (ou runQuiet : le releve
//  automatique), jamais sur le fil de l'interface. Les tableaux sont dessines ;
//  un tableau cache (<< admins.table >>) porte la meme liste pour les scripts
//  (--choisir admins.table=N) et les essais.
// =============================================================================
#include "AdminsBoard.hpp"   // 1.47.0 : les pages des admins (planches 01 a 05)
#include "AdminsKit.hpp"
#include "ListKit.hpp"
#include "Page.hpp"

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
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
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

// Une ligne du tableau : l'admin+, un admin de la liste (actif ou retire), une demande recue.
struct AdminRow {
    enum class Kind { Plus, Admin, Pending } kind{Kind::Admin};
    std::string              key, id, name, pc, machine, fingerprint, profile, email, software, version;
    Seconds                  since{0}, retired{0}, lastAction{0}, seen{0}, received{0};
    std::vector<std::string> rights;
    std::string              lastSummary;
    int                      actions30{0}, licences{0};
    bool                     me{false}, mismatch{false};
    int                      color{0};
    int                      order{0};   // 1.47.0 : son rang dans la liste (retires compris) : sa couleur dans les pages des admins
    [[nodiscard]] bool active() const { return kind != Kind::Pending && retired == 0; }
};

// Ce que le service a dit (GET /admin/admins, et les demandes d'admin pour l'admin+).
struct Fetched {
    bool                      haveView{false}, haveRequests{false};
    AdminsView                view;
    std::vector<AdminRequest> requests;
    std::string               problem;
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

// Les 6 groupes de l'empreinte, en cases (la grande : 2 x 3 ; la petite : une ligne).
void paintFingerprint(gfx::IRenderer& r, const ui::Theme& th, const Rect& box, const std::string& fingerprint, int perRow, float cellH, FontId f, bool dim = false) {
    const auto& c = th.color;
    const auto g = groups(fingerprint);
    if (g.empty()) return;
    const float gap = 8.f;
    const int rows = (static_cast<int>(g.size()) + perRow - 1) / perRow;
    const float cw = (box.w - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow);
    for (std::size_t i = 0; i < g.size(); ++i) {
        const int row = static_cast<int>(i) / perRow, col = static_cast<int>(i) % perRow;
        const Rect cell{box.x + static_cast<float>(col) * (cw + gap), box.y + static_cast<float>(row) * (cellH + gap), cw, cellH};
        ui::shapes::fillFramedRect(r, cell, c.panelBg, c.borderStrong, 8.f);
        r.drawText({cell.x + (cell.w - r.measure(g[i], f).width) * 0.5f, cell.y + (cell.h - r.lineHeight(f)) * 0.5f}, g[i], f, dim ? c.textMuted : c.text);
    }
    (void)rows;
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
// Le dialogue de la planche 02 : trois colonnes, toujours visibles (on voit ou l'on en est). Les demandes d'admin sont
// relevees toutes les 10 s tant qu'il est ouvert et qu'aucune ne correspond (runQuiet : sans bruit), ou sur
// << Relever maintenant >>.
class AddAdminBody final : public ui::Widget {
public:
    static constexpr double kPollEvery = 10.0;

    AddAdminBody(Dialog& owner, ShellHost host, std::string requestKey, std::function<void()> done)
        : ui::Widget("dialog.ajouter-admin.corps"), owner_(owner), host_(std::move(host)), wanted_(std::move(requestKey)), done_(std::move(done)) {
        opened_ = host_.workspace().now();
        name_ = &adopt(*this, std::make_unique<ui::InputText>("admins.ajouter.nom"));
        name_->setPlaceholder("Nina Perret");
        name_->setMaxLength(60);
        email_ = &adopt(*this, std::make_unique<ui::InputText>("admins.ajouter.courriel"));
        email_->setPlaceholder("nina@exemple.fr");
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
        // L'envoi de l'Admin (planche 02) : un PC parmi ceux que connait ce poste, ou un lien (48 h, une seule fois).
        pc_ = &adopt(*this, std::make_unique<ui::DropDown>("admins.ajouter.pc"));
        pcs_ = host_.workspace().pcsForMessages();
        {
            std::vector<ui::DropDown::Item> pcItems;
            for (const auto& m : pcs_) {
                ui::DropDown::Item it;
                it.label = m.name.empty() ? m.deviceId.substr(0, 12) : m.name;
                it.value = m.deviceId.empty() ? m.deviceKey : m.deviceId;
                it.note = m.customer + (m.studio.empty() ? std::string() : " \xC2\xB7 " + m.studio);
                it.keywords = m.customer + " " + m.licenseId;
                pcItems.push_back(std::move(it));
            }
            pc_->setItems(std::move(pcItems));
            if (!pcs_.empty()) pc_->setSelectedIndex(0);
        }
        send_ = &button(*this, "Envoyer l'Admin", "admins.ajouter.envoyer", ui::Button::Style::Default, ui::Icon::Mail);
        makeLink_ = &button(*this, "Cr\xC3\xA9" "er le lien", "admins.ajouter.lien.creer", ui::Button::Style::Default, ui::Icon::Link);
        copyLink_ = &button(*this, "Copier le lien", "admins.ajouter.lien.copier", ui::Button::Style::Default, ui::Icon::Copy);
        chooseFile_ = &button(*this, "Choisir l'installateur\xE2\x80\xA6", "admins.ajouter.fichier", ui::Button::Style::Default, ui::Icon::Open);
        chooseFile_->setTooltip("Aucune version publi\xC3\xA9" "e n'a l'installateur de l'Admin : choisis le fichier (XPG-Admin-Setup-....exe), il est d\xC3\xA9pos\xC3\xA9 pour l'occasion.");
        links_ += send_->clicked->connect([this] { sendInstaller(false); });
        links_ += makeLink_->clicked->connect([this] { sendInstaller(true); });
        links_ += copyLink_->clicked->connect([this] {
            if (url_.empty()) return;
            // 1.49.0 : le lien et le code d'invitation, a coller ensemble dans le courriel
            ui::setClipboardText(code_.empty() ? url_ : url_ + "\n" + codeSentence());
            host_.flash(code_.empty() ? std::string("Lien copi\xC3\xA9 : colle-le dans ton courriel au futur admin (valable 48 h, une seule fois).")
                                      : std::string("Lien et code d'invitation copi\xC3\xA9s : colle-les dans ton courriel au futur admin (48 h, une seule fois)."),
                        Tone::Info);
        });
        links_ += chooseFile_->clicked->connect([this] { chooseInstaller(lastWasLink_); });
        links_ += group_->valueChanged->connect([this](int) { update(); });
        refreshNow_ = &button(*this, "Relever maintenant", "admins.ajouter.relever", ui::Button::Style::Default, ui::Icon::Refresh);
        checked_ = &adopt(*this, std::make_unique<ui::Checkbox>("J'ai v\xC3\xA9rifi\xC3\xA9 l'empreinte", "admins.empreinte.verifiee"));
        // 1.49.0 (docs/ADMINS.md, par. 12) : le code d'invitation (montre une fois, Copier) ; les 6 chiffres a taper
        copyCode_ = &button(*this, "Copier", "admins.ajouter.code.copier", ui::Button::Style::Default, ui::Icon::Copy);
        confirm_ = &adopt(*this, std::make_unique<ui::InputText>("admins.confirmation"));
        confirm_->setPlaceholder("123 456");
        confirm_->setMaxLength(9);
        links_ += copyCode_->clicked->connect([this] {
            if (code_.empty()) return;
            ui::setClipboardText(code_);
            host_.flash("Code d'invitation copi\xC3\xA9 : valable 48 h, une seule fois.", Tone::Info);
        });
        links_ += confirm_->textChanged->connect([this](const std::string&) {
            confirmProblem_.clear();
            update();
        });
        refuse_ = &button(*this, "Refuser la demande", "admins.refuser", ui::Button::Style::Danger);
        later_ = &button(*this, "Plus tard", "admins.plus-tard");
        accept_ = &button(*this, "Accepter comme admin", "admins.accepter", ui::Button::Style::Primary, ui::Icon::User);
        links_ += name_->textChanged->connect([this](const std::string&) { update(); });
        links_ += checked_->stateChanged->connect([this](ui::Checkbox::State) { update(); });
        links_ += refreshNow_->clicked->connect([this] { poll(true); });
        links_ += later_->clicked->connect([this] { owner_.close(); });
        links_ += refuse_->clicked->connect([this] { refuse(); });
        links_ += accept_->clicked->connect([this] { accept(); });
        update();
    }
    ~AddAdminBody() override { *alive_ = false; }

    // A chaque image : le releve des demandes, toutes les 10 s tant qu'aucune ne correspond.
    void step() {
        if (matched() || polling_ || sent_) return;
        const double now = steadySeconds();
        if (lastPoll_ >= 0 && now - lastPoll_ < kPollEvery) return;
        poll(false);
    }

protected:
    void onLayout() override {
        frame_.place(*this, 1300.f, 790.f);
        const auto& p = frame_.panel;
        top_ = p.y + 77.f;
        const float gap = 16.f, x0 = p.x + 22.f, colW = (p.w - 44.f - 2.f * gap) / 3.f;
        for (int i = 0; i < 3; ++i) cols_[i] = {x0 + static_cast<float>(i) * (colW + gap), top_ + 18.f, colW, p.bottom() - 86.f - (top_ + 18.f)};
        // Colonne 1
        const Rect& a = cols_[0];
        const float y0 = a.y + 80.f;
        name_->setBounds({a.x + 16.f, y0 + 20.f, a.w - 32.f, 34.f});
        email_->setBounds({a.x + 16.f, y0 + 88.f, a.w - 32.f, 34.f});
        studio_->setBounds({a.x + 30.f, y0 + 160.f, a.w - 60.f, 26.f});
        link_->setBounds({a.x + 30.f, y0 + 212.f, a.w - 60.f, 26.f});
        const float sy = y0 + 264.f;
        const bool byLink = group_->value() == 1;
        show(pc_, !byLink);
        show(send_, !byLink);
        show(makeLink_, byLink && url_.empty());
        show(copyLink_, byLink && !url_.empty());
        if (!byLink) {
            const float bw = buttonWidth(*send_, 130.f);
            send_->setBounds({a.right() - 16.f - bw, sy, bw, 32.f});
            pc_->setBounds({a.x + 16.f, sy, std::max(80.f, send_->bounds().x - 8.f - (a.x + 16.f)), 32.f});
        } else {
            makeLink_->setBounds({a.x + 16.f, sy, buttonWidth(*makeLink_, 130.f), 32.f});
            copyLink_->setBounds({a.x + 16.f, sy, buttonWidth(*copyLink_, 130.f), 32.f});
        }
        show(chooseFile_, needFile_);
        chooseFile_->setBounds({a.x + 16.f, sy + 40.f, buttonWidth(*chooseFile_, 160.f), 30.f});
        profileY_ = sy + (needFile_ ? 120.f : 92.f);
        profile_->setBounds({a.x + 16.f, profileY_ + 22.f, a.w - 32.f, 32.f});
        // Colonne 2
        const Rect& b = cols_[1];
        refreshNow_->setBounds({b.x + 16.f, b.y + 334.f, buttonWidth(*refreshNow_, 160.f), 32.f});
        // 1.49.0 : le code d'invitation, une fois cree (le bas de la colonne 1)
        show(copyCode_, !code_.empty());
        if (!code_.empty()) {
            const float cw = buttonWidth(*copyCode_, 90.f);
            copyCode_->setBounds({a.right() - 28.f - cw, a.bottom() - 86.f + 20.f, cw, 32.f});
        }
        // Colonne 3
        const Rect& c3 = cols_[2];
        checked_->setBounds({c3.x + 30.f, c3.y + 352.f, c3.w - 60.f, 30.f});
        // 1.49.0 : une demande avec invitation - les 6 chiffres a taper a la place de la case
        const AdminRequest* mq = matched();
        const bool byCode = mq && mq->byCode();
        show(checked_, !byCode);
        show(confirm_, byCode);
        confirm_->setBounds({c3.x + 16.f, c3.y + 250.f, std::min(c3.w - 32.f, 220.f), 44.f});
        // Le pied
        const float by = p.bottom() - 58.f;
        const float rw = buttonWidth(*refuse_, 160.f);
        refuse_->setBounds({p.x + 22.f, by, rw, 38.f});
        const float aw = buttonWidth(*accept_, 220.f), lw = buttonWidth(*later_, 100.f);
        accept_->setBounds({p.right() - 22.f - aw, by, aw, 38.f});
        later_->setBounds({accept_->bounds().x - 10.f - lw, by, lw, 38.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const auto& p = frame_.panel;
        (void)frame_.paint(r, th, "Ajouter un admin",
                           "Trois \xC3\xA9tapes. Sa cl\xC3\xA9 na\xC3\xAEt sur son PC ; toi, tu la certifies avec la cl\xC3\xA9 de l'\xC3\xA9" "diteur.", ui::Icon::User);
        // L'etat, en haut a droite.
        const AdminRequest* q = matched();
        {
            std::string s = q ? (q->fingerprintMismatch ? "Empreinte diff\xC3\xA9rente : refuse" : "Demande re\xC3\xA7ue \xC2\xB7 \xC3\xA0 v\xC3\xA9rifier")
                              : "En attente de sa demande";
            const Tint t = q ? (q->fingerprintMismatch ? Tint{c.error, c.error.withAlpha(24)} : Tint{rgb(0xC2410C), rgb(0xFFF4E5)}) : Tint{c.textMuted, c.rowAltBg};
            const FontId f = gfx::semibold(kF13);
            const float w = pillWidth(r, s, f, true);
            (void)pill(r, frame_.closeBox.x - 14.f - w, p.y + 28.f, s, f, t.fg, t.bg, 24.f, true);
        }
        const std::string who = displayName();
        // ---- 1 : son nom et son courriel ----
        stepHead(r, th, cols_[0], 1, "Son nom et son courriel", name_->text().empty() ? std::string("\xC3\xA0 remplir") : "pr\xC3\xA9par\xC3\xA9 \xC3\xA0 " + clockFr(opened_),
                 !trimmed(name_->text()).empty());
        {
            const Rect& a = cols_[0];
            r.drawText({a.x + 16.f, name_->bounds().y - 20.f}, "Pr\xC3\xA9nom et nom", gfx::semibold(kF13), c.text);
            r.drawText({a.x + 16.f, email_->bounds().y - 20.f}, "Courriel", gfx::semibold(kF13), c.text);
            r.drawText({a.x + 16.f, studio_->bounds().y - 24.f}, "Comment lui envoyer l'Admin", gfx::semibold(kF13), c.text);
            for (ui::RadioButton* rb : {studio_, link_}) {
                const Rect box{a.x + 16.f, rb->bounds().y - 6.f, a.w - 32.f, 46.f};
                ui::shapes::strokeRoundedRect(r, box, rb->selected() ? c.accent : c.border, 10.f, rb->selected() ? 1.5f : 1.f);
                const std::string sub = rb == studio_ ? std::string("un PC choisi parmi ses activations") : std::string("valable 48 h, une seule fois");
                r.drawText({rb->bounds().x + 24.f, rb->bounds().bottom() - 2.f}, fit(r, sub, kF12, box.w - 60.f), kF12, c.textMuted);
            }
            // Ce qu'a donne l'envoi (ou ce qu'il faut).
            const float ny = (pc_->visible() ? pc_->bounds().bottom() : std::max(makeLink_->bounds().bottom(), copyLink_->bounds().bottom())) + (needFile_ ? 44.f : 8.f);
            std::string status;
            Color ink = c.textMuted;
            if (sending_) status = "Envoi en cours\xE2\x80\xA6";
            else if (!sendProblem_.empty()) {
                status = sendProblem_;
                ink = c.error;
            } else if (!url_.empty()) {
                status = "Lien (48 h, une seule fois) : " + url_;
                ink = c.text;
            } else if (sentAt_) {
                status = "\xE2\x9C\x93 Envoy\xC3\xA9 \xC3\xA0 " + clockFr(sentAt_) + " : " + sentWhat_;
                ink = c.ok;
            } else if (group_->value() == 0 && pcs_.empty())
                status = "Aucun PC connu sur ce poste : choisis \xC2\xAB Par un lien \xC2\xBB, ou fais-lui installer l'Admin comme tu veux.";
            else
                status = group_->value() == 0 ? std::string("L'installateur arrive dans les notifications de son Studio, sign\xC3\xA9 par l'\xC3\xA9" "diteur.")
                                              : std::string("Un lien \xC3\xA0 coller dans ton courriel : il l'ouvre dans son navigateur.");
            (void)drawWrapped(r, a.x + 16.f, ny, a.w - 32.f, status, kF12, ink, 16.f, 2);
            r.drawText({a.x + 16.f, profile_->bounds().y - 22.f}, "Son profil", gfx::semibold(kF13), c.text);
            const Rect note{a.x + 16.f, a.bottom() - 70.f, a.w - 32.f, 56.f};
            if (!code_.empty()) {
                // 1.49.0 : le code d'invitation, montre ici une seule fois (le service n'en garde que l'empreinte)
                const Rect box{a.x + 16.f, a.bottom() - 86.f, a.w - 32.f, 72.f};
                r.fillRoundedRect(box, c.accent.withAlpha(16), 10.f);
                ui::shapes::strokeRoundedRect(r, box, c.accent, 10.f, 1.5f);
                r.drawText({box.x + 12.f, box.y + 10.f}, code_, gfx::semibold(FontId{24}), c.text);
                r.drawText({box.x + 12.f, box.y + 46.f}, fit(r, "valable 48 h, une seule fois \xC2\xB7 dans le message", kF12, copyCode_->bounds().x - box.x - 20.f), kF12,
                           c.textMuted);
            } else if (note.y > profile_->bounds().bottom() + 8.f) {
                r.fillRoundedRect(note, c.accent.withAlpha(16), 10.f);
                (void)drawWrapped(r, note.x + 12.f, note.y + 10.f, note.w - 24.f,
                                  "Il aura les droits du profil choisi (une copie : tu les ajustes ensuite pour lui seul). Ceux de l'admin+ restent \xC3\xA0 toi.", kF12,
                                  c.accent, 17.f, 2);
            }
        }
        // ---- 2 : il installe l'Admin ----
        stepHead(r, th, cols_[1], 2, (who.empty() ? std::string("Il") : who) + " installe l'Admin",
                 q ? "demande re\xC3\xA7ue \xC3\xA0 " + clockFr(q->received) : std::string("en attente"), q != nullptr);
        {
            const Rect& b = cols_[1];
            float y = b.y + 80.f;
            const auto item = [&](bool done, const std::string& title, const std::string& sub, const std::string& when) {
                if (done) drawTick(r, b.x + 18.f, y + 3.f, c.ok);
                else r.fillRoundedRect({b.x + 20.f, y + 5.f, 7.f, 7.f}, c.textDisabled, 3.5f);
                r.drawText({b.x + 40.f, y}, fit(r, title, gfx::semibold(kF13), b.w - 100.f), gfx::semibold(kF13), done ? c.text : c.textMuted);
                if (!when.empty()) r.drawText({b.right() - 16.f - r.measure(when, kF12).width, y + 1.f}, when, kF12, c.textMuted);
                if (!sub.empty()) r.drawText({b.x + 40.f, y + 19.f}, fit(r, sub, kF12, b.w - 60.f), kF12, c.textMuted);
                y += 54.f;
            };
            if (sentAt_ && url_.empty())
                item(true, "L'installateur est dans ses notifications", sentWhat_, clockFr(sentAt_));
            else if (!url_.empty())
                item(true, "Le lien est pr\xC3\xAAt (48 h, une seule fois)", "\xC3\xA0 coller dans ton courriel", clockFr(sentAt_));
            else
                item(!trimmed(name_->text()).empty(), "Invitation pr\xC3\xA9par\xC3\xA9" "e", "son nom, son profil : " + selectedProfile(), clockFr(opened_));
            item(q != nullptr, (who.empty() ? std::string("Il") : who) + " installe l'Admin sur son PC", q ? q->pc + " \xC2\xB7 " + q->software : std::string("le Studio, un lien ou une cl\xC3\xA9 USB"), {});
            item(q != nullptr, "Son Admin a cr\xC3\xA9\xC3\xA9 sa cl\xC3\xA9", "sur son PC ; elle n'en sortira jamais", {});
            item(q != nullptr, "Sa demande d'admin est arriv\xC3\xA9" "e", "avec l'empreinte de sa cl\xC3\xA9", q ? clockFr(q->received) : std::string());
            if (!q) {
                std::string wait = polling_ ? std::string("Relev\xC3\xA9 en cours\xE2\x80\xA6")
                                            : lastProblem_.empty() ? "Relev\xC3\xA9 toutes les 10 s tant que ce dialogue est ouvert."
                                                                   : "Relev\xC3\xA9 : " + lastProblem_;
                if (!requests_.empty() && !polling_)
                    wait = count(requests_.size(), "demande attend", "demandes attendent") + " : tape son nom tel qu'il l'a donn\xC3\xA9, ou choisis-la dans Administrateurs.";
                (void)drawWrapped(r, b.x + 16.f, refreshNow_->bounds().y - 46.f, b.w - 32.f, wait, kF12, lastProblem_.empty() ? c.textMuted : c.warning, 17.f, 2);
            }
            // Ce que voit le futur admin en ce moment
            const Rect mini{b.x + 16.f, b.bottom() - 206.f, b.w - 32.f, 190.f};
            r.drawText({mini.x, mini.y - 22.f}, "CE QUE VOIT " + (who.empty() ? std::string("LE FUTUR ADMIN") : upper(who)) + " EN CE MOMENT", gfx::semibold(kF11), c.textMuted);
            ui::shapes::fillFramedRect(r, mini, c.panelBg, c.border, 12.f);
            r.fillRoundedRect({mini.x, mini.y, mini.w, 36.f}, rgb(0x111A2E), 12.f);
            r.fillRect({mini.x, mini.y + 24.f, mini.w, 12.f}, rgb(0x111A2E));
            r.drawText({mini.x + 14.f, mini.y + 10.f}, "License Manager", gfx::semibold(kF13), rgb(0xFFFFFF));
            const std::string tag = (who.empty() ? std::string("toi") : who) + " \xC2\xB7 admin en attente";
            r.drawText({mini.right() - 14.f - r.measure(tag, kF12).width, mini.y + 11.f}, tag, kF12, rgb(0xC7D0E0));
            const std::string boss = plusName();
            r.drawText({mini.x + 14.f, mini.y + 48.f}, fit(r, boss + " doit accepter ta cl\xC3\xA9", gfx::semibold(kF14), mini.w - 28.f), gfx::semibold(kF14), c.text);
            const bool viaCode = (q && q->byCode()) || (!q && !code_.empty());   // 1.49.0 : 6 chiffres, jamais montres ici
            r.drawText({mini.x + 14.f, mini.y + 68.f},
                       fit(r, viaCode ? std::string("Dis ce code \xC3\xA0 ton admin+ :") : boss + " va t'appeler. Lis-lui ton empreinte :", kF12, mini.w - 28.f), kF12,
                       c.textMuted);
            if (viaCode) r.drawText({mini.x + 14.f, mini.y + 100.f}, "(6 chiffres, sur son \xC3\xA9" "cran : il te les dit)", kF13, c.textDisabled);
            else if (q) paintFingerprint(r, th, {mini.x + 14.f, mini.y + 92.f, mini.w - 28.f, 64.f}, q->fingerprint, 3, 28.f, gfx::semibold(kF13));
            else r.drawText({mini.x + 14.f, mini.y + 100.f}, "(son empreinte, d\xC3\xA8s qu'il l'a envoy\xC3\xA9" "e)", kF13, c.textDisabled);
            r.drawText({mini.x + 14.f, mini.bottom() - 24.f}, "En attendant, l'Admin ne peut rien signer.", kF12, c.textMuted);
        }
        // ---- 3 (1.49.0) : une demande avec invitation - les 6 chiffres que dit le futur admin ----
        const bool byCode = q && q->byCode();
        if (byCode) {
            stepHead(r, th, cols_[2], 3, "Tu tapes le code que te dit " + (who.empty() ? std::string("le futur admin") : who) + ", et tu acceptes",
                     "6 chiffres, sur l'\xC3\xA9" "cran de son Admin", false, true);
            const Rect& d = cols_[2];
            float y = d.y + 78.f;
            y += drawWrapped(r, d.x + 16.f, y, d.w - 32.f,
                             "Appelle " + (who.empty() ? std::string("le futur admin") : who) +
                                 " : son Admin lui montre 6 chiffres. Tape-les ici ; justes, la cl\xC3\xA9 de l'\xC3\xA9" "diteur signe. Faux : rien n'est sign\xC3\xA9.",
                             kF13, c.text, 19.f, 4) +
                 10.f;
            r.drawText({d.x + 16.f, y}, "SON INVITATION", gfx::semibold(kF11), c.textMuted);
            r.drawText({d.x + 16.f, y + 20.f},
                       fit(r, q->invitationId + (q->invitationProfile.empty() ? std::string() : " \xC2\xB7 " + q->invitationProfile) +
                                  (q->invited ? " \xC2\xB7 invit\xC3\xA9 \xC3\xA0 " + clockFr(q->invited) : std::string()),
                           kF12, d.w - 32.f),
                       kF12, c.text);
            r.drawText({d.x + 16.f, confirm_->bounds().y - 20.f}, "Les 6 chiffres", gfx::semibold(kF13), c.text);
            if (!confirmProblem_.empty())
                (void)drawWrapped(r, d.x + 16.f, confirm_->bounds().bottom() + 12.f, d.w - 32.f, confirmProblem_, kF12, c.error, 17.f, 4);
        }
        // ---- 3 : l'empreinte au telephone ----
        if (!byCode) stepHead(r, th, cols_[2], 3, "Tu v\xC3\xA9rifies l'empreinte avec " + (who.empty() ? std::string("lui") : who) + ", et tu acceptes",
                 "au t\xC3\xA9l\xC3\xA9phone, pas par courriel", false, true);
        if (!byCode) {
            const Rect& d = cols_[2];
            float y = d.y + 78.f;
            y += drawWrapped(r, d.x + 16.f, y, d.w - 32.f,
                             "Appelle " + (who.empty() ? std::string("le futur admin") : who) +
                                 " et lisez l'empreinte ensemble, groupe par groupe. Il la voit sur son \xC3\xA9" "cran.",
                             kF13, c.text, 19.f, 3) +
                 10.f;
            r.drawText({d.x + 16.f, y}, "L'EMPREINTE DE SA CL\xC3\x89, RE\xC3\x87UE ICI", gfx::semibold(kF11), c.textMuted);
            y += 20.f;
            if (q) {
                paintFingerprint(r, th, {d.x + 16.f, y, d.w - 32.f, 128.f}, q->fingerprint, 3, 58.f, gfx::semibold(FontId{24}));
                r.drawText({d.x + 16.f, y + 134.f},
                           fit(r, "cl\xC3\xA9 cr\xC3\xA9\xC3\xA9" "e sur " + q->pc + " \xC2\xB7 re\xC3\xA7ue \xC3\xA0 " + clockFr(q->received) + " \xC2\xB7 " + q->id, kF12, d.w - 32.f),
                           kF12, c.textMuted);
            } else {
                paintFingerprint(r, th, {d.x + 16.f, y, d.w - 32.f, 128.f}, "\xE2\x80\x94 \xE2\x80\x94 \xE2\x80\x94 \xE2\x80\x94 \xE2\x80\x94 \xE2\x80\x94", 3, 58.f,
                                 gfx::semibold(FontId{24}), true);
                r.drawText({d.x + 16.f, y + 134.f}, "Elle arrive avec sa demande.", kF12, c.textMuted);
            }
            const Rect cb = checked_->bounds();
            ui::shapes::strokeRoundedRect(r, {d.x + 16.f, cb.y - 8.f, d.w - 32.f, cb.h + 16.f}, checked_->isChecked() ? c.accent : c.border, 10.f);
            float wy = cb.bottom() + 20.f;
            const Rect warn{d.x + 16.f, wy, d.w - 32.f, 56.f};
            const bool bad = q && q->fingerprintMismatch;
            r.fillRoundedRect(warn, bad ? c.error.withAlpha(22) : rgb(0xFFF7E6), 10.f);
            ui::drawIcon(r, ui::Icon::Warning, {warn.x + 12.f, warn.y + 12.f, 16.f, 16.f}, bad ? c.error : rgb(0xC2410C));
            (void)drawWrapped(r, warn.x + 38.f, warn.y + 10.f, warn.w - 50.f,
                              bad ? std::string("L'empreinte rendue par le service n'est pas celle calcul\xC3\xA9" "e ici : refuse la demande.")
                                  : std::string("Un seul caract\xC3\xA8re diff\xC3\xA8re ? Refuse la demande : quelqu'un s'est gliss\xC3\xA9 entre vous."),
                              kF12, bad ? c.error : rgb(0xB45309), 17.f, 2);
            wy += 68.f;
            const Rect info{d.x + 16.f, d.bottom() - 82.f, d.w - 32.f, 68.f};
            if (info.y > wy) {
                r.fillRoundedRect(info, c.accent.withAlpha(16), 10.f);
                (void)drawWrapped(r, info.x + 12.f, info.y + 10.f, info.w - 24.f,
                                  "En acceptant, la cl\xC3\xA9 de l'\xC3\xA9" "diteur signe son certificat d'admin, ici, sur ton PC, puis le publie sur le service. " +
                                      (who.empty() ? std::string("Il") : who) + " devient admin dans la minute.",
                                  kF12, c.accent, 17.f, 3);
            }
        }
        r.fillRect({p.x + 1.f, p.bottom() - 76.f, p.w - 2.f, 1.f}, c.border);
        r.drawText({refuse_->bounds().right() + 16.f, refuse_->bounds().y + 11.f}, "Tout est \xC3\xA9" "crit dans le journal des admins.", kF13, c.textMuted);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (frame_.hitClose(ev)) {
            owner_.close();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    static std::string upper(std::string s) {
        for (auto& ch : s)
            if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        return s;
    }
    // 1.49.0 : la phrase du code, a coller avec le lien (la meme que dans le message du Studio)
    [[nodiscard]] std::string codeSentence() const {
        return "Ton code d'invitation : " + code_ + " (\xC3\xA0 saisir dans l'installateur, valable 48 h)";
    }
    [[nodiscard]] std::string plusName() const {
        const auto& k = host_.workspace().key();
        return k.name.empty() ? std::string("L'admin+") : k.name;
    }
    [[nodiscard]] std::string selectedProfile() const {
        const auto* it = profile_->selectedItem();
        return it ? it->value : std::string(adm::kAllRightsProfile);
    }
    // Le nom a montrer : celui tape, sinon celui de la demande.
    [[nodiscard]] std::string displayName() const {
        std::string n = trimmed(name_->text());
        if (n.empty())
            if (const AdminRequest* q = matched()) n = q->name;
        const auto sp = n.find(' ');
        return sp == std::string::npos ? n : n.substr(0, sp);   // le prenom (<< Accepter Nina comme admin >>)
    }
    // La demande qui correspond : celle demandee, sinon par le nom tape, sinon la seule en attente.
    [[nodiscard]] const AdminRequest* matched() const {
        if (!wanted_.empty()) {
            for (const auto& q : requests_)
                if (q.key == wanted_) return &q;
            return nullptr;
        }
        const std::string typed = lower(trimmed(name_->text()));
        if (!typed.empty()) {
            for (const auto& q : requests_)
                if (lower(trimmed(q.name)) == typed) return &q;
            for (const auto& q : requests_) {
                const std::string n = lower(trimmed(q.name));
                if (n.rfind(typed, 0) == 0 || typed.rfind(n, 0) == 0) return &q;
            }
            return nullptr;
        }
        return requests_.size() == 1 ? &requests_.front() : nullptr;
    }
    void stepHead(gfx::IRenderer& r, const ui::Theme& th, const Rect& col, int n, const std::string& title, const std::string& sub, bool done,
                  bool current = false) const {
        const auto& c = th.color;
        ui::shapes::fillFramedRect(r, col, c.panelBg, current ? c.accent : c.border, 12.f);
        if (current) ui::shapes::strokeRoundedRect(r, col.inset(-2.f, -2.f), c.accent.withAlpha(60), 14.f, 2.f);
        const Rect badge{col.x + 16.f, col.y + 18.f, 28.f, 28.f};
        if (done) {
            r.fillRoundedRect(badge, c.ok.withAlpha(30), 8.f);
            drawTick(r, badge.x + 8.f, badge.y + 8.f, c.ok);
        } else {
            r.fillRoundedRect(badge, current ? c.accent : c.rowAltBg, 8.f);
            const std::string s = std::to_string(n);
            r.drawText({badge.x + (28.f - r.measure(s, gfx::semibold(kF13)).width) * 0.5f, badge.y + 6.f}, s, gfx::semibold(kF13), current ? rgb(0xFFFFFF) : c.textMuted);
        }
        r.drawText({badge.right() + 12.f, col.y + 16.f}, fit(r, std::to_string(n) + " \xC2\xB7 " + title, gfx::semibold(kF14), col.w - 70.f), gfx::semibold(kF14), c.text);
        r.drawText({badge.right() + 12.f, col.y + 36.f}, fit(r, sub, kF12, col.w - 70.f), kF12, c.textMuted);
        r.fillRect({col.x + 1.f, col.y + 64.f, col.w - 2.f, 1.f}, c.border);
    }

    void update() {
        const AdminRequest* q = matched();
        const std::string who = displayName();
        checked_->setLabel("J'ai v\xC3\xA9rifi\xC3\xA9 l'empreinte avec " + (who.empty() ? std::string("lui") : who));
        accept_->setText("Accepter " + (who.empty() ? std::string("ce futur admin") : who) + " comme admin");
        const bool busy = host_.busy && host_.busy();
        refuse_->setEnabled(q != nullptr && !sent_);
        checked_->setEnabled(q != nullptr && !q->fingerprintMismatch);
        // 1.49.0 : avec invitation, les 6 chiffres tapes (verifies a l'acceptation) a la place de la case
        const bool byCode = q && q->byCode();
        std::size_t digits = 0;
        for (const char ch : confirm_->text()) digits += (ch >= '0' && ch <= '9') ? 1 : 0;
        const bool ok = q && !q->fingerprintMismatch && (byCode ? digits == 6 : checked_->isChecked()) && !sent_ && !busy;
        accept_->setEnabled(ok);
        accept_->setTooltip(!q ? std::string("Sa demande n'est pas encore arriv\xC3\xA9" "e.")
                            : q->fingerprintMismatch ? std::string("L'empreinte ne correspond pas : refuse la demande.")
                            : byCode ? (digits == 6 ? std::string("Les 6 chiffres sont v\xC3\xA9rifi\xC3\xA9s, puis la cl\xC3\xA9 de l'\xC3\xA9" "diteur signe la nouvelle liste.")
                                                    : std::string("Tape les 6 chiffres que te dit le futur admin (son Admin les montre)."))
                            : !checked_->isChecked() ? std::string("Coche d'abord \xC2\xAB J'ai v\xC3\xA9rifi\xC3\xA9 l'empreinte \xC2\xBB, apr\xC3\xA8s l'avoir lue au t\xC3\xA9l\xC3\xA9phone.")
                                                     : std::string("La cl\xC3\xA9 de l'\xC3\xA9" "diteur signe la nouvelle liste des admins, puis la publie."));
        refreshNow_->setEnabled(!polling_);
        send_->setEnabled(!sending_ && !busy && pc_->selectedItem() != nullptr);
        makeLink_->setEnabled(!sending_ && !busy);
        chooseFile_->setEnabled(!sending_ && !busy);
        show(refreshNow_, q == nullptr);
        invalidateLayout();
        invalidate();
    }

    // Envoyer l'installateur de l'Admin : dans les notifications du Studio du PC choisi, ou un lien (48 h, une fois). Aucune
    // version publiee n'a l'installateur : le fichier est demande, puis l'envoi repart.
    void sendInstaller(bool byLink) {
        if (sending_) return;
        lastWasLink_ = byLink;
        const auto* it = pc_->selectedItem();
        if (!byLink && !it) {
            sendProblem_ = "choisis le PC (ses activations, ses demandes)";
            update();
            return;
        }
        const Workspace* w = &host_.workspace();
        const std::string device = it ? it->value : std::string(), name = trimmed(name_->text()), file = fallback_;
        // 1.49.0 (docs/ADMINS.md, par. 12) : le code d'invitation est cree d'abord (une fois par dialogue) et part dans le message
        if (code_.empty() && name.empty()) {
            sendProblem_ = "son nom d'abord : il va avec le code d'invitation";
            update();
            return;
        }
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
        const auto done = [this, alive, url, byLink, code, inv](const OpResult& r) {
            if (!*alive) return;
            sending_ = false;
            if (code_.empty() && !code->empty()) code_ = *code;   // montre une fois, dans ce dialogue seulement
            if (r.ok) {
                // 1.47.0 (agent A) : l'invitation notee (invitations-admins.json) - la liste << Demandes d'admin >> la rapproche
                // (1.49.0 : celle du code est deja notee par inviteAdmin)
                if (code_.empty())
                    (void)host_.workspace().noteAdminInvitation({trimmed(name_->text()), trimmed(email_->text()), selectedProfile(), byLink ? "lien" : "studio", 0, {}, 0});
                sentAt_ = host_.workspace().now();
                sentWhat_ = r.details.empty() ? r.message : r.details.front();
                if (sentWhat_.rfind("Fichier : ", 0) == 0) sentWhat_ = sentWhat_.substr(10);
                if (byLink) url_ = *url;
                needFile_ = false;
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
        if (!host_.runSlow(byLink ? "Lien vers l'installateur de l'Admin" : "Envoyer l'Admin \xC3\xA0 un PC", work, done)) {
            sending_ = false;
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        }
        update();
    }
    void chooseInstaller(bool byLink) {
        ui::FilePick p;
        p.title = "L'installateur de l'Admin (XPG-Admin-Setup-....exe)";
        p.filters = {{"Installateur Windows (.exe)", "exe"}};
        auto alive = alive_;
        host_.pickFile(p, [this, alive, byLink](std::string path) {
            if (!*alive || path.empty()) return;
            fallback_ = path;
            sendInstaller(byLink);
        });
    }

    void poll(bool loud) {
        if (polling_) return;
        polling_ = true;
        lastPoll_ = steadySeconds();
        const Workspace* w = &host_.workspace();
        auto out = std::make_shared<std::vector<AdminRequest>>();
        auto alive = alive_;
        const auto work = [w, out] { return w->pendingAdminRequests(*out); };
        const auto done = [this, alive, out](const OpResult& r) {
            if (!*alive) return;
            polling_ = false;
            lastPoll_ = steadySeconds();
            if (r.ok) {
                requests_ = *out;
                lastProblem_.clear();
            } else {
                lastProblem_ = r.message;
            }
            // Ouvert sur une demande qui n'est plus la : elle a ete traitee ailleurs.
            if (!wanted_.empty() && r.ok && !matched()) lastProblem_ = "cette demande n'est plus en attente (accept\xC3\xA9" "e ou refus\xC3\xA9" "e ailleurs)";
            if (const AdminRequest* q = matched()) {
                if (trimmed(name_->text()).empty()) name_->setText(q->name);
                if (trimmed(email_->text()).empty()) email_->setText(q->email);
            }
            update();
        };
        const bool started = loud ? host_.runSlow && host_.runSlow("Admins : demandes d'admin", work, done)
                                  : runQuietly(host_, "Admins : demandes d'admin", work, done);
        if (!started) polling_ = false;
        update();
    }

    void accept() {
        const AdminRequest* q = matched();
        if (!q || q->fingerprintMismatch || sent_) return;
        // 1.49.0 : une demande avec invitation - les 6 chiffres verifies ici (de sa cle) ; faux : rien n'est signe
        const bool byCode = q->byCode();
        const std::string typed = byCode ? confirm_->text() : std::string();
        if (byCode && !adm::confirmationMatches(q->key, typed)) {
            confirmProblem_ = "Ce code ne correspond pas \xC3\xA0 sa cl\xC3\xA9 : rien n'est sign\xC3\xA9. Fais-lui relire les 6 chiffres que montre son Admin ; "
                              "s'ils diff\xC3\xA8rent encore, refuse la demande.";
            update();
            return;
        }
        if (!byCode && !checked_->isChecked()) return;
        if (host_.busy && host_.busy()) {
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
            return;
        }
        const Workspace* w = &host_.workspace();
        const std::string key = q->key, profile = selectedProfile(), name = q->name;
        sent_ = true;
        auto alive = alive_;
        auto done = done_;
        Dialog* owner = &owner_;
        if (!host_.runSlow("Accepter " + name + " comme admin", [w, key, profile, byCode, typed] { return w->acceptAdmin(key, profile, nullptr, !byCode, typed); },
                           [this, alive, done, owner](const OpResult& r) {
                               if (*alive) {
                                   sent_ = false;
                                   if (r.ok) owner->close();
                                   else update();
                               }
                               if (r.ok && done) done();
                           })) {
            sent_ = false;
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        }
        if (*alive) update();
    }

    void refuse() {
        const AdminRequest* q = matched();
        if (!q || sent_) return;
        if (host_.busy && host_.busy()) {
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
            return;
        }
        const Workspace* w = &host_.workspace();
        const std::string key = q->key, name = q->name;
        sent_ = true;
        auto alive = alive_;
        auto done = done_;
        Dialog* owner = &owner_;
        if (!host_.runSlow("Refuser la demande de " + name, [w, key] { return w->refuseAdmin(key); },
                           [this, alive, done, owner](const OpResult& r) {
                               if (*alive) {
                                   sent_ = false;
                                   if (r.ok) owner->close();
                                   else update();
                               }
                               if (r.ok && done) done();
                           })) {
            sent_ = false;
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        }
        if (*alive) update();
    }

    Dialog&                          owner_;
    ShellHost                        host_;
    std::string                      wanted_;
    std::function<void()>            done_;
    Seconds                          opened_{0};
    std::vector<AdminRequest>        requests_;
    std::string                      lastProblem_;
    bool                             polling_{false}, sent_{false};
    double                           lastPoll_{-1};
    ui::InputText*                   name_{nullptr};
    ui::InputText*                   email_{nullptr};
    std::shared_ptr<ui::RadioGroup>  group_;
    ui::RadioButton*                 studio_{nullptr};
    ui::RadioButton*                 link_{nullptr};
    ui::DropDown*                    profile_{nullptr};
    ui::Button*                      refreshNow_{nullptr};
    ui::DropDown*                    pc_{nullptr};
    ui::Button*                      send_{nullptr};
    ui::Button*                      makeLink_{nullptr};
    ui::Button*                      copyLink_{nullptr};
    ui::Button*                      chooseFile_{nullptr};
    std::vector<MessagePc>           pcs_;
    std::string                      fallback_, url_, sentWhat_, sendProblem_;
    Seconds                          sentAt_{0};
    bool                             sending_{false}, needFile_{false}, lastWasLink_{false};
    float                            profileY_{0.f};
    ui::Checkbox*                    checked_{nullptr};
    ui::Button*                      copyCode_{nullptr};    // 1.49.0 : le code d'invitation (Copier)
    ui::InputText*                   confirm_{nullptr};     // 1.49.0 : les 6 chiffres
    std::string                      code_, confirmProblem_;
    ui::Button*                      refuse_{nullptr};
    ui::Button*                      later_{nullptr};
    ui::Button*                      accept_{nullptr};
    Frame                            frame_;
    float                            top_{0.f};
    Rect                             cols_[3]{};
    std::shared_ptr<bool>            alive_ = std::make_shared<bool>(true);
    core::ConnectionScope            links_;
};

class AddAdminDialog final : public Dialog {
public:
    AddAdminDialog(ShellHost host, std::string requestKey, std::function<void()> done)
        : Dialog("dialog.ajouter-admin"), host_(std::move(host)), key_(std::move(requestKey)), done_(std::move(done)) {}
    [[nodiscard]] std::string title() const override { return "Ajouter un admin"; }

protected:
    core::Status buildUi() override {
        auto body = std::make_unique<AddAdminBody>(*this, host_, key_, done_);
        body_ = body.get();
        setRoot(std::move(body));
        return core::ok();
    }
    void onTick() override {
        if (body_ && !closed()) body_->step();
    }

private:
    ShellHost             host_;
    std::string           key_;
    std::function<void()> done_;
    AddAdminBody*         body_{nullptr};
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
        // 1.47.0 (agent A) : << N demandes d'admin en attente : Voir la liste >> -> Demandes d'admin (PageAdminRequests.cpp)
        requestsLink_ = &adopt(*this, std::make_unique<ab::LinkButton>("Voir la liste", "admins.demandes-liste"));
        links_ += requestsLink_->clicked->connect([this] { host_.navigate("demandes-admins"); });
        reload_ = &button(*this, "Relever", "admins.relever", ui::Button::Style::Default, ui::Icon::Refresh);
        reload_->setTooltip("Relire tout de suite la liste des admins, les demandes et les chiffres du service (l'Admin le fait seul en arrivant sur la page).");
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
        reqName_ = &adopt(*this, std::make_unique<ui::InputText>("admins.demande.nom"));
        reqName_->setPlaceholder("Pr\xC3\xA9nom et nom");
        reqName_->setMaxLength(60);
        reqEmail_ = &adopt(*this, std::make_unique<ui::InputText>("admins.demande.courriel"));
        reqEmail_->setPlaceholder("ton@courriel");
        reqEmail_->setMaxLength(120);
        // 1.49.0 (docs/ADMINS.md, par. 12) : XPG Admin - le code d'invitation (pose par l'installateur ; sinon tape ici)
        reqCode_ = &adopt(*this, std::make_unique<ui::InputText>("admins.demande.invitation"));
        reqCode_->setPlaceholder("INV-XXXX-XXXX");
        reqCode_->setMaxLength(20);
        send_ = &button(*this, "Envoyer ma demande", "admins.demande.envoyer", ui::Button::Style::Primary, ui::Icon::Mail);
        service_ = &button(*this, "R\xC3\xA9gler le service\xE2\x80\xA6", "admins.regler-service", ui::Button::Style::Default, ui::Icon::Globe);
        // En attente.
        check_ = &button(*this, "V\xC3\xA9rifier maintenant", "admins.attente.verifier", ui::Button::Style::Default, ui::Icon::Refresh);

        table_ = &adopt(*this, std::make_unique<ui::TableView>("admins.table"));
        table_->setSelectionMode(ui::SelectionMode::Single);
        table_->setVisibility(ui::Visibility::Collapsed);   // le tableau des scripts : la page dessine le sien

        // Les vues de l'admin+ (la vue d'ensemble, qui fait quoi).
        actions_ = makeActions();
        overview_ = &adopt(*this, ab::makeOverview(data_, actions_, host_));
        who_ = &adopt(*this, ab::makeWho(data_, actions_, host_));

        links_ += add_->clicked->connect([this] { openAdd({}); });
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
        links_ += send_->clicked->connect([this] { sendRequest(); });
        links_ += check_->clicked->connect([this] { checkRequest(); });
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
        const Role role = ws().role();
        if (role != shownRole_) {
            shownRole_ = role;
            fetched_ = Fetched{};
            lastFetch_ = -1;
            closeCard();
        }
        if (role == Role::None && reqName_->text().empty() && !ws().adminKey().name.empty()) reqName_->setText(ws().adminKey().name);
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
                               static_cast<ui::Widget*>(restore_), static_cast<ui::Widget*>(reqName_), static_cast<ui::Widget*>(reqEmail_),
                               static_cast<ui::Widget*>(reqCode_), static_cast<ui::Widget*>(send_), static_cast<ui::Widget*>(service_), static_cast<ui::Widget*>(check_)})
            wd->setVisibility(ui::Visibility::Collapsed);
        for (auto* a : anchors_) a->setVisibility(ui::Visibility::Collapsed);
        if (card_) card_->setVisibility(ui::Visibility::Collapsed);
        requestsLink_->setVisibility(ui::Visibility::Collapsed);   // 1.47.0 (agent A)
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
        a.verify = [this](const std::string& key) {
            selectedKey_ = key;
            openAdd(key);
        };
        a.refuse = [this](const std::string& key) { refuseRequest(key); };
        a.select = [this](const std::string& key) { syncTable(key); };
        a.addAdmin = [this] { openAdd({}); };
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
        if (!p || p->kind == ab::Person::Kind::Pending) return;
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
            if (p.kind == ab::Person::Kind::Pending || p.key.size() != 64) continue;
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
        // 1.47.0 (agent A) : les demandes d'admin en attente -> leur liste (s'il y a la place apres les onglets)
        if (const std::size_t n = fetched_.haveRequests ? fetched_.requests.size() : 0; n > 0) {
            requestsLink_->setText(std::to_string(n) + (n > 1 ? " demandes d'admin en attente : Voir la liste" : " demande d'admin en attente : Voir la liste"));
            const float lw2 = requestsLink_->naturalWidth();
            const float limit = from_->visible() ? from_->bounds().x - 12.f : tr;
            requestsLink_->setBounds({tx, y + 6.f, lw2, 22.f});
            requestsLink_->setVisibility(tx + lw2 <= limit ? ui::Visibility::Visible : ui::Visibility::Collapsed);
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
        int active = 0, waiting = 0, gone = 0;
        for (const auto& row : rows_) {
            if (row.kind == AdminRow::Kind::Pending) ++waiting;
            else if (row.retired) ++gone;
            else ++active;
        }
        std::string lead = count(static_cast<std::size_t>(active), "actif", "actifs");
        if (waiting) lead += ", " + std::to_string(waiting) + " en attente";
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
        // Les demandes recues (l'admin+).
        if (role == Role::Plus && fetched_.haveRequests)
            for (const auto& q : fetched_.requests) {
                AdminRow r;
                r.kind = AdminRow::Kind::Pending;
                r.key = q.key;
                r.id = q.id;
                r.name = q.name;
                r.email = q.email;
                r.pc = q.pc;
                r.machine = q.machine;
                r.software = q.software;
                r.received = q.received;
                r.fingerprint = q.fingerprint;
                r.mismatch = q.fingerprintMismatch;
                r.color = color++;
                r.order = order++;
                rows_.push_back(std::move(r));
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
                p.kind = r.kind == AdminRow::Kind::Plus ? ab::Person::Kind::Plus : r.kind == AdminRow::Kind::Pending ? ab::Person::Kind::Pending : ab::Person::Kind::Admin;
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
                p.received = r.received;
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
        if (r.kind == AdminRow::Kind::Pending) return "En attente d'acceptation";
        if (r.retired) return "Retir\xC3\xA9";
        return "Actif";
    }

    void showButtons() {
        const bool busy = host_.busy && host_.busy();
        const bool ready = ws().service().ready();
        add_->setEnabled(ready && !busy);
        add_->setTooltip(ready ? std::string("Trois \xC3\xA9tapes : son nom ; il installe l'Admin et envoie sa demande ; vous lisez l'empreinte au t\xC3\xA9l\xC3\xA9phone, tu acceptes.")
                               : std::string("Il faut d'abord le service des licences (Service en ligne) : la demande du futur admin y arrive."));
        reload_->setEnabled(ready && !fetching_);
        const bool configured = ws().service().configured;
        send_->setEnabled(configured && !busy);
        send_->setTooltip(configured ? std::string("Ta cl\xC3\xA9 d'admin na\xC3\xAEt sur ce PC ; ta demande part au service des licences.")
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
            OpResult r = w->serviceAdmins(out->view);
            out->haveView = r.ok;
            if (!r.ok) out->problem = r.message;
            if (role == Role::Plus) {
                const OpResult q = w->pendingAdminRequests(out->requests);
                out->haveRequests = q.ok;
                if (!q.ok && out->problem.empty()) out->problem = q.message;
                if (q.ok && r.ok && !out->requests.empty()) r.message += " " + q.message;
            }
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

    void openAdd(const std::string& requestKey) {
        auto alive = alive_;
        host_.openDialog(makeAddAdminDialog(host_, requestKey, [this, alive] {
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

    void refuseRequest(const std::string& key) {
        const AdminRow* s = nullptr;
        for (const auto& r : rows_)
            if (r.key == key) s = &r;
        if (!s || s->kind != AdminRow::Kind::Pending) return;
        const Workspace* w = &ws();
        auto alive = alive_;
        if (!host_.runSlow("Refuser la demande de " + s->name, [w, key] { return w->refuseAdmin(key); },
                           [this, alive](const OpResult& r) {
                               if (*alive && r.ok) fetch(false);
                           }))
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
    }

    void sendRequest() {
        const std::string name = trimmed(reqName_->text()), email = trimmed(reqEmail_->text());
        if (name.empty() || email.empty()) {
            host_.report(OpResult::fail(name.empty() ? "ton nom : l'admin+ le voit dans ta demande" : "ton courriel : l'admin+ s'en sert pour te joindre"));
            return;
        }
        const Workspace* w = &ws();
        auto fp = std::make_shared<std::string>();
        auto alive = alive_;
        // 1.49.0 (docs/ADMINS.md, par. 12) : XPG Admin envoie sa demande avec le code d'invitation (invitation.txt, sinon
        // le champ) ; un code refuse se retape.
        if (license_admin::classique()) {
            const std::string typed = trimmed(reqCode_->text());
            const std::string code = typed.empty() ? ws().pendingInvitationCode() : typed;
            if (code.empty()) {
                invitationProblem_ = "Tape ton code d'invitation (INV-XXXX-XXXX) : il est dans le message de l'admin+.";
                lastState_ = "invitation";
                showButtons();
                invalidateLayout();
                return;
            }
            auto refused = std::make_shared<bool>(false);
            if (!host_.runSlow("Demande pour devenir admin", [w, name, email, code, refused] { return w->requestAdminInvited(name, email, code, refused.get()); },
                               [this, alive, refused](const OpResult& r) {
                                   if (!*alive) return;
                                   if (r.ok) {
                                       invitationProblem_.clear();
                                       reqCode_->setText({});
                                       lastState_ = "attente";
                                       lastCheck_ = ws().now();
                                   } else if (*refused) {
                                       invitationProblem_ = r.message.rfind("Refus\xC3\xA9 : ", 0) == 0 ? r.message.substr(10) : r.message;
                                       if (!invitationProblem_.empty() && invitationProblem_[0] >= 'a' && invitationProblem_[0] <= 'z')
                                           invitationProblem_[0] = static_cast<char>(invitationProblem_[0] - 'a' + 'A');
                                       lastState_ = "invitation";
                                   }
                                   showButtons();
                                   invalidateLayout();
                               }))
                host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
            return;
        }
        if (!host_.runSlow("Demande pour devenir admin", [w, name, email, fp] { return w->requestAdmin(name, email, fp.get()); },
                           [this, alive](const OpResult& r) {
                               if (!*alive) return;
                               if (r.ok) {
                                   lastState_ = "attente";
                                   lastCheck_ = ws().now();
                               }
                           }))
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
    }

    void checkRequest() {
        if (checking_) return;
        const Workspace* w = &ws();
        auto x = std::make_shared<AdminRefresh>();
        auto alive = alive_;
        checking_ = true;
        if (!host_.runSlow("Ta demande d'admin", [w, x] { return w->refreshAdminState(nullptr, x.get()); },
                           [this, alive, x](const OpResult&) {
                               if (!*alive) return;
                               checking_ = false;
                               lastState_ = x->accepted ? std::string("acceptee") : x->requestState;
                               // 1.49.0 : un code refuse, pas encore retape : la demande n'est pas partie (le champ reste)
                               if (lastState_ == "aucune" && !invitationProblem_.empty()) lastState_ = "invitation";
                               lastCheck_ = ws().now();
                               showButtons();
                               invalidate();
                           })) {
            checking_ = false;
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        }
        showButtons();
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
        int active = 0, waiting = 0, gone = 0;
        for (const auto& row : rows_) {
            if (row.kind == AdminRow::Kind::Pending) ++waiting;
            else if (row.retired) ++gone;
            else ++active;
        }
        std::string lead = "Les PC o\xC3\xB9 l'Admin est install\xC3\xA9 \xC2\xB7 " + count(static_cast<std::size_t>(active), "actif", "actifs");
        if (waiting) lead += ", " + std::to_string(waiting) + " en attente";
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
        const float gap = 20.f;
        const float cw = std::min(560.f, (w_ - gap) * 0.5f);
        // 1.49.0 (docs/ADMINS.md, par. 12) : XPG Admin (l'Admin classique) n'a que le chemin de l'admin (B, l'invitation) ;
        // l'Admin+ sans la cle de l'editeur n'a que la cle de l'editeur (A : la creer, la restaurer), jamais B.
        if (license_admin::classique()) {
            noneA_ = {};
            noneB_ = {x0_, top, std::min(640.f, w_), 430.f};
            layoutForm(noneB_);
            return;
        }
        noneA_ = {x0_, top, std::min(640.f, w_), 330.f};
        noneB_ = {};
        (void)cw;
        (void)gap;
        const float bx = noneA_.x + 24.f;
        createKey_->setBounds({bx, noneA_.bottom() - 62.f, buttonWidth(*createKey_, 200.f), 38.f});
        restore_->setBounds({createKey_->bounds().right() + 10.f, noneA_.bottom() - 62.f, buttonWidth(*restore_, 160.f), 38.f});
        show(createKey_, true);
        show(restore_, true);
    }
    void layoutForm(const Rect& card) {
        const float fx = card.x + 24.f, fw = card.w - 48.f;
        reqName_->setBounds({fx, card.y + 178.f, fw, 34.f});
        reqEmail_->setBounds({fx, card.y + 246.f, fw, 34.f});
        send_->setBounds({fx, card.bottom() - 62.f, buttonWidth(*send_, 180.f), 38.f});
        show(reqName_, true);
        show(reqEmail_, true);
        // 1.49.0 : XPG Admin - le code d'invitation : celui de l'installateur (dit), sinon le champ
        fileCode_ = license_admin::classique() ? ws().pendingInvitationCode() : std::string();
        if (license_admin::classique() && fileCode_.empty()) {
            reqCode_->setBounds({fx, card.y + 314.f, std::min(fw, 260.f), 34.f});
            show(reqCode_, true);
        }
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
                   fit(r, classic ? std::string("Ce PC n'a encore aucune cl\xC3\xA9 d'admin : ta demande part avec ton invitation, l'admin+ l'accepte.")
                                  : std::string("Ce programme est celui de l'admin+. Un admin installe XPG Admin, re\xC3\xA7u avec son invitation."),
                       kF14, w_),
                   kF14, c.textMuted);
        if (classic) {
            cardBox(r, th, noneB_);
            paintForm(ctx, noneB_, false);
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

    void paintForm(const ui::PaintContext& ctx, const Rect& card, bool again) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const float fx = card.x + 24.f, fw = card.w - 48.f;
        const Rect ib{fx, card.y + 24.f, 40.f, 40.f};
        r.fillRoundedRect(ib, rgb(0x0E7490), 11.f);
        ui::drawIcon(r, ui::Icon::User, {ib.x + 11.f, ib.y + 11.f, 18.f, 18.f}, rgb(0xFFFFFF));
        r.drawText({fx + 54.f, card.y + 26.f}, again ? "Envoyer ta demande de nouveau" : "Demander \xC3\xA0 devenir admin", gfx::semibold(kF16), c.text);
        r.drawText({fx + 54.f, card.y + 48.f}, "ta cl\xC3\xA9 na\xC3\xAEt sur ce PC \xC2\xB7 l'admin+ la certifie", kF13, c.textMuted);
        const bool classic = license_admin::classique();
        (void)drawWrapped(r, fx, card.y + 82.f, fw,
                          classic ? std::string("Donne ton nom et ton courriel : ta demande part au service avec ton code d'invitation. L'Admin te montre "
                                                "ensuite 6 chiffres \xC3\xA0 dire \xC3\xA0 l'admin+.")
                                  : std::string("L'admin+ t'a fait installer l'Admin ? Donne ton nom et ton courriel : ta demande part au service, l'admin+ "
                                                "t'appelle et tu lui lis ton empreinte."),
                          kF14, c.text, 21.f, 3);
        r.drawText({fx, reqName_->bounds().y - 20.f}, "Pr\xC3\xA9nom et nom", gfx::semibold(kF13), c.text);
        r.drawText({fx, reqEmail_->bounds().y - 20.f}, "Courriel", gfx::semibold(kF13), c.text);
        float below = reqEmail_->bounds().bottom() + 14.f;
        if (classic) {
            // 1.49.0 : le code d'invitation (pose par l'installateur, ou a taper ; refuse : la phrase du service)
            if (!fileCode_.empty()) {
                r.drawText({fx, card.y + 300.f}, "Code d'invitation", gfx::semibold(kF13), c.text);
                r.drawText({fx, card.y + 322.f}, fit(r, fileCode_ + " (pos\xC3\xA9 par l'installateur)", kF14, fw), kF14, c.textMuted);
                below = card.y + 348.f;
            } else {
                r.drawText({fx, reqCode_->bounds().y - 20.f}, "Code d'invitation", gfx::semibold(kF13), c.text);
                r.drawText({reqCode_->bounds().right() + 12.f, reqCode_->bounds().y + 9.f},
                           fit(r, "dans le message de l'admin+ (48 h)", kF12, std::max(10.f, fx + fw - reqCode_->bounds().right() - 12.f)), kF12, c.textMuted);
                below = reqCode_->bounds().bottom() + 8.f;
            }
            if (!invitationProblem_.empty()) {
                (void)drawWrapped(r, fx, below, fw, invitationProblem_, kF13, c.error, 18.f, 2);
                below += 20.f;
            }
        }
        if (!ws().service().configured)
            (void)drawWrapped(r, fx, below, fw,
                              "Il faut d'abord l'adresse du service des licences (celle que l'admin+ t'a donn\xC3\xA9" "e) : \xC2\xAB R\xC3\xA9gler le service\xE2\x80\xA6 \xC2\xBB.",
                              kF13, c.warning, 19.f, 3);
    }

    // ------------------------------------------------------------------ en attente
    void layoutPending(const Rect& b) {
        titleY_ = b.y + 26.f;
        const float top = titleY_ + 84.f;
        const float cw = std::min(640.f, w_);
        pendingCard_ = {x0_, top, cw, 470.f};
        check_->setBounds({pendingCard_.x + 24.f, pendingCard_.bottom() - 60.f, buttonWidth(*check_, 170.f), 38.f});
        show(check_, true);
        requestSent_ = ws().account().requestSent != 0;   // 1.49.0 : une demande d'avant (l'empreinte) reste montree
        if (lastState_ == "refusee" || lastState_ == "aucune" || lastState_ == "invitation") {
            noneB_ = {pendingCard_.right() + 20.f, top, std::max(360.f, std::min(520.f, x0_ + w_ - pendingCard_.right() - 20.f)), 430.f};
            if (noneB_.right() > x0_ + w_ + 1.f) noneB_ = {x0_, pendingCard_.bottom() + 20.f, cw, 430.f};
            layoutForm(noneB_);
        }
    }

    void paintPending(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Workspace& w = ws();
        r.drawText({x0_, titleY_}, "Administrateurs", gfx::semibold(kF28), c.text);
        r.drawText({x0_, titleY_ + 40.f}, fit(r, "Ta demande pour devenir admin attend l'acceptation de l'admin+.", kF14, w_), kF14, c.textMuted);
        cardBox(r, th, pendingCard_);
        const Rect band{pendingCard_.x + 1.f, pendingCard_.y + 1.f, pendingCard_.w - 2.f, 44.f};
        r.fillRoundedRect(band, rgb(0x111A2E), 11.f);
        r.fillRect({band.x, band.y + 30.f, band.w, 14.f}, rgb(0x111A2E));
        r.drawText({band.x + 18.f, band.y + 13.f}, "License Manager", gfx::semibold(kF14), rgb(0xFFFFFF));
        const std::string tag = (w.adminKey().name.empty() ? std::string("toi") : w.adminKey().name) + " \xC2\xB7 admin en attente";
        r.drawText({band.right() - 18.f - r.measure(tag, kF13).width, band.y + 14.f}, tag, kF13, rgb(0xC7D0E0));
        const float fx = pendingCard_.x + 24.f, fw = pendingCard_.w - 48.f;
        float y = band.bottom() + 20.f;
        // 1.49.0 (docs/ADMINS.md, par. 12) : XPG Admin, une demande partie avec l'invitation - les 6 chiffres a dire ; pas
        // encore partie (un code refuse) - rien a dire ; sinon (une demande d'avant) l'empreinte au telephone.
        const std::string code = w.pendingConfirmation();
        if (!code.empty() && lastState_ != "invitation") {
            r.drawText({fx, y}, "L'admin+ doit accepter ta cl\xC3\xA9", gfx::semibold(FontId{20}), c.text);
            y += 32.f;
            r.drawText({fx, y}, fit(r, "Dis ce code \xC3\xA0 ton admin+ : " + code, gfx::semibold(kF14), fw), gfx::semibold(kF14), c.text);
            y += 30.f;
            paintFingerprint(r, th, {fx, y, std::min(fw, 360.f), 84.f}, code, 2, 84.f, gfx::semibold(FontId{34}));
            y += 98.f;
            (void)drawWrapped(r, fx, y, fw, "Il le tape dans ses Demandes d'admin : c'est ce qui prouve que la demande vient de toi.", kF13, c.textMuted, 19.f, 2);
            y += 48.f;
        } else if (license_admin::classique() && code.empty() && (lastState_ == "invitation" || !requestSent_)) {
            r.drawText({fx, y}, "Ta demande n'est pas encore partie", gfx::semibold(FontId{20}), c.text);
            y += 32.f;
            (void)drawWrapped(r, fx, y, fw, "Tape ton code d'invitation (dans le message de l'admin+, valable 48 h), puis envoie ta demande.", kF14, c.textMuted, 21.f, 3);
            y += 176.f;
        } else {
            r.drawText({fx, y}, "L'admin+ doit accepter ta cl\xC3\xA9", gfx::semibold(FontId{20}), c.text);
            y += 32.f;
            r.drawText({fx, y}, "Il va t'appeler. Lis-lui ton empreinte :", kF14, c.textMuted);
            y += 30.f;
            paintFingerprint(r, th, {fx, y, fw, 132.f}, w.adminKey().fingerprint, 3, 62.f, gfx::semibold(FontId{26}));
            y += 146.f;
        }
        r.drawText({fx, y}, "En attendant, l'Admin ne peut rien signer.", kF14, c.text);
        y += 26.f;
        std::string st = "L'Admin regarde ta demande seul, toutes les 15 s.";
        Color ink = c.textMuted;
        if (lastState_ == "refusee") {
            st = "L'admin+ a refus\xC3\xA9 ta demande. Parle-lui, puis envoie-la de nouveau.";
            ink = c.error;
        } else if (lastState_ == "invitation") {
            st = invitationProblem_.empty() ? std::string("Ta demande n'est pas partie : tape ton code d'invitation.") : invitationProblem_;   // 1.49.0
            ink = c.error;
        } else if (lastState_ == "aucune") {
            st = "Le service n'a plus ta demande : envoie-la de nouveau.";
            ink = c.warning;
        } else if (lastCheck_) {
            st = "V\xC3\xA9rifi\xC3\xA9" "e " + agoFr(w.now(), lastCheck_) + " : en attente. " + st;
        }
        (void)drawWrapped(r, fx, y, fw, st, kF13, ink, 19.f, 2);
        r.drawText({check_->bounds().right() + 14.f, check_->bounds().y + 11.f}, fit(r, "cl\xC3\xA9 " + w.adminKey().id, kF12, fw - check_->bounds().w - 20.f), kF12,
                   c.textMuted);
        if (lastState_ == "refusee" || lastState_ == "aucune" || lastState_ == "invitation") {
            cardBox(r, th, noneB_);
            paintForm(ctx, noneB_, true);
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
    ab::LinkButton*        requestsLink_{nullptr};   // 1.47.0 (agent A) : << N demandes d'admin en attente : Voir la liste >>
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
    ui::InputText*         reqName_{nullptr};
    ui::InputText*         reqEmail_{nullptr};
    ui::InputText*         reqCode_{nullptr};      // 1.49.0 : le code d'invitation (XPG Admin, sans invitation.txt)
    std::string            fileCode_, invitationProblem_;   // 1.49.0 : celui de l'installateur ; la phrase d'un code refuse
    bool                   requestSent_{false};               // 1.49.0 : demande-admin.json (une demande est partie)
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

menu::MenuPtr makeAddAdminDialog(ShellHost host, std::string requestKey, std::function<void()> done) {
    return std::make_unique<AddAdminDialog>(std::move(host), std::move(requestKey), std::move(done));
}

} // namespace license_admin::gui
