// =============================================================================
//  license_admin/gui/PageAdminRequests.cpp - 1.47.0 : LA PAGE << Demandes d'admin >>
// -----------------------------------------------------------------------------
//  Les demandes pour devenir admin (docs/ADMINS.md par. 4) en LISTE, comme les
//  demandes de licence (Demandes en ligne) : l'admin+ seul (lui seul signe la
//  liste des admins ; Shell::plusOnly). Relevees du service a l'arrivee
//  (GET /admin/admins/demandes?toutes=1 : en attente, et les closes des 7
//  derniers jours), et par << Relever >>. La table : recue le, nom, courriel,
//  PC, logiciel, l'empreinte courte, l'invitation qui va avec
//  (invitations-admins.json : << invite le ... >> / << sans invitation >>),
//  l'etat. La barre de filtres, le clic droit dans l'ordre de la regle et le
//  depliage (l'empreinte en grand, l'invitation) : ListKit.hpp.
//
//  PLUSIEURS D'UN COUP : choisir plusieurs demandes -> << Accepter 3
//  demandes... >> ouvre UNE fenetre qui les liste, chacune avec son empreinte
//  en 6 groupes, sa case << J'ai verifie l'empreinte (lue au telephone) >>
//  (obligatoire : sans elle, rien n'est signe) et son profil (celui de
//  l'invitation, sinon Admin LV1). Confirmer : la cle de l'editeur signe UNE
//  nouvelle liste des admins (numero + 1) avec tous (Workspace::acceptAdmins),
//  la cle de synchro est deposee pour chacun, chaque demande est close (le
//  service : PUT /admin/admins), une ligne au journal par nouvel admin.
//  << Refuser 2 demandes... >> : une raison facultative (au journal des
//  admins ; le demandeur voit << refusee >>).
// =============================================================================
#include "AdminsKit.hpp"
#include "ListKit.hpp"
#include "Page.hpp"

#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/ScrollBar.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace {

namespace adm = licensing::admins;
using gfx::Color;
using gfx::FontId;
using gfx::Rect;
using RowIndex = ui::RowIndex;
using kit::fit;

constexpr float kPad = 16.f;
constexpr float kButtonH = 32.f;
constexpr float kGap = 8.f;
constexpr float kLineH = 20.f;
constexpr const char* kBusy = "Un travail est en cours : attends qu'il finisse.";
constexpr const char* kWaitingLabel = "En attente";

std::string stateLabel(const AdminRequest& q) {
    if (q.state == "acceptee") return "Accept\xC3\xA9" "e";
    if (q.state == "refusee") return "Refus\xC3\xA9" "e";
    return kWaitingLabel;
}
ui::Tone stateTone(const AdminRequest& q) {
    if (q.fingerprintMismatch) return ui::Tone::Error;
    if (q.state == "acceptee") return ui::Tone::Ok;
    if (q.state == "refusee") return ui::Tone::Muted;
    return ui::Tone::Warning;
}
std::string padKey(long long v) {
    char b[32];
    std::snprintf(b, sizeof b, "%012lld", v);
    return b;
}
std::string invitationText(const AdminInvitation* i) {
    if (!i) return "sans invitation";
    if (!i->id.empty()) {   // 1.49.0 : l'invitation par codes (son id exact)
        std::string t = i->id + " \xC2\xB7 invit\xC3\xA9 le " + dateFr(i->at);
        if (!i->profile.empty()) t += " (" + i->profile + ")";
        return t;
    }
    std::string t = "invit\xC3\xA9 le " + dateFr(i->at);
    if (!i->profile.empty()) t += " (" + i->profile + ")";
    return t;
}
std::string requestWho(const AdminRequest& q) { return q.name.empty() ? std::string("(sans nom)") : q.name; }

// L'empreinte en 6 groupes, une case chacun (une ligne).
void paintGroups(gfx::IRenderer& r, const ui::Theme& th, const Rect& box, const std::string& fingerprint, FontId f, bool bad = false) {
    const auto g = kit::groups(fingerprint);
    if (g.empty()) return;
    const float gap = 6.f;
    const float cw = (box.w - gap * static_cast<float>(g.size() - 1)) / static_cast<float>(g.size());
    for (std::size_t i = 0; i < g.size(); ++i) {
        const Rect cell{box.x + static_cast<float>(i) * (cw + gap), box.y, cw, box.h};
        ui::shapes::fillFramedRect(r, cell, th.color.panelBg, bad ? th.color.error : th.color.borderStrong, 7.f);
        r.drawText({cell.x + (cell.w - r.measure(g[i], f).width) * 0.5f, cell.y + (cell.h - r.lineHeight(f)) * 0.5f}, g[i], f, bad ? th.color.error : th.color.text);
    }
}

// Le profil propose pour une demande : celui de son invitation (s'il existe encore), sinon << Admin LV1 >>, sinon aucun.
std::string defaultProfile(const AdminInvitation* inv, const std::vector<adm::Profile>& profiles) {
    const auto has = [&](const std::string& n) { return std::any_of(profiles.begin(), profiles.end(), [&](const adm::Profile& p) { return p.name == n; }); };
    if (inv && !inv->profile.empty() && has(inv->profile)) return inv->profile;
    if (has("Admin LV1")) return "Admin LV1";
    return {};
}

class Rows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(RowIndex, std::size_t)>;
    explicit Rows(std::vector<std::string> headers) : headers_(std::move(headers)) {}
    void set(std::vector<std::vector<std::string>> rows, std::vector<std::string> sortKeys, Style style) {
        rows_ = std::move(rows);
        keys_ = std::move(sortKeys);
        style_ = std::move(style);
        modelReset->emit();
    }
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string(); }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string();
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override { return style_ ? style_(r, c) : ui::CellStyle{}; }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t c) const override {
        if (c == 0 && a < keys_.size() && b < keys_.size()) return keys_[a] < keys_[b];
        return cellText(a, c) < cellText(b, c);
    }

private:
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<std::string>              keys_;
    Style                                 style_;
};

// =================================================================== accepter plusieurs demandes
class AcceptBody final : public ui::Widget {
public:
    AcceptBody(kit::Dialog& owner, ShellHost host, std::vector<AdminRequest> requests, std::vector<AdminInvitation> invitations,
               std::function<void(const OpResult&)> after)
        : ui::Widget("demandes-admins.lot.corps"), owner_(owner), host_(std::move(host)), invitations_(std::move(invitations)), after_(std::move(after)) {
        profiles_ = host_.workspace().profiles();
        listNumber_ = host_.workspace().adminList() ? host_.workspace().adminList()->number : 0;
        for (auto& q : requests) addRow(std::move(q));
        cancel_ = &kit::button(*this, "Annuler", "demandes-admins.lot.annuler");
        ok_ = &kit::button(*this, "Accepter", "demandes-admins.lot.ok", ui::Button::Style::Primary, ui::Icon::Ok);
        links_ += cancel_->clicked->connect([this] { owner_.close(); });
        links_ += ok_->clicked->connect([this] { confirm(); });
        sync();
    }

