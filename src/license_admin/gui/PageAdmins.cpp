// =============================================================================
//  license_admin/gui/PageAdmins.cpp - 1.44.0 : LA PAGE << ADMINISTRATEURS >>
//  (maquette 1.43.0, planches 01 et 02 ; docs/ADMINS.md)
// -----------------------------------------------------------------------------
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
#include "AdminsKit.hpp"
#include "Page.hpp"

#include "../../deploy/DevKey.hpp"
#include "../../ui/widgets/DataViews.hpp"

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
    if (id == "demandes") return "les demandes en ligne";
    if (id == "codes") return "revoir les codes";
    if (id == "client_supprimer") return "supprimer un client";
    if (id == "clients_exporter") return "exporter les clients";
    if (id == "courriels") return "\xC3\xA9" "crire aux clients";
    return adm::rightLabel(id);
}

// Une ligne du tableau : l'admin+, un admin de la liste (actif ou retire), une demande recue.
struct AdminRow {
    enum class Kind { Plus, Admin, Pending } kind{Kind::Admin};
    std::string              key, name, pc, fingerprint, profile, email, software, version;
    Seconds                  since{0}, retired{0}, lastAction{0}, seen{0}, received{0};
    std::vector<std::string> rights;
    std::string              lastSummary;
    int                      actions30{0}, licences{0};
    bool                     me{false}, mismatch{false};
    int                      color{0};
    [[nodiscard]] bool active() const { return kind != Kind::Pending && retired == 0; }
};