    // Les essais.
    [[nodiscard]] std::size_t count() const noexcept { return rows_.size(); }
    [[nodiscard]] std::string statusText() const { return status(); }

protected:
    void onLayout() override {
        const float n = static_cast<float>(rows_.size());
        frame_.place(*this, 900.f, 190.f + std::max(1.f, n) * kRow + 96.f);
        const auto& p = frame_.panel;
        list_ = {p.x + 1.f, p.y + 132.f, p.w - 2.f, std::max(80.f, p.bottom() - 96.f - (p.y + 132.f))};
        const float content = n * kRow;
        scroll_.place(list_, content, list_.h);
        offset_ = scroll_.clamp(offset_);
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            Row& r = rows_[i];
            const float y = list_.y + static_cast<float>(i) * kRow - offset_;
            r.top = y;
            const bool inside = y + kRow > list_.y + 1.f && y < list_.bottom() - 1.f;
            const auto put = [&](ui::Widget* w, const Rect& b) {
                w->setBounds(b);
                w->setVisibility(inside && b.y >= list_.y - 2.f && b.bottom() <= list_.bottom() + 2.f ? ui::Visibility::Visible : ui::Visibility::Hidden);
            };
            if (r.q.byCode()) put(r.code, {list_.x + 24.f, y + 96.f, 150.f, 32.f});   // 1.49.0 : les 6 chiffres
            else put(r.check, {list_.x + 24.f, y + 98.f, 330.f, 26.f});
            const float rw = kit::buttonWidth(*r.remove, 110.f);
            put(r.remove, {list_.right() - 24.f - rw, y + 96.f, rw, 30.f});
            put(r.profile, {r.remove->bounds().x - 12.f - 240.f, y + 96.f, 240.f, 30.f});
        }
        const float by = p.bottom() - 54.f;
        const float ow = kit::buttonWidth(*ok_, 170.f), cw = kit::buttonWidth(*cancel_, 100.f);
        ok_->setBounds({p.right() - 24.f - ow, by, ow, 34.f});
        cancel_->setBounds({ok_->bounds().x - 8.f - cw, by, cw, 34.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const auto& p = frame_.panel;
        const std::string title = rows_.size() == 1 ? "Accepter la demande de " + requestWho(rows_.front().q)
                                                     : "Accepter " + std::to_string(rows_.size()) + " demandes d'admin";
        const float top = frame_.paint(r, th, title, std::string("Une seule liste des admins sign\xC3\xA9" "e par la cl\xC3\xA9 de l'\xC3\xA9" "diteur") +
                                                         " (n\xC2\xB0 " + std::to_string(listNumber_ + 1) + ")" +
                                                         " avec chacun ; la cl\xC3\xA9 de synchro d\xC3\xA9pos\xC3\xA9" "e pour chacun.",
                                       ui::Icon::User);
        kit::drawWrapped(r, p.x + 24.f, top + 12.f, p.w - 48.f,
                         "Pour chaque demande : appelle la personne. Avec invitation, elle te dit les 6 chiffres de son Admin : tape-les. Sans, lis-lui les 6 "
                         "groupes de l'empreinte et coche \xC2\xAB J'ai v\xC3\xA9rifi\xC3\xA9 l'empreinte \xC2\xBB. Faux ou vide : rien n'est sign\xC3\xA9.",
                         FontId{13}, c.textMuted, 18.f, 2);
        r.pushClip(list_);
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const Row& row = rows_[i];
            const float y = row.top;
            if (y + kRow < list_.y || y > list_.bottom()) continue;
            if (i > 0) r.fillRect({list_.x + 16.f, y, list_.w - 32.f, 1.f}, c.border);
            const float x = list_.x + 24.f, w = list_.w - 48.f;
            const bool ok = rowReady(row);
            // la personne
            const FontId fn = gfx::semibold(FontId{15});
            r.drawText({x, y + 12.f}, fit(r, requestWho(row.q), fn, 260.f), fn, c.text);
            std::string sub = row.q.email.empty() ? std::string("(sans courriel)") : row.q.email;
            sub += " \xC2\xB7 PC " + (row.q.pc.empty() ? std::string("(sans nom)") : row.q.pc);
            sub += " \xC2\xB7 re\xC3\xA7ue " + kit::whenFr(host_.workspace().now(), row.q.received);
            r.drawText({x + 270.f, y + 14.f}, fit(r, sub, FontId{13}, w - 270.f), FontId{13}, c.textMuted);
            // l'empreinte, en grand
            const float ix = x + std::min(470.f, w * 0.56f) + 18.f;
            const AdminInvitation* inv = invitationFor(invitations_, row.q);
            if (row.q.byCode()) {
                // 1.49.0 : une demande avec invitation - les 6 chiffres que dit le futur admin (son Admin les montre)
                kit::drawWrapped(r, x, y + 44.f, std::min(470.f, w * 0.56f),
                                 "Son Admin lui montre 6 chiffres : il te les dit, tu les tapes. Faux : rien n'est sign\xC3\xA9.", FontId{13}, c.text, 18.f, 2);
                r.drawText({ix, y + 46.f},
                           fit(r, "Invitation : " + row.q.invitationId + (row.q.invitationProfile.empty() ? std::string() : " (" + row.q.invitationProfile + ")"),
                               FontId{13}, x + w - ix),
                           FontId{13}, c.text);
                if (row.wrong)
                    r.drawText({row.code->bounds().right() + 12.f, row.code->bounds().y + 8.f},
                               fit(r, "Ce code ne correspond pas \xC3\xA0 sa cl\xC3\xA9 : rien n'est sign\xC3\xA9.", FontId{13},
                                   std::max(10.f, row.profile->bounds().x - row.code->bounds().right() - 24.f)),
                               FontId{13}, c.error);
                r.drawText({ix, y + 66.f}, ok ? std::string("\xE2\x9C\x93 6 chiffres tap\xC3\xA9s") : std::string("6 chiffres \xC3\xA0 taper"), FontId{13},
                           ok ? c.ok : c.textMuted);
                continue;
            }
            paintGroups(r, th, {x, y + 42.f, std::min(470.f, w * 0.56f), 42.f}, row.q.fingerprint, gfx::semibold(FontId{19}), row.q.fingerprintMismatch);
            r.drawText({ix, y + 46.f}, fit(r, inv ? "Invitation : " + invitationText(inv) : std::string("Sans invitation : v\xC3\xA9rifie bien qui c'est."), FontId{13}, x + w - ix),
                       FontId{13}, inv ? c.text : c.warning);
            if (row.q.fingerprintMismatch)
                r.drawText({ix, y + 66.f}, fit(r, "Le service rend une autre empreinte : ne l'accepte pas.", FontId{13}, x + w - ix), FontId{13}, c.error);
            else
                r.drawText({ix, y + 66.f}, ok ? std::string("\xE2\x9C\x93 v\xC3\xA9rifi\xC3\xA9" "e") : std::string("pas encore v\xC3\xA9rifi\xC3\xA9" "e"), FontId{13}, ok ? c.ok : c.textMuted);
        }
        r.popClip();
        r.drawText({p.x + 24.f, p.bottom() - 46.f}, fit(r, status(), FontId{13}, cancel_->bounds().x - 16.f - (p.x + 24.f)), FontId{13},
                   allChecked() ? c.ok : c.warning);
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (scroll_.needed()) scroll_.paint(ctx, offset_);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (frame_.hitClose(ev)) {
            owner_.close();
            return ui::EventResult::Consumed;
        }
        bool changed = false;
        if (scroll_.handle(*this, ev, offset_, &changed)) {
            if (changed) invalidateLayout();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    static constexpr float kRow = 140.f;
    struct Row {
        AdminRequest   q;
        ui::Checkbox*  check{nullptr};
        ui::InputText* code{nullptr};   // 1.49.0 : les 6 chiffres (une demande avec invitation)
        bool           wrong{false};
        ui::DropDown*  profile{nullptr};
        ui::Button*    remove{nullptr};
        float          top{0.f};
    };

    void addRow(AdminRequest q) {
        const std::size_t i = rows_.size();
        Row row;
        const std::string n = std::to_string(i);
        row.check = &kit::adopt(*this, std::make_unique<ui::Checkbox>("J'ai v\xC3\xA9rifi\xC3\xA9 l'empreinte (lue au t\xC3\xA9l\xC3\xA9phone)", "demandes-admins.lot.verifie." + n));
        row.code = &kit::adopt(*this, std::make_unique<ui::InputText>("demandes-admins.lot.code." + n));
        row.code->setPlaceholder("123 456");
        row.code->setMaxLength(9);
        row.code->setVisibility(ui::Visibility::Collapsed);
        if (q.byCode()) row.check->setVisibility(ui::Visibility::Collapsed);
        row.profile = &kit::adopt(*this, std::make_unique<ui::DropDown>("demandes-admins.lot.profil." + n));
        std::vector<ui::DropDown::Item> items;
        items.push_back({"Aucun droit (\xC3\xA0 r\xC3\xA9gler ensuite)", ""});
        for (const auto& p : profiles_) items.push_back({p.name + " \xC2\xB7 " + std::to_string(p.rights.size()) + "/13", p.name});
        row.profile->setItems(items);
        const std::string want = defaultProfile(invitationFor(invitations_, q), profiles_);
        int sel = 0;
        for (std::size_t k = 0; k < items.size(); ++k)
            if (items[k].value == want) sel = static_cast<int>(k);
        row.profile->setSelectedIndex(sel);
        row.remove = &kit::button(*this, "Retirer du lot", "demandes-admins.lot.retirer." + n, ui::Button::Style::Flat);
        row.remove->setTooltip("Ne pas accepter cette demande maintenant (elle reste en attente).");
        if (q.fingerprintMismatch) row.check->setEnabled(false);
        row.q = std::move(q);
        const std::string key = row.q.key;
        links_ += row.check->stateChanged->connect([this](ui::Checkbox::State) { sync(); });
        links_ += row.code->textChanged->connect([this, key](const std::string&) {
            for (auto& x : rows_)
                if (x.q.key == key) x.wrong = false;
            sync();
        });
        links_ += row.remove->clicked->connect([this, key] { removeRow(key); });
        rows_.push_back(row);
    }

    void removeRow(const std::string& key) {
        for (auto it = rows_.begin(); it != rows_.end(); ++it)
            if (it->q.key == key) {
                for (ui::Widget* w : {static_cast<ui::Widget*>(it->check), static_cast<ui::Widget*>(it->code), static_cast<ui::Widget*>(it->profile),
                                      static_cast<ui::Widget*>(it->remove)})
                    w->setVisibility(ui::Visibility::Collapsed);   // gardes (leurs connexions), plus montres
                gone_.push_back(*it);
                rows_.erase(it);
                break;
            }
        if (rows_.empty()) {
            owner_.close();
            return;
        }
        sync();
        invalidateLayout();
    }

    // 1.49.0 : une ligne prete - l'empreinte cochee (sans invitation), ou 6 chiffres tapes (avec ; verifies a la confirmation)
    [[nodiscard]] static bool rowReady(const Row& r) {
        if (!r.q.byCode()) return r.check->isChecked();
        std::size_t d = 0;
        for (const char ch : r.code->text()) d += (ch >= '0' && ch <= '9') ? 1 : 0;
        return d == 6 && !r.wrong;
    }
    [[nodiscard]] std::size_t checkedCount() const {
        std::size_t n = 0;
        for (const auto& r : rows_) n += rowReady(r) ? 1 : 0;
        return n;
    }
    [[nodiscard]] bool allChecked() const { return !rows_.empty() && checkedCount() == rows_.size(); }
    [[nodiscard]] std::string status() const {
        if (rows_.empty()) return "Aucune demande dans le lot.";
        const std::size_t n = checkedCount();
        if (n == rows_.size())
            return rows_.size() == 1 ? std::string("Empreinte v\xC3\xA9rifi\xC3\xA9" "e : pr\xC3\xAAt \xC3\xA0 signer.")
                                     : "Les " + std::to_string(n) + " empreintes sont v\xC3\xA9rifi\xC3\xA9" "es : pr\xC3\xAAt \xC3\xA0 signer.";
        for (const auto& r : rows_)
            if (r.wrong) return "Le code de " + requestWho(r.q) + " ne correspond pas \xC3\xA0 sa cl\xC3\xA9 : rien n'est sign\xC3\xA9. Fais-lui relire ses 6 chiffres.";
        return std::to_string(n) + " sur " + std::to_string(rows_.size()) + " v\xC3\xA9rifi\xC3\xA9" "e" + (n > 1 ? "s" : "") +
               " : coche chaque empreinte, ou tape les 6 chiffres (ou retire la demande du lot).";
    }

    void sync() {
        ok_->setText(rows_.size() == 1 ? "Accepter " + requestWho(rows_.front().q) : "Accepter les " + std::to_string(rows_.size()));
        const bool busy = host_.busy && host_.busy();
        ok_->setEnabled(allChecked() && !busy);
        ok_->setTooltip(allChecked() ? std::string("La cl\xC3\xA9 de l'\xC3\xA9" "diteur signe une liste des admins avec chacun.")
                                     : std::string("Coche \xC2\xAB J'ai v\xC3\xA9rifi\xC3\xA9 l'empreinte \xC2\xBB, ou tape les 6 chiffres, pour chaque demande."));
        invalidateLayout();
        invalidate();
    }

    void confirm() {
        if (done_ || !allChecked()) return;
        if (host_.busy && host_.busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return;
        }
        // 1.49.0 : les 6 chiffres de chaque demande avec invitation, verifies ici (de sa cle) : un seul faux, rien n'est signe
        bool wrong = false;
        for (auto& r : rows_)
            if (r.q.byCode() && !adm::confirmationMatches(r.q.key, r.code->text())) r.wrong = wrong = true;
        if (wrong) {
            sync();
            return;
        }
        std::vector<AdminAccept> items;
        for (const auto& r : rows_) {
            const ui::DropDown::Item* it = r.profile->selectedItem();
            items.push_back({r.q.key, it ? it->value : std::string(), !r.q.byCode() && r.check->isChecked(), r.q.byCode() ? r.code->text() : std::string()});
        }
        done_ = true;
        const Workspace* w = &host_.workspace();
        const std::string label = items.size() == 1 ? "Service : accepter " + requestWho(rows_.front().q) : "Service : accepter " + std::to_string(items.size()) + " admins";
        auto after = after_;
        ShellHost host = host_;
        owner_.close();
        const bool started = host.runSlow && host.runSlow(label, [w, items] { return w->acceptAdmins(items); }, [after](const OpResult& r) {
                                 if (after) after(r);
                             });
        if (!started && host.flash) host.flash(kBusy, Tone::Warning);
    }

    kit::Dialog&                         owner_;
    ShellHost                            host_;
    std::vector<AdminInvitation>         invitations_;
    std::vector<adm::Profile>            profiles_;
    std::function<void(const OpResult&)> after_;
    int                                  listNumber_{0};
    std::vector<Row>                     rows_, gone_;
    ui::Button*                          cancel_{nullptr};
    ui::Button*                          ok_{nullptr};
    kit::Frame                           frame_;
    Rect                                 list_{};
    ui::ScrollArea                       scroll_;
    float                                offset_{0.f};
    bool                                 done_{false};
    core::ConnectionScope                links_;
};

class AcceptDialog final : public kit::Dialog {
public:
    AcceptDialog(ShellHost host, std::vector<AdminRequest> requests, std::vector<AdminInvitation> invitations, std::function<void(const OpResult&)> after)
        : kit::Dialog("dialog.demandes-admins.accepter"), host_(std::move(host)), requests_(std::move(requests)), invitations_(std::move(invitations)),
          after_(std::move(after)) {}
    [[nodiscard]] std::string title() const override { return "Accepter des demandes d'admin"; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<AcceptBody>(*this, host_, requests_, invitations_, after_));
        return core::ok();
    }

private:
    ShellHost                            host_;
    std::vector<AdminRequest>            requests_;
    std::vector<AdminInvitation>         invitations_;
    std::function<void(const OpResult&)> after_;
};

// =================================================================== refuser plusieurs demandes
class RefuseBody final : public ui::Widget {
public:
    RefuseBody(kit::Dialog& owner, ShellHost host, std::vector<AdminRequest> requests, std::function<void(const OpResult&)> after)
        : ui::Widget("demandes-admins.refus.corps"), owner_(owner), host_(std::move(host)), requests_(std::move(requests)), after_(std::move(after)) {
        reason_ = &kit::adopt(*this, std::make_unique<ui::InputText>("demandes-admins.refus.raison"));
        reason_->setPlaceholder("la raison (facultative), par exemple : personne inconnue");
        reason_->setMaxLength(300);
        cancel_ = &kit::button(*this, "Annuler", "demandes-admins.refus.annuler");
        ok_ = &kit::button(*this, requests_.size() == 1 ? std::string("Refuser") : "Refuser les " + std::to_string(requests_.size()), "demandes-admins.refus.ok",
                           ui::Button::Style::Danger);
        links_ += cancel_->clicked->connect([this] { owner_.close(); });
        links_ += ok_->clicked->connect([this] { confirm(); });
    }

protected:
    void onLayout() override {
        const float n = static_cast<float>(std::min<std::size_t>(requests_.size(), 8));
        frame_.place(*this, 620.f, 250.f + n * 22.f);
        const auto& p = frame_.panel;
        reason_->setBounds({p.x + 24.f, p.bottom() - 112.f, p.w - 48.f, 34.f});
        const float by = p.bottom() - 54.f;
        const float ow = kit::buttonWidth(*ok_, 130.f), cw = kit::buttonWidth(*cancel_, 100.f);
        ok_->setBounds({p.right() - 24.f - ow, by, ow, 34.f});
        cancel_->setBounds({ok_->bounds().x - 8.f - cw, by, cw, 34.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto& p = frame_.panel;
        const std::string title = requests_.size() == 1 ? "Refuser la demande de " + requestWho(requests_.front())
                                                         : "Refuser " + std::to_string(requests_.size()) + " demandes d'admin";
        float y = frame_.paint(r, ctx.theme, title, "Le demandeur voit \xC2\xAB refus\xC3\xA9" "e \xC2\xBB ; la raison va au journal des admins.") + 14.f;
        std::size_t shown = 0;
        for (const auto& q : requests_) {
            if (++shown > 8) {
                r.drawText({p.x + 24.f, y}, "\xE2\x80\xA6 et " + std::to_string(requests_.size() - 8) + " autres", FontId{13}, c.textMuted);
                y += 22.f;
                break;
            }
            r.drawText({p.x + 24.f, y}, fit(r, requestWho(q) + " \xC2\xB7 " + (q.email.empty() ? std::string("(sans courriel)") : q.email) + " \xC2\xB7 " +
                                                  kit::shortFingerprint(q.fingerprint) + "\xE2\x80\xA6",
                                              FontId{14}, p.w - 48.f),
                       FontId{14}, c.text);
            y += 22.f;
        }
        r.drawText({p.x + 24.f, p.bottom() - 136.f}, "Raison (facultative)", FontId{13}, c.textMuted);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (frame_.hitClose(ev)) {
            owner_.close();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    void confirm() {
        if (done_) return;
        if (host_.busy && host_.busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return;
        }
        done_ = true;
        std::vector<std::string> keys;
        for (const auto& q : requests_) keys.push_back(q.key);
        const std::string reason = reason_->text();
        const Workspace* w = &host_.workspace();
        auto after = after_;
        ShellHost host = host_;
        const std::string label = keys.size() == 1 ? std::string("Service : refuser une demande d'admin") : "Service : refuser " + std::to_string(keys.size()) + " demandes d'admin";
        owner_.close();
        const bool started = host.runSlow && host.runSlow(label, [w, keys, reason] { return w->refuseAdmins(keys, reason); }, [after](const OpResult& r) {
                                 if (after) after(r);
                             });
        if (!started && host.flash) host.flash(kBusy, Tone::Warning);
    }

    kit::Dialog&                         owner_;
    ShellHost                            host_;
    std::vector<AdminRequest>            requests_;
    std::function<void(const OpResult&)> after_;
    ui::InputText*                       reason_{nullptr};
    ui::Button*                          cancel_{nullptr};
    ui::Button*                          ok_{nullptr};
    kit::Frame                           frame_;
    bool                                 done_{false};
    core::ConnectionScope                links_;
};

class RefuseDialog final : public kit::Dialog {
public:
    RefuseDialog(ShellHost host, std::vector<AdminRequest> requests, std::function<void(const OpResult&)> after)
        : kit::Dialog("dialog.demandes-admins.refuser"), host_(std::move(host)), requests_(std::move(requests)), after_(std::move(after)) {}
    [[nodiscard]] std::string title() const override { return "Refuser des demandes d'admin"; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<RefuseBody>(*this, host_, requests_, after_));
        return core::ok();
    }

private:
    ShellHost                            host_;
    std::vector<AdminRequest>            requests_;
    std::function<void(const OpResult&)> after_;
};

// =================================================================================== la page
class AdminRequestsPage final : public Page {
public:
    // 1.47.0 : l'erreur du service tient sur une ligne (listkit::shortError) ; sous la souris, son texte entier.
    [[nodiscard]] bool hasTooltip() const override { return !problem_.empty() || Page::hasTooltip(); }
    [[nodiscard]] std::string liveTooltip(gfx::Point m) const override {
        if (!problem_.empty() && m.y >= statusY_ - 4.f && m.y <= statusY_ + 22.f) return problem_;
        return Page::liveTooltip(m);
    }

    explicit AdminRequestsPage(ShellHost host) : Page("page.demandes-admins", std::move(host)) {
        fetch_ = &kit::button(*this, "Actualiser", "demandes-admins.relever", ui::Button::Style::Default, ui::Icon::Refresh);
        fetch_->setTooltip("Relire les demandes d'admin du service (en attente, et celles closes depuis 7 jours).");
        accept_ = &kit::button(*this, "Accepter\xE2\x80\xA6", "demandes-admins.accepter", ui::Button::Style::Primary, ui::Icon::Ok);
        refuse_ = &kit::button(*this, "Refuser\xE2\x80\xA6", "demandes-admins.refuser", ui::Button::Style::Danger, ui::Icon::Close);
        table_ = &kit::adopt(*this, std::make_unique<ui::TableView>("demandes-admins.table"));
        // les 8 colonnes tiennent dans la fenetre de 1373 px (sans defiler de cote)
        table_->setColumns({{"Re\xC3\xA7ue le", 142.f}, {"Nom", 132.f}, {"Courriel", 172.f}, {"PC", 100.f}, {"Logiciel", 150.f}, {"Empreinte", 112.f},
                            {"Invitation", 172.f}, {"\xC3\x89tat", 100.f}});
        table_->setSelectionMode(ui::SelectionMode::Extended);
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Re\xC3\xA7ue le", "Nom", "Courriel", "PC", "Logiciel", "Empreinte", "Invitation", "\xC3\x89tat"});
        table_->setModel(model_);
        bar_ = &kit::adopt(*this, std::make_unique<ui::FilterBar>("demandes-admins", barFields()));
        bar_->setNoun("demandes");
        bar_->setClock([this] { return listkit::seconds(ws().now()); });
        {
            ui::FilterBar::Source src;
            src.rows = [this] { return all_.size(); };
            src.text = [this](std::size_t r, std::size_t f) { return r < texts_.size() && f < texts_[r].size() ? texts_[r][f] : std::string(); };
            src.time = [this](std::size_t r, std::size_t f) -> std::int64_t { return f == 0 && r < all_.size() ? listkit::seconds(all_[r].received) : 0; };
            src.extra = [this](std::size_t r) { return r < all_.size() ? all_[r].fingerprint + " " + all_[r].id + " " + all_[r].key : std::string(); };
            bar_->setSource(std::move(src));
        }
        bar_->linkTable(*table_);
        bar_->setMemoryKey("demandes-admins");
        if (!bar_->state().active()) bar_->setValues(kStateField, {kWaitingLabel});   // d'office : celles qui attendent
        links_ += bar_->changed->connect([this] { rebuild(); });
        links_ += ui::ListMenu::bind(*table_, [this] { return menuFor(); });
        table_->setUnfold([this](RowIndex r) { return unfoldFor(r); }, 8);   // reperee par sa cle (colonne cachee)
        links_ += fetch_->clicked->connect([this] { fetch(true); });
        links_ += accept_->clicked->connect([this] { askAccept(); });
        links_ += refuse_->clicked->connect([this] { askRefuse(); });
        links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>&) { syncButtons(); });
        links_ += table_->activated->connect([this](RowIndex r) { (void)table_->toggleUnfolded(r); });
        refresh();
    }
    ~AdminRequestsPage() override {
        links_.clear();
        *life_ = false;
    }

    [[nodiscard]] std::string title() const override { return "Demandes d'admin"; }

    void refresh() override {
        invitations_ = ws().adminInvitations();
        reloadTexts();
        rebuild();
        if (allowed() && ws().service().ready() && loadedAt_ == 0 && !loading_ && !tried_) {
            tried_ = true;   // une fois a l'arrivee ; ensuite << Relever >> (ou apres un geste)
            fetch(false);
        }
        invalidateLayout();
        invalidate();
    }

    // Les essais.
    [[nodiscard]] const std::vector<AdminRequest>& shown() const noexcept { return shown_; }
    [[nodiscard]] const std::vector<AdminRequest>& all() const noexcept { return all_; }
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] ui::FilterBar& bar() noexcept { return *bar_; }
    [[nodiscard]] std::string statusText() const { return status(); }
    [[nodiscard]] std::string emptyText() const { return empty(); }
    [[nodiscard]] std::string acceptLabel() const { return accept_->text(); }
    [[nodiscard]] std::string refuseLabel() const { return refuse_->text(); }
    [[nodiscard]] ui::ListMenu menu() { return menuFor(); }
    void reload() { fetch(true); }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        float y = b.y + kPad;
        titleY_ = y;
        y += 34.f;
        leadLines_ = kit::wrap(lead(), FontId{14}, w);
        if (leadLines_.size() > 3) leadLines_.resize(3);
        leadY_ = y;
        y += static_cast<float>(leadLines_.size()) * kLineH + 10.f;
        float bx = x;
        for (ui::Button* btn : {accept_, refuse_, fetch_}) {
            if (!btn->visible()) continue;
            const float bw = btn->sizeHint().preferred.w + 8.f;
            btn->setBounds({bx, y, bw, kButtonH});
            bx += bw + kGap;
        }
        y += kButtonH + kGap;
        const float bh = bar_->heightFor(w);
        bar_->setBounds({x, y, w, bh});
        y += bh + kGap;
        statusY_ = y;
        y += kLineH + 6.f;
        table_->setBounds({x, y, w, std::max(60.f, b.bottom() - kPad - y)});
        emptyY_ = y + 40.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        if (const bool bz = busy(); bz != lastBusy_) {
            lastBusy_ = bz;
            syncButtons();
        }
        // le releve de l'arrivee n'a pas pu partir (un autre travail tournait) : des que la coquille est libre
        if (!tried_ && !lastBusy_ && allowed() && loadedAt_ == 0 && !loading_ && ws().service().ready()) {
            tried_ = true;
            fetch(false);
        }
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const Rect b = bounds();
        r.fillRect(b, c.panelBg);
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        r.drawText({x, titleY_}, "Demandes d'admin", ctx.theme.font.title, c.text);
        for (std::size_t i = 0; i < leadLines_.size(); ++i)
            r.drawText({x, leadY_ + static_cast<float>(i) * kLineH}, fit(r, leadLines_[i], FontId{14}, w), FontId{14}, c.textMuted);
        r.drawText({x, statusY_}, fit(r, status(), FontId{14}, w), FontId{14}, problem_.empty() ? c.text : c.error);
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!shown_.empty()) return;
        auto& r = ctx.r;
        const Rect t = table_->bounds();
        float y = emptyY_;
        for (const auto& l : kit::wrap(empty(), FontId{14}, t.w - 32.f)) {
            r.drawText({t.x + 16.f, y}, l, FontId{14}, ctx.theme.color.textMuted);
            y += kLineH;
        }
    }

private:
    static constexpr std::size_t kStateField = 8;
    static constexpr std::size_t kInvField = 9;

    [[nodiscard]] bool allowed() const { return ws().role() == Role::Plus; }
    [[nodiscard]] bool busy() const { return host_.busy && host_.busy(); }

    [[nodiscard]] std::string lead() const {
        if (!allowed())
            return "Seul l'admin+ accepte un admin : il signe la liste des admins avec la cl\xC3\xA9 de l'\xC3\xA9" "diteur. Cette page est en lecture seule pour toi.";
        return "Les demandes pour devenir admin, envoy\xC3\xA9" "es par l'Admin des futurs admins. Choisis-en une ou plusieurs : \xC2\xAB Accepter \xC2\xBB les met "
               "toutes dans une seule liste des admins sign\xC3\xA9" "e - apr\xC3\xA8s avoir lu chaque empreinte au t\xC3\xA9l\xC3\xA9phone.";
    }

    [[nodiscard]] std::size_t waitingCount() const {
        std::size_t n = 0;
        for (const auto& q : all_) n += q.waiting() ? 1 : 0;
        return n;
    }

    [[nodiscard]] std::string status() const {
        if (!allowed()) return "R\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+.";
        if (loading_) return "Relev\xC3\xA9 des demandes d'admin en cours\xE2\x80\xA6";
        if (!problem_.empty()) return listkit::shortError(problem_);   // 1.47.0 : une ligne ; l'infobulle dit tout
        if (!ws().service().ready()) return "Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne).";
        if (loadedAt_ == 0) return "Pas encore relev\xC3\xA9" "es.";
        const std::size_t n = waitingCount();
        return (n == 0 ? std::string("Aucune demande en attente") : std::to_string(n) + (n > 1 ? " demandes en attente" : " demande en attente")) +
               " \xC2\xB7 relev\xC3\xA9" "es " + kit::whenFr(ws().now(), loadedAt_) + " \xC2\xB7 " + std::to_string(invitations_.size()) +
               (invitations_.size() > 1 ? " invitations not\xC3\xA9" "es" : " invitation not\xC3\xA9" "e") + " sur ce poste";
    }

    [[nodiscard]] std::string empty() const {
        if (!allowed()) return "Seul l'admin+ voit et accepte les demandes d'admin.";
        if (!ws().service().ready()) return "R\xC3\xA8gle d'abord le service des licences : les demandes y arrivent.";
        if (loadedAt_ == 0) return loading_ ? std::string() : std::string("Clique \xC2\xAB Actualiser \xC2\xBB.");
        if (!all_.empty()) return "Aucune demande ne correspond aux filtres (\xC2\xAB Effacer les filtres \xC2\xBB montre toutes celles des 7 derniers jours).";
        return "Aucune demande d'admin. Un futur admin la fait depuis son Admin (Administrateurs \xE2\x80\xBA Demander \xC3\xA0 devenir admin), apr\xC3\xA8s ton invitation "
               "(\xC2\xAB Ajouter un admin\xE2\x80\xA6 \xC2\xBB).";
    }

    static std::vector<ui::FilterBar::Field> barFields() {
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> f(11);
        f[0] = {.title = "Re\xC3\xA7ue le", .kind = K::Period, .chip = true, .chipLabel = "P\xC3\xA9riode"};
        f[1] = {.title = "Nom", .kind = K::Text};
        f[2] = {.title = "Courriel", .kind = K::Text};
        f[3] = {.title = "PC", .noun = "un PC"};
        f[4] = {.title = "Logiciel", .noun = "un logiciel"};
        f[5] = {.title = "Empreinte", .kind = K::Text};
        f[6] = {.title = "Invitation (d\xC3\xA9tail)", .kind = K::Text};
        f[7] = {.title = "\xC3\x89tat (d\xC3\xA9tail)", .kind = K::Text};
        f[8] = {.title = "\xC3\x89tat", .chip = true, .noun = "un \xC3\xA9tat", .order = {kWaitingLabel, "Accept\xC3\xA9" "e", "Refus\xC3\xA9" "e"}};
        f[9] = {.title = "Invitation", .chip = true, .noun = "une invitation", .order = {"invit\xC3\xA9", "sans invitation"}};
        f[10] = {.title = "Cl\xC3\xA9", .kind = K::Text};
        return f;
    }

    void reloadTexts() {
        texts_.clear();
        for (const auto& q : all_) {
            const AdminInvitation* inv = invitationFor(invitations_, q);
            std::string st = stateLabel(q);
            if (q.fingerprintMismatch) st += " \xE2\x80\x94 empreinte fausse";
            else if (!q.waiting() && q.changed) st += " " + dateFr(q.changed);
            // 1.49.0 : une demande avec invitation par codes la dit (son id), meme notee sur un autre poste
            const std::string invText = inv || !q.byCode() ? invitationText(inv)
                                                           : q.invitationId + (q.invitationProfile.empty() ? std::string() : " (" + q.invitationProfile + ")");
            texts_.push_back({dateTimeFr(q.received), requestWho(q), q.email, q.pc, q.software, kit::shortFingerprint(q.fingerprint) + "\xE2\x80\xA6", invText, st,
                              stateLabel(q), inv || q.byCode() ? "invit\xC3\xA9" : "sans invitation", q.key});
        }
        bar_->refreshCounts();
    }

    void rebuild() {
        shown_.clear();
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> keys;
        std::vector<ui::Tone> tones;
        std::vector<bool> waiting;
        for (std::size_t i = 0; i < all_.size(); ++i) {
            if (!bar_->accepts(i)) continue;
            shown_.push_back(all_[i]);
            auto cells = texts_[i];
            cells.resize(8);
            cells.push_back(all_[i].key);   // 8 (cachee) : la cle du depliage
            rows.push_back(std::move(cells));
            keys.push_back(padKey(static_cast<long long>(all_[i].received)));
            tones.push_back(stateTone(all_[i]));
            waiting.push_back(all_[i].waiting());
        }
        model_->set(std::move(rows), std::move(keys), [tones, waiting](RowIndex r, std::size_t c) {
            ui::CellStyle st;
            if (r >= tones.size()) return st;
            if (c == 1) st.bold = waiting[r];
            if (c == 5) st.monospace = true;
            if (c == 7) {
                st.fgTone = tones[r];
                st.bold = waiting[r];
            }
            return st;
        });
        if (!shown_.empty() && table_->selection().empty()) table_->selectModelRows({0}, false);   // la premiere, d'office
        syncButtons();
        invalidateLayout();
        invalidate();
    }

    [[nodiscard]] std::vector<AdminRequest> selected() const {
        std::vector<AdminRequest> out;
        for (const RowIndex r : table_->selectedModelRows())
            if (r < shown_.size()) out.push_back(shown_[r]);
        return out;
    }
    [[nodiscard]] std::vector<AdminRequest> selectedWaiting() const {
        std::vector<AdminRequest> out;
        for (auto& q : selected())
            if (q.waiting()) out.push_back(q);
        return out;
    }

    [[nodiscard]] std::string whyNotAccept(const std::vector<AdminRequest>& waiting) const {
        if (!allowed()) return "seul l'admin+ accepte un admin (il signe la liste des admins)";
        if (busy()) return kBusy;
        if (waiting.empty()) return selected().empty() ? std::string("choisis une demande en attente") : std::string("elle n'attend plus (accept\xC3\xA9" "e ou refus\xC3\xA9" "e)");
        for (const auto& q : waiting)
            if (q.fingerprintMismatch) return requestWho(q) + " : le service rend une autre empreinte, ne l'accepte pas";
        return {};
    }
    [[nodiscard]] std::string whyNotRefuse(const std::vector<AdminRequest>& waiting) const {
        if (!allowed()) return "seul l'admin+ r\xC3\xA9pond aux demandes d'admin";
        if (busy()) return kBusy;
        if (waiting.empty()) return selected().empty() ? std::string("choisis une demande en attente") : std::string("elle n'attend plus (accept\xC3\xA9" "e ou refus\xC3\xA9" "e)");
        return {};
    }

    void syncButtons() {
        const auto w = selectedWaiting();
        const std::size_t n = w.size();
        accept_->setText(n > 1 ? "Accepter " + std::to_string(n) + " demandes\xE2\x80\xA6" : std::string("Accepter\xE2\x80\xA6"));
        refuse_->setText(n > 1 ? "Refuser " + std::to_string(n) + " demandes\xE2\x80\xA6" : std::string("Refuser\xE2\x80\xA6"));
        const std::string na = whyNotAccept(w), nr = whyNotRefuse(w);
        accept_->setEnabled(na.empty());
        refuse_->setEnabled(nr.empty());
        accept_->setTooltip(na.empty() ? std::string("Une fen\xC3\xAAtre : chaque empreinte \xC3\xA0 cocher, un profil pour chacun ; puis une seule liste des admins sign\xC3\xA9" "e.") : na);
        refuse_->setTooltip(nr.empty() ? std::string("Une raison facultative (au journal des admins).") : nr);
        fetch_->setEnabled(allowed() && !loading_ && !busy() && ws().service().ready());
        kit::show(accept_, allowed());
        kit::show(refuse_, allowed());
        kit::show(fetch_, allowed());
        invalidateLayout();
    }

    void fetch(bool loud) {
        if (!allowed() || loading_) return;
        if (!ws().service().ready()) {
            if (loud && host_.flash) host_.flash("Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne).", Tone::Warning);
            return;
        }
        const Workspace* w = &ws();
        auto out = std::make_shared<std::vector<AdminRequest>>();
        std::weak_ptr<bool> alive = life_;
        loading_ = true;
        const auto done = [this, alive, out](const OpResult& r) {
            if (alive.expired()) return;
            loading_ = false;
            if (r.ok) {
                all_ = std::move(*out);
                loadedAt_ = ws().now();
                problem_.clear();
            } else {
                problem_ = r.message;
            }
            refresh();
        };
        const auto work = [w, out] { return w->adminRequests(*out, true); };
        const bool started = loud ? (host_.runSlow && host_.runSlow("Service : relev\xC3\xA9 des demandes d'admin", work, done))
                                  : kit::runQuietly(host_, "Service : relev\xC3\xA9 des demandes d'admin", work, done);
        if (!started) {
            loading_ = false;
            if (loud && host_.flash) host_.flash(kBusy, Tone::Warning);
            if (!loud && loadedAt_ == 0) tried_ = false;   // reessaye quand la coquille sera libre (onPaint)
        }
        if (!alive.expired()) syncButtons();
    }

    // Apres un geste : tout relire (le Workspace : la liste signee ; puis les demandes).
    void afterAction(const OpResult& r) {
        (void)r;
        ws().reload();
        loadedAt_ = 0;
        tried_ = true;
        fetch(false);
    }

    void askAccept() {
        const auto w = selectedWaiting();
        if (const std::string why = whyNotAccept(w); !why.empty()) {
            if (host_.flash) host_.flash(why, Tone::Warning);
            return;
        }
        std::weak_ptr<bool> alive = life_;
        host_.openDialog(std::make_unique<AcceptDialog>(host_, w, invitations_, [this, alive](const OpResult& r) {
            if (!alive.expired()) afterAction(r);
        }));
    }

    void askRefuse() {
        const auto w = selectedWaiting();
        if (const std::string why = whyNotRefuse(w); !why.empty()) {
            if (host_.flash) host_.flash(why, Tone::Warning);
            return;
        }
        std::weak_ptr<bool> alive = life_;
        host_.openDialog(std::make_unique<RefuseDialog>(host_, w, [this, alive](const OpResult& r) {
            if (!alive.expired()) afterAction(r);
        }));
    }

    // ---- le clic droit, dans l'ordre de la regle (ListMenu.hpp) ----
    ui::ListMenu menuFor() {
        const auto rows = table_->selectedModelRows();
        ui::ListMenu m({rows.size(), "demandes", true});
        if (rows.empty()) return m;
        const auto waiting = selectedWaiting();
        const AdminRequest* q = rows.front() < shown_.size() ? &shown_[rows.front()] : nullptr;
        if (rows.size() == 1 && q) {
            const RowIndex row = rows.front();
            m.open(table_->isUnfolded(row) ? "Replier la demande" : "Ouvrir la demande", [this, row] { (void)table_->toggleUnfolded(row); });
        }
        m.action({.label = "Accepter\xE2\x80\xA6", .many = "Accepter les %n demandes\xE2\x80\xA6", .icon = ui::Icon::Ok, .why = whyNotAccept(waiting),
                  .run = [this] { askAccept(); }});
        if (rows.size() == 1 && q) {
            if (const adm::Admin* a = ws().adminList() ? ws().adminList()->byKey(q->key) : nullptr) {
                const std::string key = a->key, name = a->name;
                m.goTo({.label = "L'admin : " + name, .icon = ui::Icon::User, .run = [this, key] {
                            requestPageFocus("admins", "admin:" + key);
                            host_.navigate("admins");
                        }});
            }
            m.goTo({.label = "Administrateurs", .icon = ui::Icon::User, .run = [this] { host_.navigate("admins"); }});
            const std::size_t i = indexOf(*q);
            const auto on = [&](std::size_t f, const std::string& label) {
                if (i >= texts_.size() || texts_[i][f].empty()) return;
                const std::string v = texts_[i][f];
                m.filterOn({.label = label + " : " + v, .run = [this, f, v] { bar_->filterOn(f, v); }});
            };
            on(3, "Ce PC");
            on(4, "Ce logiciel");
            on(kStateField, "Cet \xC3\xA9tat");
            on(kInvField, "Invitation");
            if (q->received > 0) {
                const std::int64_t when = listkit::seconds(q->received);
                m.filterOn({.label = "Ce jour : " + dateFr(q->received), .run = [this, when] { bar_->filterOnDay(0, when); }});
            }
        }
        std::string fps, mails, lines;
        for (const RowIndex r : rows) {
            if (r >= shown_.size()) continue;
            fps += requestWho(shown_[r]) + " : " + shown_[r].fingerprint + "\n";
            if (!shown_[r].email.empty()) mails += (mails.empty() ? "" : "; ") + shown_[r].email;
            std::string line;
            for (std::size_t c = 0; c < 8; ++c) line += (c ? "\t" : "") + model_->cellText(r, c);
            lines += line + "\n";
        }
        if (q) {
            m.copy("L'empreinte", q->fingerprint, ui::Icon::Lock, "Copier les %n empreintes", fps);
            if (!q->email.empty()) m.copy("Le courriel", q->email, ui::Icon::Mail, "Copier les courriels", mails);
            m.copy("La cl\xC3\xA9 (64 hexad\xC3\xA9" "cimaux)", q->key, ui::Icon::Copy);
        }
        m.copy("La ligne", lines, ui::Icon::Copy, "Copier les %n lignes", lines);
        m.exportRows([this, rows] { listkit::exportRows(host_, *table_, rows, "demandes-admin.csv", "demandes d'admin"); });
        m.danger({.label = "Refuser\xE2\x80\xA6", .many = "Refuser les %n demandes\xE2\x80\xA6", .icon = ui::Icon::Close, .why = whyNotRefuse(waiting),
                  .run = [this] { askRefuse(); }});
        return m;
    }

    [[nodiscard]] std::size_t indexOf(const AdminRequest& q) const {
        for (std::size_t i = 0; i < all_.size(); ++i)
            if (all_[i].key == q.key) return i;
        return all_.size();
    }

    // ---- la ligne depliee : l'empreinte en grand, la demande, l'invitation ----
    ui::WidgetPtr unfoldFor(RowIndex r) {
        if (r >= shown_.size()) return nullptr;
        const AdminRequest q = shown_[r];
        auto card = std::make_unique<ui::UnfoldCard>("demandes-admins.depliage");
        const Seconds now = ws().now();
        ui::UnfoldCard::Box fp;
        fp.title = "L'EMPREINTE \xC2\xB7 \xC3\x80 LIRE AU T\xC3\x89L\xC3\x89PHONE";
        fp.weight = 1.6f;
        fp.height = 64.f;
        const std::string f = q.fingerprint;
        const bool bad = q.fingerprintMismatch;
        fp.paint = [f, bad](const ui::PaintContext& ctx, gfx::Rect box) {
            paintGroups(ctx.r, ctx.theme, {box.x, box.y + 6.f, std::min(box.w, 560.f), 46.f}, f, gfx::semibold(FontId{22}), bad);
        };
        card->addBox(std::move(fp));
        ui::UnfoldCard::Box ask;
        ask.title = "LA DEMANDE";
        ask.lines.push_back({.icon = ui::Icon::User, .text = "**" + requestWho(q) + "**" + (q.email.empty() ? std::string() : " \xC2\xB7 " + q.email)});
        ask.lines.push_back({.icon = ui::Icon::Station, .text = (q.pc.empty() ? std::string("(sans nom)") : q.pc) + (q.software.empty() ? std::string() : " \xC2\xB7 " + q.software)});
        ask.lines.push_back({.icon = ui::Icon::History, .text = "re\xC3\xA7ue " + kit::whenFr(now, q.received)});
        ask.lines.push_back({.icon = ui::Icon::Lock, .text = q.id, .mono = true});
        if (bad) ask.lines.push_back({.icon = ui::Icon::Error, .text = "**le service rend une autre empreinte : ne l'accepte pas**", .tone = ui::Tone::Error});
        else if (!q.waiting())
            ask.lines.push_back({.icon = q.state == "acceptee" ? ui::Icon::Ok : ui::Icon::Close, .text = "**" + stateLabel(q) + "** " + kit::whenFr(now, q.changed),
                                 .tone = stateTone(q)});
        card->addBox(std::move(ask));
        ui::UnfoldCard::Box inv;
        inv.title = "L'INVITATION";
        if (q.byCode())   // 1.49.0 : l'invitation par codes que la demande a servie (le service le dit), et les 6 chiffres a taper
            inv.lines.push_back({.icon = ui::Icon::Lock, .text = "invitation **" + q.invitationId + "** \xC2\xB7 \xC2\xAB Accepter\xE2\x80\xA6 \xC2\xBB demande les 6 chiffres de son Admin", .tone = ui::Tone::Ok, .check = true});
        if (const AdminInvitation* i = invitationFor(invitations_, q)) {
            inv.lines.push_back({.icon = ui::Icon::Mail, .text = "invit\xC3\xA9 " + kit::whenFr(now, i->at) + (i->via.empty() ? std::string() : " (" + i->via + ")"), .tone = ui::Tone::Ok, .check = true});
            if (!i->name.empty() || !i->email.empty()) inv.lines.push_back({.icon = ui::Icon::User, .text = i->name + (i->email.empty() ? std::string() : " \xC2\xB7 " + i->email)});
            inv.lines.push_back({.icon = ui::Icon::Layers, .text = i->profile.empty() ? std::string("aucun profil pr\xC3\xA9vu") : "profil pr\xC3\xA9vu : **" + i->profile + "**"});
        } else if (!q.byCode()) {
            inv.lines.push_back({.icon = ui::Icon::Warning, .text = "**sans invitation** : v\xC3\xA9rifie bien qui c'est avant d'accepter", .tone = ui::Tone::Warning});
        }
        card->addBox(std::move(inv));
        if (q.waiting() && allowed()) {
            const RowIndex row = r;
            (void)card->addButton("Accepter\xE2\x80\xA6", "demandes-admins.depliage.accepter", ui::Icon::Ok, [this, row] {
                table_->selectModelRows({row}, false);
                askAccept();
            }, bad ? std::string("empreinte fausse") : std::string(), false);
            (void)card->addButton("Refuser\xE2\x80\xA6", "demandes-admins.depliage.refuser", ui::Icon::Close, [this, row] {
                table_->selectModelRows({row}, false);
                askRefuse();
            }, {}, false);
        }
        return card;
    }

    std::shared_ptr<bool>                 life_{std::make_shared<bool>(true)};
    core::ConnectionScope                 links_;
    ui::Button*                           fetch_{nullptr};
    ui::Button*                           accept_{nullptr};
    ui::Button*                           refuse_{nullptr};
    ui::FilterBar*                        bar_{nullptr};
    ui::TableView*                        table_{nullptr};
    std::shared_ptr<Rows>                 model_;
    std::vector<AdminRequest>             all_, shown_;
    std::vector<std::vector<std::string>> texts_;
    std::vector<AdminInvitation>          invitations_;
    std::vector<std::string>              leadLines_;
    std::string                           problem_;
    Seconds                               loadedAt_{0};
    bool                                  loading_{false}, tried_{false}, lastBusy_{false};
    float                                 titleY_{0}, leadY_{0}, statusY_{0}, emptyY_{0};
};

} // namespace

PagePtr makeAdminRequestsPage(ShellHost host) { return std::make_unique<AdminRequestsPage>(std::move(host)); }

} // namespace license_admin::gui