// Ce que le service a dit (GET /admin/admins, et les demandes d'admin pour l'admin+).
struct Fetched {
    bool                      haveView{false}, haveRequests{false};
    AdminsView                view;
    std::vector<AdminRequest> requests;
    std::string               problem;
};

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
        refreshNow_ = &button(*this, "Relever maintenant", "admins.ajouter.relever", ui::Button::Style::Default, ui::Icon::Refresh);
        checked_ = &adopt(*this, std::make_unique<ui::Checkbox>("J'ai v\xC3\xA9rifi\xC3\xA9 l'empreinte", "admins.empreinte.verifiee"));
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
        float y = a.y + 82.f;
        name_->setBounds({a.x + 16.f, y + 20.f, a.w - 32.f, 34.f});
        y += 70.f;
        email_->setBounds({a.x + 16.f, y + 20.f, a.w - 32.f, 34.f});
        y += 78.f;
        studio_->setBounds({a.x + 30.f, y + 24.f, a.w - 60.f, 26.f});
        link_->setBounds({a.x + 30.f, y + 74.f, a.w - 60.f, 26.f});
        y += 166.f;
        profile_->setBounds({a.x + 16.f, y + 22.f, a.w - 32.f, 32.f});
        // Colonne 2
        const Rect& b = cols_[1];
        refreshNow_->setBounds({b.x + 16.f, b.y + 334.f, buttonWidth(*refreshNow_, 160.f), 32.f});
        // Colonne 3
        const Rect& c3 = cols_[2];
        checked_->setBounds({c3.x + 30.f, c3.y + 352.f, c3.w - 60.f, 30.f});
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
            const float ny = link_->bounds().bottom() + 22.f;
            (void)drawWrapped(r, a.x + 16.f, ny, a.w - 32.f,
                              "L'envoi arrive avec la page Versions. En attendant, fais-lui installer l'Admin comme tu veux (une cl\xC3\xA9 USB, un partage) : "
                              "il y choisit \xC2\xAB Demander \xC3\xA0 devenir admin \xC2\xBB.",
                              kF12, c.textMuted, 17.f, 4);
            r.drawText({a.x + 16.f, profile_->bounds().y - 22.f}, "Son profil", gfx::semibold(kF13), c.text);
            const Rect note{a.x + 16.f, a.bottom() - 70.f, a.w - 32.f, 56.f};
            r.fillRoundedRect(note, c.accent.withAlpha(16), 10.f);
            (void)drawWrapped(r, note.x + 12.f, note.y + 10.f, note.w - 24.f,
                              "Il aura les droits du profil choisi (une copie : tu les ajustes ensuite pour lui seul). Ceux de l'admin+ restent \xC3\xA0 toi.", kF12,
                              c.accent, 17.f, 2);
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
            r.drawText({mini.x + 14.f, mini.y + 68.f}, fit(r, boss + " va t'appeler. Lis-lui ton empreinte :", kF12, mini.w - 28.f), kF12, c.textMuted);
            if (q) paintFingerprint(r, th, {mini.x + 14.f, mini.y + 92.f, mini.w - 28.f, 64.f}, q->fingerprint, 3, 28.f, gfx::semibold(kF13));
            else r.drawText({mini.x + 14.f, mini.y + 100.f}, "(son empreinte, d\xC3\xA8s qu'il l'a envoy\xC3\xA9" "e)", kF13, c.textDisabled);
            r.drawText({mini.x + 14.f, mini.bottom() - 24.f}, "En attendant, l'Admin ne peut rien signer.", kF12, c.textMuted);
        }
        // ---- 3 : l'empreinte au telephone ----
        stepHead(r, th, cols_[2], 3, "Tu v\xC3\xA9rifies l'empreinte avec " + (who.empty() ? std::string("lui") : who) + ", et tu acceptes",
                 "au t\xC3\xA9l\xC3\xA9phone, pas par courriel", false, true);
        {
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
        const bool ok = q && !q->fingerprintMismatch && checked_->isChecked() && !sent_ && !busy;
        accept_->setEnabled(ok);
        accept_->setTooltip(!q ? std::string("Sa demande n'est pas encore arriv\xC3\xA9" "e.")
                            : q->fingerprintMismatch ? std::string("L'empreinte ne correspond pas : refuse la demande.")
                            : !checked_->isChecked() ? std::string("Coche d'abord \xC2\xAB J'ai v\xC3\xA9rifi\xC3\xA9 l'empreinte \xC2\xBB, apr\xC3\xA8s l'avoir lue au t\xC3\xA9l\xC3\xA9phone.")
                                                     : std::string("La cl\xC3\xA9 de l'\xC3\xA9" "diteur signe la nouvelle liste des admins, puis la publie."));
        refreshNow_->setEnabled(!polling_);
        show(refreshNow_, q == nullptr);
        invalidateLayout();
        invalidate();
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
        if (!q || !checked_->isChecked() || q->fingerprintMismatch || sent_) return;
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
        if (!host_.runSlow("Accepter " + name + " comme admin", [w, key, profile] { return w->acceptAdmin(key, profile, nullptr, true); },
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
    ui::Checkbox*                    checked_{nullptr};
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
class AdminsPage final : public Page {
public:
    explicit AdminsPage(ShellHost host) : Page("admins", std::move(host)) {
        // L'admin+ : les gestes de l'en-tete et de la fiche.
        add_ = &button(*this, "Ajouter un admin\xE2\x80\xA6", "admins.ajouter", ui::Button::Style::Primary, ui::Icon::Plus);
        add_->setTooltip("Trois \xC3\xA9tapes : son nom ; il installe l'Admin et envoie sa demande ; vous lisez l'empreinte au t\xC3\xA9l\xC3\xA9phone, tu acceptes.");
        rightsPage_ = &button(*this, "Droits des admins", "admins.droits-page", ui::Button::Style::Default, ui::Icon::Lock);
        rightsPage_->setTooltip("Les profils de droits (Admin LV1, LV2\xE2\x80\xA6), copi\xC3\xA9s vers un admin puis ajust\xC3\xA9s pour lui seul.");
        reload_ = &button(*this, "Relever", "admins.relever", ui::Button::Style::Default, ui::Icon::Refresh);
        reload_->setTooltip("Relire tout de suite la liste des admins et les demandes sur le service (l'Admin le fait seul en arrivant sur la page).");
        journal_ = &button(*this, "Voir son journal", "admins.voir-journal", ui::Button::Style::Default, ui::Icon::History);
        rightsOf_ = &button(*this, "Ses droits\xE2\x80\xA6", "admins.droits", ui::Button::Style::Default, ui::Icon::Lock);
        retire_ = &button(*this, "Retirer cet admin\xE2\x80\xA6", "admins.retirer", ui::Button::Style::Danger, ui::Icon::User);
        verify_ = &button(*this, "V\xC3\xA9rifier et accepter\xE2\x80\xA6", "admins.detail.verifier", ui::Button::Style::Primary);
        refuseReq_ = &button(*this, "Refuser la demande", "admins.detail.refuser", ui::Button::Style::Danger);
        // Pas encore admin : les deux chemins.
        createKey_ = &button(*this, "Cr\xC3\xA9" "er la cl\xC3\xA9 de l'\xC3\xA9" "diteur\xE2\x80\xA6", "admins.creer-cle", ui::Button::Style::Primary, ui::Icon::Lock);
        restore_ = &button(*this, "Restaurer une sauvegarde\xE2\x80\xA6", "admins.restaurer", ui::Button::Style::Default, ui::Icon::Open);
        reqName_ = &adopt(*this, std::make_unique<ui::InputText>("admins.demande.nom"));
        reqName_->setPlaceholder("Pr\xC3\xA9nom et nom");
        reqName_->setMaxLength(60);
        reqEmail_ = &adopt(*this, std::make_unique<ui::InputText>("admins.demande.courriel"));
        reqEmail_->setPlaceholder("ton@courriel");
        reqEmail_->setMaxLength(120);
        send_ = &button(*this, "Envoyer ma demande", "admins.demande.envoyer", ui::Button::Style::Primary, ui::Icon::Mail);
        service_ = &button(*this, "R\xC3\xA9gler le service\xE2\x80\xA6", "admins.regler-service", ui::Button::Style::Default, ui::Icon::Globe);
        // En attente.
        check_ = &button(*this, "V\xC3\xA9rifier maintenant", "admins.attente.verifier", ui::Button::Style::Default, ui::Icon::Refresh);

        table_ = &adopt(*this, std::make_unique<ui::TableView>("admins.table"));
        table_->setSelectionMode(ui::SelectionMode::Single);
        table_->setVisibility(ui::Visibility::Collapsed);   // le tableau des scripts : la page dessine le sien

        links_ += add_->clicked->connect([this] { openAdd({}); });
        links_ += rightsPage_->clicked->connect([this] { host_.navigate("droits"); });
        links_ += reload_->clicked->connect([this] { fetch(true); });
        links_ += journal_->clicked->connect([this] {
            if (const AdminRow* r = selected()) {
                requestPageFocus("journal", "admin:" + r->key);
                host_.navigate("journal");
            }
        });
        links_ += rightsOf_->clicked->connect([this] {
            if (const AdminRow* r = selected()) {
                requestPageFocus("droits", "admin:" + r->key);
                host_.navigate("droits");
            }
        });
        links_ += retire_->clicked->connect([this] { askRetire(); });
        links_ += verify_->clicked->connect([this] {
            if (const AdminRow* r = selected(); r && r->kind == AdminRow::Kind::Pending) openAdd(r->key);
        });
        links_ += refuseReq_->clicked->connect([this] { refuseSelected(); });
        links_ += createKey_->clicked->connect([this] { host_.navigate("securite"); });
        links_ += restore_->clicked->connect([this] { host_.navigate("securite"); });
        links_ += service_->clicked->connect([this] { host_.navigate("service"); });
        links_ += send_->clicked->connect([this] { sendRequest(); });
        links_ += check_->clicked->connect([this] { checkRequest(); });
        links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& sel) {
            if (syncing_) return;
            if (!sel.empty() && sel.front() < rows_.size()) selectedKey_ = rows_[sel.front()].key;
            showButtons();
            invalidate();
        });
        refresh();
    }
    ~AdminsPage() override { *alive_ = false; }

    [[nodiscard]] std::string title() const override { return "Administrateurs"; }

    void refresh() override {
        if (const std::string f = takePageFocus("admins"); f.rfind("admin:", 0) == 0) selectedKey_ = f.substr(6);
        const Role role = ws().role();
        if (role != shownRole_) {
            shownRole_ = role;
            fetched_ = Fetched{};
            lastFetch_ = -1;
        }
        if (role == Role::None && reqName_->text().empty() && !ws().adminKey().name.empty()) reqName_->setText(ws().adminKey().name);
        rebuild();
        // Arriver sur la page : relire ce que le service sait (sans bruit ; au plus une fois par minute).
        if ((role == Role::Plus || role == Role::Admin) && ws().service().ready() && !fetching_ && (lastFetch_ < 0 || steadySeconds() - lastFetch_ > 60.0))
            fetch(false);
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
                               static_cast<ui::Widget*>(journal_), static_cast<ui::Widget*>(rightsOf_), static_cast<ui::Widget*>(retire_),
                               static_cast<ui::Widget*>(verify_), static_cast<ui::Widget*>(refuseReq_), static_cast<ui::Widget*>(createKey_),
                               static_cast<ui::Widget*>(restore_), static_cast<ui::Widget*>(reqName_), static_cast<ui::Widget*>(reqEmail_),
                               static_cast<ui::Widget*>(send_), static_cast<ui::Widget*>(service_), static_cast<ui::Widget*>(check_)})
            wd->setVisibility(ui::Visibility::Collapsed);
        for (auto* v : verifyRow_) v->setVisibility(ui::Visibility::Collapsed);
        if (role == Role::None) layoutNone(b);
        else if (role == Role::Pending) layoutPending(b);
        else layoutList(b, role == Role::Plus);
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Rect b = bounds();
        r.fillRect(b, c.windowBg);
        const Role role = shownRole_;
        if (role == Role::None) paintNone(ctx);
        else if (role == Role::Pending) paintPending(ctx);
        else paintList(ctx, role == Role::Plus);
        (void)c;
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
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
                p.name = w.key().name.empty() ? std::string("admin+") : w.key().name;
                p.pc = thisPc();
                p.me = true;
            } else if (fetched_.haveView && !fetched_.view.editors.empty()) {
                const auto& e = fetched_.view.editors.front();
                p.key = e.admin.key;
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
        int color = 1;
        std::vector<AdminRow> retired;
        for (const auto& a : admins) {
            AdminRow r;
            r.kind = AdminRow::Kind::Admin;
            r.key = a.key;
            r.name = a.name;
            r.pc = a.pc;
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
            (a.active() ? rows_ : retired).push_back(std::move(r));
        }
        // Les demandes recues (l'admin+).
        if (role == Role::Plus && fetched_.haveRequests)
            for (const auto& q : fetched_.requests) {
                AdminRow r;
                r.kind = AdminRow::Kind::Pending;
                r.key = q.key;
                r.name = q.name;
                r.email = q.email;
                r.pc = q.pc;
                r.software = q.software;
                r.received = q.received;
                r.fingerprint = q.fingerprint;
                r.mismatch = q.fingerprintMismatch;
                r.color = color++;
                rows_.push_back(std::move(r));
            }
        for (auto& r : retired) rows_.push_back(std::move(r));
        // Les chiffres du journal (l'admin+ : il le releve et le garde).
        if (role == Role::Plus) {
            const JournalView v = w.journalLines();
            for (auto& r : rows_) {
                for (const auto& j : v.rows) {
                    if (j.line.key != r.key) continue;
                    const Seconds t = j.line.parsed.when ? j.line.parsed.when : j.line.received;
                    if (t >= now - 30 * 86400) ++r.actions30;
                    if (j.line.parsed.action == "licence_creer") ++r.licences;
                    if (t >= r.lastAction) {
                        r.lastAction = t;
                        r.lastSummary = j.line.parsed.object.empty() ? j.line.parsed.summary : j.line.parsed.object;
                    }
                }
            }
        }
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
        // Le tableau des scripts.
        std::vector<std::vector<std::string>> t;
        for (const auto& r : rows_) t.push_back({r.name, stateText(r)});
        syncing_ = true;
        table_->setModel(std::make_shared<TextRows>(std::vector<std::string>{"Admin", "\xC3\x89tat"}, std::move(t)));
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].key == selectedKey_) table_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
        syncing_ = false;
        // Une ligne << Verifier et accepter... >> par demande.
        std::size_t pending = 0;
        for (const auto& r : rows_) pending += r.kind == AdminRow::Kind::Pending ? 1 : 0;
        while (verifyRow_.size() < pending) {
            const std::size_t n = verifyRow_.size() + 1;
            ui::Button& v = button(*this, "V\xC3\xA9rifier et accepter\xE2\x80\xA6", "admins.verifier." + std::to_string(n), ui::Button::Style::Flat);
            v.setVisibility(ui::Visibility::Collapsed);
            links_ += v.clicked->connect([this, n] {
                std::size_t k = 0;
                for (const auto& r : rows_)
                    if (r.kind == AdminRow::Kind::Pending && ++k == n) {
                        selectedKey_ = r.key;
                        openAdd(r.key);
                        return;
                    }
            });
            verifyRow_.push_back(&v);
        }
        scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
        showButtons();
        invalidateLayout();
        invalidate();
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
        const AdminRow* s = selected();
        const bool busy = host_.busy && host_.busy();
        const bool ready = ws().service().ready();
        journal_->setEnabled(s && s->key.size() == 64);
        rightsOf_->setEnabled(s && s->kind == AdminRow::Kind::Admin && s->active());
        retire_->setEnabled(s && s->kind == AdminRow::Kind::Admin && s->active() && ready && !busy);
        retire_->setTooltip(!ready ? std::string("Il faut le service des licences (Service en ligne) : la nouvelle liste y est publi\xC3\xA9" "e.")
                                   : "Son certificat est r\xC3\xA9voqu\xC3\xA9. Ce qu'il a sign\xC3\xA9 reste valide, sauf si tu choisis de r\xC3\xA9voquer aussi ses licences.");
        verify_->setEnabled(s && s->kind == AdminRow::Kind::Pending && !busy);
        refuseReq_->setEnabled(s && s->kind == AdminRow::Kind::Pending && !busy);
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

    void askRetire() {
        const AdminRow* s = selected();
        if (!s || s->kind != AdminRow::Kind::Admin || !s->active()) return;
        auto alive = alive_;
        host_.openDialog(std::make_unique<RetireDialog>(host_, s->key, s->name, [this, alive] {
            if (*alive) fetch(false);
        }));
    }

    void refuseSelected() {
        const AdminRow* s = selected();
        if (!s || s->kind != AdminRow::Kind::Pending) return;
        const Workspace* w = &ws();
        const std::string key = s->key;
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

    // ------------------------------------------------------------------ la liste
    void layoutList(const Rect& b, bool plus) {
        const bool compact = b.h < 680.f;
        titleY_ = b.y + (compact ? 18.f : 26.f);
        float bx = x0_ + w_;
        if (plus) {
            for (ui::Button* btn : {add_, rightsPage_, reload_}) {
                const float bw = buttonWidth(*btn, 100.f);
                bx -= bw;
                btn->setBounds({bx, titleY_ + 4.f, bw, 38.f});
                btn->setVisibility(ui::Visibility::Visible);
                bx -= 10.f;
            }
        } else {
            const float bw = buttonWidth(*reload_, 100.f);
            reload_->setBounds({bx - bw, titleY_ + 4.f, bw, 38.f});
            reload_->setVisibility(ui::Visibility::Visible);
        }
        const float top = titleY_ + (compact ? 74.f : 84.f), bottom = b.bottom() - 18.f;
        const float rightW = std::clamp(w_ * 0.32f, 330.f, 420.f);
        const float leftW = std::max(360.f, w_ - rightW - 16.f);
        listRect_ = {x0_, top, leftW, std::max(160.f, bottom - top)};
        cardRect_ = {x0_ + leftW + 16.f, top, rightW, std::max(160.f, bottom - top)};
        rowH_ = plus ? (compact ? 56.f : 64.f) : 48.f;
        const float headH = plus ? 40.f : 50.f;
        const float footH = plus ? 44.f : 64.f;
        const float avail = listRect_.h - headH - footH;
        visibleRows_ = std::max(1, static_cast<int>(avail / rowH_));
        scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(rows_.size()) - visibleRows_));
        const int shown = std::min(visibleRows_, static_cast<int>(rows_.size()) - scroll_);
        const float listH = headH + static_cast<float>(std::max(0, shown)) * rowH_ + footH;
        listRect_.h = std::min(listRect_.h, std::max(listH, 160.f));
        rowsRect_ = {listRect_.x, listRect_.y + headH, listRect_.w, static_cast<float>(std::max(0, shown)) * rowH_};
        // Les colonnes (l'admin+) : Admin, PC, Sa cle, Ajoute le, Derniere action, Etat.
        const float cw = listRect_.w - 32.f;
        const float frac[6] = {0.25f, 0.17f, 0.14f, 0.13f, 0.16f, 0.15f};
        float cx = listRect_.x + 16.f;
        for (int i = 0; i < 6; ++i) {
            colX_[i] = cx;
            colW_[i] = cw * frac[i];
            cx += colW_[i];
        }
        // << Verifier et accepter... >> sous la pastille de chaque demande montree.
        if (plus) {
            std::size_t n = 0;
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                if (rows_[i].kind != AdminRow::Kind::Pending) continue;
                ui::Button* v = n < verifyRow_.size() ? verifyRow_[n] : nullptr;
                ++n;
                const int vi = static_cast<int>(i) - scroll_;
                if (!v || vi < 0 || vi >= shown) continue;
                const float ry = rowsRect_.y + static_cast<float>(vi) * rowH_;
                v->setBounds({colX_[5] - 6.f, ry + rowH_ * 0.5f + 2.f, std::min(colW_[5] + 10.f, buttonWidth(*v, 120.f)), 26.f});
                v->setVisibility(ui::Visibility::Visible);
            }
        }
        // La fiche.
        const AdminRow* s = selected();
        if (!plus || !s) return;
        const float fx = cardRect_.x + 20.f, fw = cardRect_.w - 40.f;
        float y = cardRect_.y + 92.f;
        if (s->kind == AdminRow::Kind::Pending) {
            const float bw1 = buttonWidth(*verify_, 150.f);
            verify_->setBounds({fx, y, std::min(fw, bw1), 36.f});
            verify_->setVisibility(ui::Visibility::Visible);
            refuseReq_->setBounds({fx, cardRect_.bottom() - 60.f, fw, 38.f});
            refuseReq_->setVisibility(ui::Visibility::Visible);
            return;
        }
        if (s->key.size() == 64) {
            const float bw1 = buttonWidth(*journal_, 120.f);
            journal_->setBounds({fx, y, bw1, 34.f});
            journal_->setVisibility(ui::Visibility::Visible);
            if (s->kind == AdminRow::Kind::Admin && s->active()) {
                const float bw2 = buttonWidth(*rightsOf_, 110.f);
                rightsOf_->setBounds({fx + bw1 + 8.f, y, std::min(bw2, fw - bw1 - 8.f), 34.f});
                rightsOf_->setVisibility(ui::Visibility::Visible);
            }
        }
        if (s->kind == AdminRow::Kind::Admin && s->active()) {
            retire_->setBounds({fx, cardRect_.bottom() - 78.f, fw, 38.f});
            retire_->setVisibility(ui::Visibility::Visible);
        }
    }

    void paintList(const ui::PaintContext& ctx, bool plus) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Workspace& w = ws();
        const Seconds now = w.now();
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
        lead += plus ? std::string(" \xC2\xB7 tu es l'admin+") : w.role() == Role::Retired ? std::string(" \xC2\xB7 tu as \xC3\xA9t\xC3\xA9 retir\xC3\xA9") : std::string(" \xC2\xB7 tu es admin");
        if (fetching_) lead += " \xC2\xB7 relev\xC3\xA9 en cours\xE2\x80\xA6";
        else if (!fetched_.problem.empty()) lead += " \xC2\xB7 service : " + fetched_.problem;
        else if (!w.service().ready()) lead += " \xC2\xB7 service pas r\xC3\xA9gl\xC3\xA9 : la liste de ce poste";
        r.drawText({x0_, titleY_ + 40.f}, fit(r, lead, kF14, std::max(100.f, reload_->bounds().x - 16.f - x0_)), kF14,
                   !fetched_.problem.empty() ? c.warning : c.textMuted);
        if (!plus) {
            const float tw = r.measure("Administrateurs", gfx::semibold(kF28)).width;
            (void)pill(r, x0_ + tw + 14.f, titleY_ + 8.f, "lecture seule", gfx::semibold(kF12), c.textMuted, c.rowAltBg, 24.f);
        }
        cardBox(r, th, listRect_);
        cardBox(r, th, cardRect_);
        if (plus) paintTable(ctx, now);
        else paintReadOnly(ctx);
        // La fiche (l'admin+) ou tes droits (un admin).
        if (plus) paintDetail(ctx, now);
        else paintMine(ctx);
    }

    void paintTable(const ui::PaintContext& ctx, Seconds now) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const FontId fh = gfx::semibold(kF11);
        const char* heads[6] = {"ADMIN", "PC", "SA CL\xC3\x89", "AJOUT\xC3\x89 LE", "DERNI\xC3\x88RE ACTION", "\xC3\x89TAT"};
        const float hy = listRect_.y + 14.f;
        for (int i = 0; i < 6; ++i) r.drawText({colX_[i] + (i == 0 ? 4.f : 0.f), hy}, heads[i], fh, c.textMuted);
        r.fillRect({listRect_.x + 1.f, rowsRect_.y - 1.f, listRect_.w - 2.f, 1.f}, c.gridLine);
        r.pushClip(rowsRect_);
        for (int vi = 0; vi < visibleRows_; ++vi) {
            const std::size_t i = static_cast<std::size_t>(vi + scroll_);
            if (i >= rows_.size()) break;
            const AdminRow& a = rows_[i];
            const float y = rowsRect_.y + static_cast<float>(vi) * rowH_;
            const Rect row{listRect_.x + 1.f, y, listRect_.w - 2.f, rowH_};
            if (a.key == selectedKey_) {
                r.fillRect(row, c.accent.withAlpha(16));
                r.fillRect({row.x, row.y, 3.f, row.h}, c.accent);
            }
            r.fillRect({row.x, row.bottom() - 1.f, row.w, 1.f}, c.gridLine);
            const float l1 = y + rowH_ * 0.5f - 18.f, l2 = y + rowH_ * 0.5f + 2.f;
            const bool gone = a.retired != 0;
            // Admin : l'avatar, le nom, le role ; dessous : toi, son profil, son courriel.
            avatar(r, {colX_[0] + 4.f, y + (rowH_ - 32.f) * 0.5f, 32.f, 32.f}, a.name, avatarColor(th, gone ? -1 : a.color), 9.f);
            const float nx = colX_[0] + 46.f;
            const FontId fn = gfx::semibold(kF14);
            const std::string nm = fit(r, a.name, fn, colW_[0] - 110.f);
            r.drawText({nx, l1}, nm, fn, gone ? c.textMuted : c.text);
            const float px = nx + r.measure(nm, fn).width + 8.f;
            if (a.kind == AdminRow::Kind::Plus) (void)pill(r, px, l1 - 2.f, "Admin+", gfx::semibold(kF11), c.accent, c.accent.withAlpha(22), 20.f);
            else (void)pill(r, px, l1 - 2.f, "Admin", gfx::semibold(kF11), c.textMuted, c.rowAltBg, 20.f);
            std::string sub = a.kind == AdminRow::Kind::Plus ? std::string(a.me ? "toi" : "la cl\xC3\xA9 de l'\xC3\xA9" "diteur")
                              : a.kind == AdminRow::Kind::Pending ? a.email
                              : a.profile.empty() ? std::string() : "profil " + a.profile;
            r.drawText({nx, l2}, fit(r, sub, kF12, colW_[0] - 50.f), kF12, c.textMuted);
            // PC
            r.drawText({colX_[1], l1}, fit(r, a.pc.empty() ? std::string("\xE2\x80\x94") : a.pc, th.font.mono, colW_[1] - 8.f), th.font.mono, gone ? c.textMuted : c.text);
            std::string pcSub = a.software.empty() ? std::string() : a.software;
            if (a.seen) pcSub = (pcSub.empty() ? std::string("vu ") : pcSub + " \xC2\xB7 vu ") + agoFr(now, a.seen);
            r.drawText({colX_[1], l2}, fit(r, pcSub, kF12, colW_[1] - 8.f), kF12, c.textMuted);
            // Sa cle
            const std::string fp = a.fingerprint.empty() ? std::string("\xE2\x80\x94") : shortFingerprint(a.fingerprint);
            r.drawText({colX_[2], l1}, fp, th.font.mono, gone ? c.textMuted : c.text);
            if (gone) r.fillRect({colX_[2], l1 + r.lineHeight(th.font.mono) * 0.55f, r.measure(fp, th.font.mono).width, 1.f}, c.error);
            std::string keySub;
            Color keyInk = c.textMuted;
            if (a.kind == AdminRow::Kind::Plus) keySub = "+ cl\xC3\xA9 de l'\xC3\xA9" "diteur";
            else if (a.kind == AdminRow::Kind::Pending) {
                keySub = a.mismatch ? "ne correspond pas" : "\xC3\xA0 v\xC3\xA9rifier";
                keyInk = a.mismatch ? c.error : rgb(0xC2410C);
            } else if (gone) keySub = "r\xC3\xA9voqu\xC3\xA9" "e";
            else keySub = "certifi\xC3\xA9" "e \xE2\x9C\x93";
            r.drawText({colX_[2], l2}, fit(r, keySub, kF12, colW_[2] - 6.f), kF12, keyInk);
            // Ajoute le
            const Seconds added = a.kind == AdminRow::Kind::Pending ? a.received : a.since;
            r.drawText({colX_[3], l1}, added ? dateFr(added) : std::string("\xE2\x80\x94"), kF13, gone ? c.textMuted : c.text);
            r.drawText({colX_[3], l2}, a.kind == AdminRow::Kind::Pending ? std::string("demande re\xC3\xA7ue") : a.kind == AdminRow::Kind::Plus ? std::string("la 1re") : std::string("par toi"),
                       kF12, c.textMuted);
            // Derniere action
            if (gone) {
                r.drawText({colX_[4], l1}, dateFr(a.retired), kF13, c.textMuted);
                r.drawText({colX_[4], l2}, "retir\xC3\xA9 par toi", kF12, c.textMuted);
            } else if (a.kind == AdminRow::Kind::Pending) {
                r.drawText({colX_[4], l1}, agoFr(now, a.received), kF13, c.text);
                r.drawText({colX_[4], l2}, "demande re\xC3\xA7ue", kF12, c.textMuted);
            } else {
                r.drawText({colX_[4], l1}, agoFr(now, a.lastAction), kF13, c.text);
                r.drawText({colX_[4], l2}, fit(r, a.lastSummary, kF12, colW_[4] - 8.f), kF12, c.textMuted);
            }
            // Etat
            const FontId fs = gfx::semibold(kF12);
            if (a.kind == AdminRow::Kind::Pending) {
                (void)pill(r, colX_[5] - 4.f, l1 - 4.f, "En attente d'acceptation", fs, rgb(0xC2410C), rgb(0xFFF4E5), 22.f);
            } else if (gone) {
                (void)pill(r, colX_[5] - 4.f, l1 - 4.f, "Retir\xC3\xA9", fs, c.error, c.error.withAlpha(22), 22.f);
                r.drawText({colX_[5] - 4.f, l2 + 2.f}, "certificat r\xC3\xA9voqu\xC3\xA9", kF12, c.textMuted);
            } else {
                (void)pill(r, colX_[5] - 4.f, l1 - 4.f, "Actif", fs, c.ok, c.ok.withAlpha(24), 22.f, true);
                if (!a.version.empty() && adm::compareSemver(a.version, kAppVersion) < 0)
                    r.drawText({colX_[5] - 4.f, l2 + 2.f}, fit(r, "Admin " + a.version + " \xC2\xB7 pas \xC3\xA0 jour", kF12, colW_[5]), kF12, rgb(0xC2410C));
            }
        }
        r.popClip();
        // Le pied : la regle d'un admin, un PC.
        const float fy = rowsRect_.bottom() + 12.f;
        ui::drawIcon(r, ui::Icon::Info, {listRect_.x + 18.f, fy + 1.f, 14.f, 14.f}, c.textMuted);
        std::string foot = "Un admin n'a qu'un PC. Nouveau PC ? Tu le retires, puis tu l'ajoutes \xC3\xA0 nouveau (une nouvelle cl\xC3\xA9).";
        if (scroll_ > 0 || scroll_ + visibleRows_ < static_cast<int>(rows_.size()))
            foot = std::to_string(scroll_ + 1) + "\xE2\x80\x93" + std::to_string(std::min(static_cast<int>(rows_.size()), scroll_ + visibleRows_)) + " sur " +
                   std::to_string(rows_.size()) + " (la molette) \xC2\xB7 " + foot;
        r.drawText({listRect_.x + 40.f, fy}, fit(r, foot, kF13, listRect_.w - 60.f), kF13, c.textMuted);
    }

    void paintDetail(const ui::PaintContext& ctx, Seconds now) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const AdminRow* s = selected();
        const float fx = cardRect_.x + 20.f, fw = cardRect_.w - 40.f;
        if (!s) {
            r.drawText({fx, cardRect_.y + 24.f}, "Choisis un admin dans la liste.", kF14, c.textMuted);
            return;
        }
        const bool gone = s->retired != 0;
        // Qui
        avatar(r, {fx, cardRect_.y + 20.f, 48.f, 48.f}, s->name, avatarColor(th, gone ? -1 : s->color), 12.f);
        const float nx = fx + 60.f;
        const FontId fn = gfx::semibold(FontId{18});
        const std::string nm = fit(r, s->name, fn, fw - 200.f);
        r.drawText({nx, cardRect_.y + 20.f}, nm, fn, c.text);
        float px = nx + r.measure(nm, fn).width + 10.f;
        const FontId fp = gfx::semibold(kF11);
        px += pill(r, px, cardRect_.y + 22.f, s->kind == AdminRow::Kind::Plus ? "Admin+" : "Admin", fp, s->kind == AdminRow::Kind::Plus ? c.accent : c.textMuted,
                   s->kind == AdminRow::Kind::Plus ? c.accent.withAlpha(22) : c.rowAltBg, 20.f) +
              6.f;
        if (s->kind == AdminRow::Kind::Pending) (void)pill(r, px, cardRect_.y + 22.f, "En attente", fp, rgb(0xC2410C), rgb(0xFFF4E5), 20.f, true);
        else if (gone) (void)pill(r, px, cardRect_.y + 22.f, "Retir\xC3\xA9", fp, c.error, c.error.withAlpha(22), 20.f, true);
        else (void)pill(r, px, cardRect_.y + 22.f, "Actif", fp, c.ok, c.ok.withAlpha(24), 20.f, true);
        std::string sub;
        if (s->kind == AdminRow::Kind::Plus) sub = "la cl\xC3\xA9 de l'\xC3\xA9" "diteur \xC2\xB7 " + s->pc;
        else if (s->kind == AdminRow::Kind::Pending) sub = s->email + " \xC2\xB7 demande re\xC3\xA7ue le " + dateTimeFr(s->received);
        else sub = (s->profile.empty() ? std::string() : "profil " + s->profile + " \xC2\xB7 ") + "admin depuis le " + dateFr(s->since);
        r.drawText({nx, cardRect_.y + 48.f}, fit(r, sub, kF13, fw - 60.f), kF13, c.textMuted);
        float y = cardRect_.y + 140.f;
        if (s->kind == AdminRow::Kind::Pending) {
            // Une demande : son empreinte, ce qu'il faut faire.
            r.drawText({fx, y}, "Sa cl\xC3\xA9", gfx::semibold(kF14), c.text);
            r.drawText({fx + 60.f, y + 1.f}, fit(r, "cr\xC3\xA9\xC3\xA9" "e sur " + s->pc, kF12, fw - 60.f), kF12, c.textMuted);
            paintFingerprint(r, th, {fx, y + 26.f, fw, 70.f}, s->fingerprint, 3, 31.f, gfx::semibold(kF14));
            y += 112.f;
            y += drawWrapped(r, fx, y, fw,
                             s->name + " a install\xC3\xA9 l'Admin et envoy\xC3\xA9 sa demande. Appelle-le, lisez son empreinte ensemble, puis accepte-le avec un profil : "
                                       "\xC2\xAB V\xC3\xA9rifier et accepter\xE2\x80\xA6 \xC2\xBB.",
                             kF13, c.text, 19.f, 6) +
                 10.f;
            if (s->mismatch)
                (void)drawWrapped(r, fx, y, fw, "L'empreinte rendue par le service n'est pas celle calcul\xC3\xA9" "e ici : refuse la demande.", kF13, c.error, 19.f, 3);
            return;
        }
        // Les chiffres (le journal : l'admin+ le releve).
        const Rect grid{cardRect_.x + 1.f, cardRect_.y + 138.f, cardRect_.w - 2.f, 62.f};
        r.fillRect({grid.x, grid.y, grid.w, 1.f}, c.gridLine);
        r.fillRect({grid.x, grid.bottom(), grid.w, 1.f}, c.gridLine);
        const float colW = grid.w / 3.f;
        for (int i = 1; i < 3; ++i) r.fillRect({grid.x + colW * static_cast<float>(i), grid.y, 1.f, grid.h}, c.gridLine);
        const std::string cells[3][2] = {{"Actions \xC2\xB7 30 j", std::to_string(s->actions30)},
                                         {"Licences sign\xC3\xA9" "es", std::to_string(s->licences)},
                                         {"Derni\xC3\xA8re", agoFr(now, s->lastAction)}};
        for (int i = 0; i < 3; ++i) {
            const float gx = grid.x + colW * static_cast<float>(i) + 16.f;
            r.drawText({gx, grid.y + 10.f}, cells[i][0], kF12, c.textMuted);
            r.drawText({gx, grid.y + 30.f}, fit(r, cells[i][1], gfx::semibold(kF15), colW - 24.f), gfx::semibold(kF15), c.text);
        }
        y = grid.bottom() + 16.f;
        // Sa cle
        r.drawText({fx, y}, "Sa cl\xC3\xA9", gfx::semibold(kF14), c.text);
        r.drawText({fx + 60.f, y + 1.f}, fit(r, s->kind == AdminRow::Kind::Plus ? "la cl\xC3\xA9 de l'\xC3\xA9" "diteur, sur ce PC" : "cr\xC3\xA9\xC3\xA9" "e sur " + s->pc, kF12, fw - 60.f), kF12,
                   c.textMuted);
        y += 24.f;
        paintFingerprint(r, th, {fx, y, fw, 26.f}, s->fingerprint, 6, 26.f, gfx::semibold(kF12));
        y += 34.f;
        const std::string cert = s->kind == AdminRow::Kind::Plus ? std::string("Elle certifie les admins ; elle ne quitte jamais ce PC.")
                                 : gone ? "Certificat r\xC3\xA9voqu\xC3\xA9 le " + dateFr(s->retired) + " : ses nouvelles signatures sont refus\xC3\xA9" "es."
                                        : "Certificat d'admin sign\xC3\xA9 par la cl\xC3\xA9 de l'\xC3\xA9" "diteur le " + dateFr(s->since);
        y += drawWrapped(r, fx, y, fw, cert, kF12, gone ? c.error : c.textMuted, 17.f, 2) + 12.f;
        // Ses droits
        r.drawText({fx, y}, "Ses droits", gfx::semibold(kF14), c.text);
        const std::string rs = s->kind == AdminRow::Kind::Plus ? std::string("tous, et ceux de l'admin+")
                                                                : std::to_string(s->rights.size()) + " sur 13" + (s->profile.empty() ? std::string() : " \xC2\xB7 " + s->profile);
        r.drawText({fx + 84.f, y + 1.f}, fit(r, rs, kF12, fw - 84.f), kF12, c.textMuted);
        y += 26.f;
        const float bottom = retire_->visible() ? retire_->bounds().y - 10.f : cardRect_.bottom() - 14.f;
        r.pushClip({cardRect_.x, y, cardRect_.w, std::max(0.f, bottom - y)});
        (void)paintRights(r, th, fx, y, fw, s->rights, s->kind == AdminRow::Kind::Plus);
        r.popClip();
        if (retire_->visible())
            (void)drawWrapped(r, fx, retire_->bounds().bottom() + 6.f, fw, "Son certificat est r\xC3\xA9voqu\xC3\xA9 ; ses licences restent valides, sauf si tu choisis de les r\xC3\xA9voquer aussi.",
                              kF12, c.textMuted, 16.f, 2);
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
        noneA_ = {x0_, top, cw, 330.f};
        noneB_ = {x0_ + cw + gap, top, cw, 430.f};
        const float bx = noneA_.x + 24.f;
        createKey_->setBounds({bx, noneA_.bottom() - 62.f, buttonWidth(*createKey_, 200.f), 38.f});
        restore_->setBounds({createKey_->bounds().right() + 10.f, noneA_.bottom() - 62.f, buttonWidth(*restore_, 160.f), 38.f});
        show(createKey_, true);
        show(restore_, true);
        layoutForm(noneB_);
    }
    void layoutForm(const Rect& card) {
        const float fx = card.x + 24.f, fw = card.w - 48.f;
        reqName_->setBounds({fx, card.y + 178.f, fw, 34.f});
        reqEmail_->setBounds({fx, card.y + 246.f, fw, 34.f});
        send_->setBounds({fx, card.bottom() - 62.f, buttonWidth(*send_, 180.f), 38.f});
        show(reqName_, true);
        show(reqEmail_, true);
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
        r.drawText({x0_, titleY_ + 40.f}, fit(r, "Ce PC n'a encore aucune cl\xC3\xA9 : il devient l'admin+ (le premier PC), ou un admin que l'admin+ accepte.", kF14, w_), kF14,
                   c.textMuted);
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
        // B : devenir admin
        cardBox(r, th, noneB_);
        paintForm(ctx, noneB_, false);
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
        (void)drawWrapped(r, fx, card.y + 82.f, fw,
                          "L'admin+ t'a fait installer l'Admin ? Donne ton nom et ton courriel : ta demande part au service, l'admin+ t'appelle et tu lui lis ton "
                          "empreinte.",
                          kF14, c.text, 21.f, 3);
        r.drawText({fx, reqName_->bounds().y - 20.f}, "Pr\xC3\xA9nom et nom", gfx::semibold(kF13), c.text);
        r.drawText({fx, reqEmail_->bounds().y - 20.f}, "Courriel", gfx::semibold(kF13), c.text);
        if (!ws().service().configured)
            (void)drawWrapped(r, fx, reqEmail_->bounds().bottom() + 14.f, fw,
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
        if (lastState_ == "refusee" || lastState_ == "aucune") {
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
        r.drawText({fx, y}, "L'admin+ doit accepter ta cl\xC3\xA9", gfx::semibold(FontId{20}), c.text);
        y += 32.f;
        r.drawText({fx, y}, "Il va t'appeler. Lis-lui ton empreinte :", kF14, c.textMuted);
        y += 30.f;
        paintFingerprint(r, th, {fx, y, fw, 132.f}, w.adminKey().fingerprint, 3, 62.f, gfx::semibold(FontId{26}));
        y += 146.f;
        r.drawText({fx, y}, "En attendant, l'Admin ne peut rien signer.", kF14, c.text);
        y += 26.f;
        std::string st = "L'Admin regarde ta demande seul, toutes les 15 s.";
        Color ink = c.textMuted;
        if (lastState_ == "refusee") {
            st = "L'admin+ a refus\xC3\xA9 ta demande. Parle-lui, puis envoie-la de nouveau.";
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
        if (lastState_ == "refusee" || lastState_ == "aucune") {
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
    // les widgets
    ui::Button*            add_{nullptr};
    ui::Button*            rightsPage_{nullptr};
    ui::Button*            reload_{nullptr};
    ui::Button*            journal_{nullptr};
    ui::Button*            rightsOf_{nullptr};
    ui::Button*            retire_{nullptr};
    ui::Button*            verify_{nullptr};
    ui::Button*            refuseReq_{nullptr};
    ui::Button*            createKey_{nullptr};
    ui::Button*            restore_{nullptr};
    ui::InputText*         reqName_{nullptr};
    ui::InputText*         reqEmail_{nullptr};
    ui::Button*            send_{nullptr};
    ui::Button*            service_{nullptr};
    ui::Button*            check_{nullptr};
    std::vector<ui::Button*> verifyRow_;
    ui::TableView*         table_{nullptr};
    // la mise en page
    float                  x0_{0.f}, w_{0.f}, titleY_{0.f}, rowH_{64.f};
    int                    visibleRows_{1}, scroll_{0};
    float                  colX_[6]{}, colW_[6]{};
    Rect                   listRect_{}, cardRect_{}, rowsRect_{}, noneA_{}, noneB_{}, pendingCard_{};
    std::shared_ptr<bool>  alive_ = std::make_shared<bool>(true);
    core::ConnectionScope  links_;
};

} // namespace

PagePtr makeAdminsPage(ShellHost host) { return std::make_unique<AdminsPage>(std::move(host)); }

menu::MenuPtr makeAddAdminDialog(ShellHost host, std::string requestKey, std::function<void()> done) {
    return std::make_unique<AddAdminDialog>(std::move(host), std::move(requestKey), std::move(done));
}

} // namespace license_admin::gui
