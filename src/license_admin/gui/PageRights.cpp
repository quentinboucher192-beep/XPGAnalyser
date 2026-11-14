// =============================================================================
//  license_admin/gui/PageRights.cpp - 1.44.0 : << LES DROITS DES ADMINS >>
//  (maquette 1.43.0, planche 08 ; docs/ADMINS.md, par. 2) - une sous-page
//  d'Administrateurs (le fil : Administrateurs > Droits), pour l'admin+ seul.
// -----------------------------------------------------------------------------
//  Trois colonnes :
//    - LES PROFILS (<< Tous les droits >> de depart, qui ne se modifie pas ;
//      << Admin LV1 >>, << Admin LV2 >>... : n fonctions sur 13, k admins) et
//      << Nouveau profil... >> ;
//    - LE PROFIL CHOISI : ses 13 fonctions par groupe, un interrupteur chacune,
//      << Qui la verifie >> (les Studios et le service : signe sans le droit =
//      refuse partout ; l'Admin : bloque dans l'Admin), ce que l'admin+ garde
//      toujours ; Dupliquer, Renommer, Supprimer le profil, Enregistrer le
//      profil ; << Copier vers un admin... >> (des cases, un admin ou plusieurs :
//      une COPIE de ses droits, pas un lien) ;
//    - UN ADMIN : ses droits a lui, les differences avec son profil marquees
//      (<< retire pour Nina >>, << ajoute pour Nina >>), << Recopier le profil >>,
//      << Signer et publier >> (la cle de l'editeur signe la nouvelle liste des
//      admins : Workspace::setAdminRights, lent).
//  Les profils s'enregistrent sur ce poste (et partent avec l'archive) ; les
//  droits d'un admin, eux, sont dans la liste signee publiee sur le service.
// =============================================================================
#include "AdminsKit.hpp"
#include "ClassicView.hpp"   // 1.52.0 : XPG Admin sous derogation (lire, jamais signer)
#include "Page.hpp"

#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace licdlg {
// Definis dans LicenceWizard.cpp.
menu::MenuPtr makeConfirmDialog(std::string id, std::string title, std::string text, std::string okLabel, bool danger,
                                std::function<void()> onOk);
} // namespace licdlg

namespace {

using namespace kit;
using gfx::Color;
using gfx::FontId;
using gfx::Rect;

constexpr FontId kF28{28}, kF16{16}, kF15{15}, kF14{14}, kF13{13}, kF12{12}, kF11{11};
const char* const kGroups[3] = {"licences", "offres", "clients"};

class Names final : public ui::ITableModel {
public:
    explicit Names(std::vector<std::string> rows) : rows_(std::move(rows)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 1; }
    [[nodiscard]] std::string headerText(std::size_t) const override { return "Profil"; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t) const override { return r < rows_.size() ? rows_[r] : std::string(); }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }

private:
    std::vector<std::string> rows_;
};

std::string checkedByShort(const char* by) {
    const std::string s = by;
    if (s.find("Studios") != std::string::npos) return "Studios et service";
    return s;
}

class RightsPage;

// << Copier vers un admin... >> : des cases (un admin chacune), dessinees par-dessus la page.
class CopyPanel final : public ui::Widget {
public:
    CopyPanel() : ui::Widget("droits.copier.panneau") {}
    std::string title;
    std::vector<std::string> sub;   // le PC et le profil de chaque admin (a cote de sa case)

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRoundedRect({b.x, b.y + 3.f, b.w, b.h + 2.f}, rgb(0x0B1222).withAlpha(30), 12.f);
        ui::shapes::fillFramedRect(ctx.r, b, c.panelBg, c.border, 12.f);
        ctx.r.drawText({b.x + 16.f, b.y + 14.f}, fit(ctx.r, title, gfx::semibold(kF11), b.w - 32.f), gfx::semibold(kF11), c.textMuted);
        float y = b.y + 40.f;
        for (const auto& s : sub) {
            ctx.r.drawText({b.right() - 16.f - ctx.r.measure(s, kF12).width, y + 7.f}, s, kF12, c.textMuted);
            y += 34.f;
        }
        const float ny = y + 6.f;
        ctx.r.fillRect({b.x + 12.f, ny - 4.f, b.w - 24.f, 1.f}, c.gridLine);
        (void)drawWrapped(ctx.r, b.x + 16.f, ny + 4.f, b.w - 32.f,
                          "Une copie : ses droits deviennent ceux du profil, ses ajustements sont effac\xC3\xA9s. Changer le profil ensuite ne change pas ses droits.",
                          kF12, c.textMuted, 16.f, 3);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        // Un clic dans le panneau ne passe jamais a ce qui est dessous.
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && bounds().contains(d->pos)) return ui::EventResult::Consumed;
        return ui::EventResult::Ignored;
    }
};

class RightsPage final : public Page {
public:
    explicit RightsPage(ShellHost host) : Page("droits", std::move(host)) {
        back_ = &button(*this, "Administrateurs", "droits.retour", ui::Button::Style::Default, ui::Icon::Undo);
        back_->setTooltip("Revenir \xC3\xA0 la page Administrateurs.");
        newProfile_ = &button(*this, "Nouveau profil\xE2\x80\xA6", "droits.nouveau", ui::Button::Style::Default, ui::Icon::Plus);
        duplicate_ = &button(*this, "Dupliquer", "droits.dupliquer", ui::Button::Style::Default, ui::Icon::Duplicate);
        rename_ = &button(*this, "Renommer", "droits.renommer", ui::Button::Style::Default, ui::Icon::Rename);
        copyTo_ = &button(*this, "Copier vers un admin\xE2\x80\xA6", "droits.copier", ui::Button::Style::Default, ui::Icon::User);
        delete_ = &button(*this, "Supprimer le profil", "droits.supprimer", ui::Button::Style::Danger, ui::Icon::Delete);
        cancel_ = &button(*this, "Annuler", "droits.annuler");
        save_ = &button(*this, "Enregistrer le profil", "droits.enregistrer", ui::Button::Style::Primary, ui::Icon::Ok);
        admin_ = &adopt(*this, std::make_unique<ui::DropDown>("droits.admin"));
        recopy_ = &button(*this, "Recopier le profil", "droits.recopier");
        recopy_->setTooltip("Ses droits redeviennent ceux de son profil d'origine (ses ajustements sont effac\xC3\xA9s) ; puis \xC2\xAB Signer et publier \xC2\xBB.");
        adminCancel_ = &button(*this, "Annuler", "droits.admin.annuler");
        publish_ = &button(*this, "Signer et publier", "droits.publier", ui::Button::Style::Primary, ui::Icon::Ok);
        for (const auto& r : adm::rights()) {
            const std::string id = r.id;
            Switch& p = adopt(*this, std::make_unique<Switch>(r.label, "droits.profil." + id));
            Switch& a = adopt(*this, std::make_unique<Switch>(r.label, "droits.admin." + id));
            links_ += p.toggled->connect([this, id](bool on) {
                if (syncing_) return;
                if (on) draft_.insert(id);
                else draft_.erase(id);
                update();
            });
            links_ += a.toggled->connect([this, id](bool on) {
                if (syncing_) return;
                if (on) adminDraft_.insert(id);
                else adminDraft_.erase(id);
                update();
            });
            profileSw_.push_back(&p);
            adminSw_.push_back(&a);
        }
        table_ = &adopt(*this, std::make_unique<ui::TableView>("droits.profils"));
        table_->setSelectionMode(ui::SelectionMode::Single);
        table_->setVisibility(ui::Visibility::Collapsed);   // les scripts : la page dessine la liste
        panel_ = &adopt(*this, std::make_unique<CopyPanel>());
        panel_->setVisibility(ui::Visibility::Collapsed);
        copyOk_ = &button(*panel_, "Copier", "droits.copier.ok", ui::Button::Style::Primary);
        copyClose_ = &button(*panel_, "Fermer", "droits.copier.fermer");

        links_ += back_->clicked->connect([this] { host_.navigate("admins"); });
        links_ += newProfile_->clicked->connect([this] { askNew(); });
        links_ += duplicate_->clicked->connect([this] { duplicate(); });
        links_ += rename_->clicked->connect([this] { askRename(); });
        links_ += delete_->clicked->connect([this] { askDelete(); });
        links_ += cancel_->clicked->connect([this] {
            dirtyReset();
            update();
        });
        links_ += save_->clicked->connect([this] { saveProfile(); });
        links_ += copyTo_->clicked->connect([this] { openCopy(!panel_->visible()); });
        links_ += copyClose_->clicked->connect([this] { openCopy(false); });
        links_ += copyOk_->clicked->connect([this] { copyNow(); });
        links_ += recopy_->clicked->connect([this] {
            if (const adm::Profile* p = profileOf(adminProfileName())) {
                adminDraft_ = std::set<std::string>(p->rights.begin(), p->rights.end());
                update();
            }
        });
        links_ += adminCancel_->clicked->connect([this] {
            adminReset();
            update();
        });
        links_ += publish_->clicked->connect([this] { publish(); });
        links_ += admin_->selectionChanged->connect([this](int i) {
            if (syncing_) return;
            if (i >= 0 && static_cast<std::size_t>(i) < admin_->items().size()) adminKey_ = admin_->items()[static_cast<std::size_t>(i)].value;
            adminReset();
            update();
        });
        links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& sel) {
            if (syncing_ || sel.empty() || sel.front() >= profiles_.size()) return;
            choose(profiles_[sel.front()].name);
        });
        refresh();
    }
    ~RightsPage() override { *alive_ = false; }

    [[nodiscard]] std::string title() const override { return "Droits"; }

    void refresh() override {
        if (const std::string f = takePageFocus("droits"); f.rfind("admin:", 0) == 0) {
            adminKey_ = f.substr(6);
            adminDirty_ = false;
        }
        profiles_ = ws().profiles();
        if (!profileOf(selected_)) {
            selected_.clear();
            for (const auto& p : profiles_)
                if (!p.builtin) {
                    selected_ = p.name;
                    break;
                }
            if (selected_.empty() && !profiles_.empty()) selected_ = profiles_.front().name;
            dirty_ = false;
        }
        if (!dirty_) dirtyReset();
        // Les admins actifs (la liste signee de ce poste).
        admins_.clear();
        if (ws().adminList())
            for (const auto& a : ws().adminList()->admins)
                if (a.active()) admins_.push_back(a);
        if (!adminOf(adminKey_)) {
            adminKey_ = admins_.empty() ? std::string() : admins_.front().key;
            adminDirty_ = false;
        }
        if (!adminDirty_) adminReset();
        // La liste des admins (son choix), le tableau des scripts.
        syncing_ = true;
        std::vector<ui::DropDown::Item> items;
        int pick = -1;
        for (std::size_t i = 0; i < admins_.size(); ++i) {
            ui::DropDown::Item it;
            it.label = admins_[i].name;
            it.value = admins_[i].key;
            it.note = admins_[i].pc;
            items.push_back(std::move(it));
            if (admins_[i].key == adminKey_) pick = static_cast<int>(i);
        }
        admin_->setItems(std::move(items));
        admin_->setSelectedIndex(pick);
        std::vector<std::string> names;
        for (const auto& p : profiles_) names.push_back(p.name);
        table_->setModel(std::make_shared<Names>(std::move(names)));
        for (std::size_t i = 0; i < profiles_.size(); ++i)
            if (profiles_[i].name == selected_) table_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
        syncing_ = false;
        if (panel_->visible()) openCopy(true);
        update();
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float pad = b.w < 900.f ? 20.f : 28.f;
        x0_ = b.x + pad;
        w_ = std::max(600.f, b.w - 2.f * pad);
        titleY_ = b.y + (b.h < 680.f ? 18.f : 26.f) - scroll_;
        const float bw = buttonWidth(*back_, 140.f);
        back_->setBounds({x0_ + w_ - bw, titleY_ + 4.f, bw, 38.f});
        const float top = titleY_ + 80.f, bottom = b.bottom() - 16.f - scroll_;
        // 1.47.0 : une fenetre etroite (1100) - la carte de l'admin passe sous les deux autres (chacune a sa largeur) ; une
        // fenetre basse - la page defile (molette) : rien ne se serre jusqu'a passer sous un bouton. Les boutons qui ne
        // tiennent pas sur leur rangee passent a la suivante (jamais l'un sur l'autre).
        const bool narrow = w_ < 1000.f;
        const float leftW = narrow ? std::clamp(w_ * 0.30f, 240.f, 300.f) : std::clamp(w_ * 0.22f, 200.f, 300.f);
        const float rightW = narrow ? w_ : std::clamp(w_ * 0.29f, 260.f, 410.f);
        const float midW = narrow ? w_ - leftW - 16.f : std::max(300.f, w_ - leftW - rightW - 32.f);
        // ce qui passe a la rangee suivante (les boutons du profil, Supprimer, Annuler de l'admin) ; la hauteur qu'il faut
        const float mw0 = midW - 36.f, rw0 = rightW - 36.f;
        const float sw = buttonWidth(*save_, 170.f), aw = buttonWidth(*cancel_, 90.f), dw = buttonWidth(*delete_, 140.f);
        const bool copyBelow = buttonWidth(*duplicate_, 90.f) + buttonWidth(*rename_, 90.f) + 16.f + buttonWidth(*copyTo_, 160.f) > mw0;
        const bool deleteAbove = dw + 12.f > mw0 - sw - 8.f - aw;   // sa note (<< ses admins gardent... >>) : si elle tient
        const float pw = std::min(rw0, buttonWidth(*publish_, 150.f)), cw2 = buttonWidth(*adminCancel_, 90.f);
        const bool cancelAbove = pw + 8.f + cw2 > rw0;
        const float needMid = 122.f + (copyBelow ? 40.f : 0.f) + 28.f + 16.f * 20.f + 12.f + (deleteAbove ? 46.f : 0.f) + 54.f;
        const float needRight = 176.f + 16.f * 20.f + 96.f + 54.f + (cancelAbove ? 44.f : 0.f);
        const float h1 = std::max({300.f, bottom - top, needMid, narrow ? 0.f : needRight});
        left_ = {x0_, top, leftW, h1};
        mid_ = {left_.right() + 16.f, top, midW, h1};
        right_ = narrow ? Rect{x0_, top + h1 + 16.f, rightW, needRight} : Rect{mid_.right() + 16.f, top, rightW, h1};
        content_ = right_.bottom() + 16.f + scroll_ - b.y;
        if (const float most = std::max(0.f, content_ - b.h); scroll_ > most) {   // la fenetre a grandi
            scroll_ = most;
            invalidateLayout();
        }
        // A gauche
        {   // 1.47.0 : a cote du titre << Profils >> ; etroit : << Nouveau... >> (le libelle entier en infobulle)
            const std::string full = "Nouveau profil\xE2\x80\xA6", brief = "Nouveau\xE2\x80\xA6";
            const float pad2 = buttonWidth(*newProfile_, 0.f) - ui::measureWidth(newProfile_->text(), gfx::FontId{16});
            const float room = leftW - 36.f - ui::measureWidth("Profils", gfx::semibold(kF16)) - 12.f;
            const std::string want = pad2 + ui::measureWidth(full, gfx::FontId{16}) <= room ? full : brief;
            if (newProfile_->text() != want) {
                newProfile_->setText(want);
                newProfile_->setTooltip(want == full ? std::string() : full);
            }
        }
        const float nw = buttonWidth(*newProfile_, 90.f);
        newProfile_->setBounds({left_.right() - 16.f - nw, left_.y + 16.f, nw, 34.f});
        // Au milieu : l'en-tete, les boutons, les lignes, le pied.
        const float mx = mid_.x + 18.f, mw = mid_.w - 36.f;
        float bx = mx;
        for (ui::Button* btn : {duplicate_, rename_}) {
            const float w = buttonWidth(*btn, 90.f);
            btn->setBounds({bx, mid_.y + 72.f, w, 32.f});
            bx += w + 8.f;
        }
        const float cw = buttonWidth(*copyTo_, 160.f);
        float extra = 0.f;
        if (copyBelow) {   // a la rangee suivante
            extra = 40.f;
            copyTo_->setBounds({mx, mid_.y + 72.f + extra, std::min(cw, mw), 32.f});
        } else {
            copyTo_->setBounds({mx + mw - cw, mid_.y + 72.f, cw, 32.f});
        }
        const float fy = mid_.bottom() - 54.f;
        save_->setBounds({mx + mw - sw, fy, sw, 36.f});
        cancel_->setBounds({save_->bounds().x - 8.f - aw, fy, aw, 36.f});
        // << Supprimer le profil >> au-dessus quand la rangee est trop courte
        delete_->setBounds({mx, deleteAbove ? fy - 46.f : fy, dw, 36.f});
        tableTop_ = mid_.y + 122.f + extra;
        const float avail = (deleteAbove ? fy - 46.f : fy) - 12.f - tableTop_ - 28.f;
        // 13 fonctions et 3 groupes ; puis ce que l'admin+ garde (4 lignes et leur titre) s'il reste la place.
        rowH_ = std::clamp(avail / 16.f, 20.f, 30.f);
        plusRows_ = avail - 16.f * rowH_ >= 26.f + 4.f * 20.f;
        if (!plusRows_) rowH_ = std::clamp(avail / 16.f, 20.f, 30.f);
        float y = tableTop_ + 28.f;
        std::size_t k = 0;
        for (const char* g : kGroups) {
            y += rowH_;   // le titre du groupe
            for (const auto& r : adm::rights()) {
                if (std::string(r.group) != g) continue;
                rowY_[r.id] = y;
                profileSw_[k = indexOf(r.id)]->setBounds({mx + mw - 44.f, y + (rowH_ - 22.f) * 0.5f, 40.f, 22.f});
                y += rowH_;
            }
        }
        (void)k;
        // A droite : le choix de l'admin, ses droits.
        const float rx = right_.x + 18.f, rw = right_.w - 36.f;
        admin_->setBounds({rx, right_.y + 16.f, std::min(rw, 420.f), 32.f});
        recopy_->setBounds({right_.right() - 18.f - std::min(rw, buttonWidth(*recopy_, 120.f)), right_.y + 128.f, std::min(rw, buttonWidth(*recopy_, 120.f)), 30.f});
        const float pfy = right_.bottom() - 54.f;
        publish_->setBounds({rx + rw - pw, pfy, pw, 36.f});
        if (cancelAbove) adminCancel_->setBounds({rx + rw - cw2, pfy - 44.f, cw2, 36.f});   // au-dessus : jamais l'un sur l'autre
        else adminCancel_->setBounds({publish_->bounds().x - 8.f - cw2, pfy, cw2, 36.f});
        adminTop_ = right_.y + 176.f;
        const float avail2 = std::min(pfy, adminCancel_->bounds().y) - 96.f - adminTop_;
        adminRowH_ = std::clamp(avail2 / 16.f, 20.f, 30.f);
        y = adminTop_;
        for (const char* g : kGroups) {
            y += adminRowH_;
            for (const auto& r : adm::rights()) {
                if (std::string(r.group) != g) continue;
                adminRowY_[r.id] = y;
                adminSw_[indexOf(r.id)]->setBounds({rx + rw - 44.f, y + (adminRowH_ - 22.f) * 0.5f, 40.f, 22.f});
                y += adminRowH_;
            }
        }
        // Le panneau << Copier vers >>, sous son bouton.
        const float ph = 40.f + 34.f * static_cast<float>(std::max<std::size_t>(1, copyBoxes_.size())) + 64.f + 52.f;
        const Rect pr{copyTo_->bounds().right() - 320.f, copyTo_->bounds().bottom() + 6.f, 320.f, ph};
        panel_->setBounds(pr);
        float cy = pr.y + 40.f;
        for (auto* cb : copyBoxes_) {
            cb->setBounds({pr.x + 12.f, cy, pr.w - 140.f, 30.f});
            cy += 34.f;
        }
        const float okw = buttonWidth(*copyOk_, 120.f), clw = buttonWidth(*copyClose_, 80.f);
        copyOk_->setBounds({pr.right() - 14.f - okw, pr.bottom() - 46.f, okw, 34.f});
        copyClose_->setBounds({copyOk_->bounds().x - 8.f - clw, pr.bottom() - 46.f, clw, 34.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        r.fillRect(bounds(), c.windowBg);
        r.drawText({x0_, titleY_}, "Les droits des admins", gfx::semibold(kF28), c.text);
        const bool plus = ws().role() == Role::Plus;
        r.drawText({x0_, titleY_ + 40.f},
                   fit(r, plus ? "Des profils que tu cr\xC3\xA9" "es, copi\xC3\xA9s vers chaque admin, puis ajust\xC3\xA9s admin par admin \xC2\xB7 seul l'admin+ les change"
                               : "R\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+ : seul le poste qui a la cl\xC3\xA9 de l'\xC3\xA9" "diteur change les droits des admins.",
                       kF14, back_->bounds().x - 16.f - x0_),
                   kF14, plus ? c.textMuted : c.warning);
        cardBox(r, th, left_);
        cardBox(r, th, mid_);
        cardBox(r, th, right_);
        paintProfiles(ctx);
        paintProfile(ctx);
        paintAdmin(ctx);
        if (const Rect b = bounds(); content_ > b.h + 1.f) {   // 1.47.0 : la page defile - sa barre
            const float th2 = std::max(30.f, b.h * b.h / content_);
            r.fillRoundedRect({b.right() - 6.f, b.y + (b.h - th2) * (scroll_ / std::max(1.f, content_ - b.h)), 4.f, th2}, c.textMuted.withAlpha(110), 2.f);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseWheel>(&ev); m && content_ > bounds().h + 1.f && bounds().contains(m->pos)) {   // 1.47.0
            scroll_ = std::clamp(scroll_ - m->dy * 48.f, 0.f, content_ - bounds().h);
            invalidateLayout();
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
            for (std::size_t i = 0; i < profileRects_.size() && i < profiles_.size(); ++i)
                if (profileRects_[i].contains(d->pos)) {
                    choose(profiles_[i].name);
                    return ui::EventResult::Consumed;
                }
            if (panel_->visible() && !panel_->bounds().contains(d->pos) && !copyTo_->bounds().contains(d->pos)) openCopy(false);
        }
        return ui::EventResult::Ignored;
    }

private:
    static std::size_t indexOf(const std::string& right) {
        const auto& all = adm::rights();
        for (std::size_t i = 0; i < all.size(); ++i)
            if (right == all[i].id) return i;
        return 0;
    }
    [[nodiscard]] const adm::Profile* profileOf(const std::string& name) const {
        for (const auto& p : profiles_)
            if (p.name == name) return &p;
        return nullptr;
    }
    [[nodiscard]] const adm::Admin* adminOf(const std::string& key) const {
        for (const auto& a : admins_)
            if (a.key == key) return &a;
        return nullptr;
    }
    [[nodiscard]] std::string adminProfileName() const {
        const adm::Admin* a = adminOf(adminKey_);
        return a ? a->profile : std::string();
    }
    [[nodiscard]] std::vector<const adm::Admin*> usersOf(const std::string& profile) const {
        std::vector<const adm::Admin*> out;
        for (const auto& a : admins_)
            if (a.profile == profile) out.push_back(&a);
        return out;
    }
    [[nodiscard]] int colorOf(const std::string& key) const {
        int i = 1;
        if (ws().adminList())
            for (const auto& a : ws().adminList()->admins) {
                if (!a.active()) continue;
                if (a.key == key) return i;
                ++i;
            }
        return 1;
    }
    [[nodiscard]] static std::vector<std::string> sorted(const std::set<std::string>& s) { return adm::normalizeRights(std::vector<std::string>(s.begin(), s.end())); }

    void dirtyReset() {
        const adm::Profile* p = profileOf(selected_);
        draft_.clear();
        if (p) draft_.insert(p->rights.begin(), p->rights.end());
        dirty_ = false;
    }
    void adminReset() {
        const adm::Admin* a = adminOf(adminKey_);
        adminDraft_.clear();
        if (a) adminDraft_.insert(a->rights.begin(), a->rights.end());
        adminDirty_ = false;
    }

    void choose(const std::string& name) {
        if (name == selected_) return;
        if (dirty_) host_.flash("Le profil \xC2\xAB " + selected_ + " \xC2\xBB n'\xC3\xA9tait pas enregistr\xC3\xA9 : ses changements sont oubli\xC3\xA9s.", Tone::Warning);
        selected_ = name;
        dirtyReset();
        syncing_ = true;
        for (std::size_t i = 0; i < profiles_.size(); ++i)
            if (profiles_[i].name == selected_) table_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
        syncing_ = false;
        if (panel_->visible()) openCopy(true);
        update();
    }

    // Les interrupteurs, les boutons : ce que le brouillon permet.
    void update() {
        const bool plus = ws().role() == Role::Plus;
        const bool busy = host_.busy && host_.busy();
        const bool ready = ws().service().ready();
        const adm::Profile* p = profileOf(selected_);
        const bool builtin = !p || p->builtin;
        dirty_ = p && sorted(draft_) != adm::normalizeRights(p->rights);
        const adm::Admin* a = adminOf(adminKey_);
        adminDirty_ = a && sorted(adminDraft_) != adm::normalizeRights(a->rights);
        syncing_ = true;
        const auto& all = adm::rights();
        for (std::size_t i = 0; i < all.size(); ++i) {
            profileSw_[i]->setChecked(draft_.count(all[i].id) != 0);
            profileSw_[i]->setEnabled(plus && !builtin);
            adminSw_[i]->setChecked(adminDraft_.count(all[i].id) != 0);
            adminSw_[i]->setEnabled(plus && a != nullptr);
            show(adminSw_[i], a != nullptr);
        }
        syncing_ = false;
        newProfile_->setEnabled(plus);
        duplicate_->setEnabled(plus && p);
        rename_->setEnabled(plus && !builtin);
        delete_->setEnabled(plus && !builtin);
        rename_->setTooltip(builtin ? std::string("Le profil \xC2\xAB Tous les droits \xC2\xBB ne se renomme pas.") : std::string("Changer le nom du profil."));
        delete_->setTooltip(builtin ? std::string("Le profil \xC2\xAB Tous les droits \xC2\xBB ne se supprime pas.")
                                    : std::string("Ses admins gardent leurs droits (une copie, pas un lien)."));
        save_->setEnabled(plus && !builtin && dirty_);
        cancel_->setEnabled(dirty_);
        copyTo_->setEnabled(plus && p && !admins_.empty() && ready && !busy && !dirty_);
        copyTo_->setTooltip(!ready ? std::string("Il faut le service des licences : la nouvelle liste des admins y est publi\xC3\xA9" "e.")
                            : dirty_ ? std::string("Enregistre d'abord le profil : c'est lui qui est copi\xC3\xA9.")
                            : admins_.empty() ? std::string("Aucun admin actif pour l'instant (Administrateurs \xE2\x80\xBA Ajouter un admin\xE2\x80\xA6).")
                                              : std::string("Ses droits deviennent ceux du profil, pour l'admin choisi (une copie)."));
        admin_->setEnabled(!admins_.empty());
        recopy_->setEnabled(plus && a && profileOf(a->profile) != nullptr);
        adminCancel_->setEnabled(adminDirty_);
        publish_->setEnabled(plus && adminDirty_ && ready && !busy);
        publish_->setTooltip(!ready ? std::string("Il faut le service des licences : la nouvelle liste des admins y est publi\xC3\xA9" "e.")
                                    : std::string("La cl\xC3\xA9 de l'\xC3\xA9" "diteur signe la liste des admins avec ses droits, puis la publie."));
        if (!plus)   // 1.52.0 : XPG Admin sous derogation - les droits et les profils se lisent ; la cle de l'editeur les signe
            for (ui::Button* b : {newProfile_, duplicate_, rename_, delete_, save_, copyTo_, recopy_, publish_}) b->setTooltip(classic::kEditorKeyOnly);
        if (panel_->visible()) {
            std::size_t n = 0;
            std::string one;
            for (std::size_t i = 0; i < copyBoxes_.size(); ++i)
                if (copyBoxes_[i]->isChecked()) {
                    ++n;
                    one = i < copyKeys_.size() && adminOf(copyKeys_[i]) ? adminOf(copyKeys_[i])->name : std::string();
                }
            copyOk_->setText(n == 1 ? "Copier vers " + one : n == 0 ? std::string("Copier") : "Copier vers " + std::to_string(n) + " admins");
            copyOk_->setEnabled(n > 0 && !busy);
        }
        invalidateLayout();
        invalidate();
    }

    // ------------------------------------------------------------------ les gestes
    void askNew() {
        auto alive = alive_;
        host_.openDialog(makeNameDialog("nouveau-profil", "Nouveau profil", "Son nom (\xC2\xAB Admin LV1 \xC2\xBB) ; il commence sans aucun droit.", "", "Cr\xC3\xA9" "er",
                                        [this, alive](const std::string& name) {
                                            if (!*alive) return false;
                                            if (profileOf(name)) {
                                                host_.report(OpResult::fail("un profil \xC2\xAB " + name + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0"));
                                                return false;
                                            }
                                            adm::Profile p;
                                            p.name = name;
                                            const OpResult r = ws().saveProfile(p);
                                            if (r.ok) {
                                                selected_ = name;
                                                dirty_ = false;
                                            }
                                            host_.report(r);
                                            if (*alive) refresh();
                                            return r.ok;
                                        }));
    }

    void duplicate() {
        const adm::Profile* p = profileOf(selected_);
        if (!p) return;
        std::string name = p->name + " (copie)";
        for (int i = 2; profileOf(name) && i < 100; ++i) name = p->name + " (copie " + std::to_string(i) + ")";
        adm::Profile q;
        q.name = name;
        q.rights = sorted(draft_);
        const OpResult r = ws().saveProfile(q);
        if (r.ok) {
            selected_ = name;
            dirty_ = false;
        }
        host_.report(r);
        refresh();
    }

    void askRename() {
        const adm::Profile* p = profileOf(selected_);
        if (!p || p->builtin) return;
        const std::string from = p->name;
        auto alive = alive_;
        host_.openDialog(makeNameDialog("renommer-profil", "Renommer le profil", "Le nouveau nom de \xC2\xAB " + from + " \xC2\xBB.", from, "Renommer",
                                        [this, alive, from](const std::string& to) {
                                            if (!*alive) return false;
                                            const OpResult r = ws().renameProfile(from, to);
                                            if (r.ok) selected_ = to;
                                            host_.report(r);
                                            if (*alive) refresh();
                                            return r.ok;
                                        }));
    }

    void askDelete() {
        const adm::Profile* p = profileOf(selected_);
        if (!p || p->builtin) return;
        const std::string name = p->name;
        auto alive = alive_;
        host_.openDialog(licdlg::makeConfirmDialog(
            "supprimer-profil", "Supprimer le profil",
            "Supprimer le profil \xC2\xAB " + name + " \xC2\xBB ?\n\nSes admins gardent leurs droits : un profil est copi\xC3\xA9 vers un admin, ce n'est pas un lien.",
            "Supprimer", true, [this, alive, name] {
                if (!*alive) return;
                const OpResult r = ws().deleteProfile(name);
                if (r.ok) {
                    selected_.clear();
                    dirty_ = false;
                }
                host_.report(r);
                if (*alive) refresh();
            }));
    }

    void saveProfile() {
        const adm::Profile* p = profileOf(selected_);
        if (!p || p->builtin || !dirty_) return;
        adm::Profile q;
        q.name = p->name;
        q.rights = sorted(draft_);
        const OpResult r = ws().saveProfile(q);
        if (r.ok) dirty_ = false;
        host_.report(r);
        refresh();
    }

    void openCopy(bool open) {
        for (auto* cb : copyBoxes_) panel_->removeChild(*cb);
        copyBoxes_.clear();
        copyKeys_.clear();
        panel_->sub.clear();
        if (!open) {
            panel_->setVisibility(ui::Visibility::Collapsed);
            update();
            return;
        }
        panel_->title = "COPIER \xC2\xAB " + selected_ + " \xC2\xBB VERS";
        for (std::size_t i = 0; i < admins_.size(); ++i) {
            auto cb = std::make_unique<ui::Checkbox>(admins_[i].name, "droits.copier." + std::to_string(i + 1));
            if (admins_[i].key == adminKey_) cb->setState(ui::Checkbox::State::Checked);
            links_ += cb->stateChanged->connect([this](ui::Checkbox::State) { update(); });
            copyBoxes_.push_back(&static_cast<ui::Checkbox&>(panel_->addChild(std::move(cb))));
            copyKeys_.push_back(admins_[i].key);
            panel_->sub.push_back(admins_[i].pc + (admins_[i].profile.empty() ? std::string() : " \xC2\xB7 " + admins_[i].profile));
        }
        panel_->setVisibility(ui::Visibility::Visible);
        update();
    }

    void copyNow() {
        std::vector<std::string> keys;
        for (std::size_t i = 0; i < copyBoxes_.size() && i < copyKeys_.size(); ++i)
            if (copyBoxes_[i]->isChecked()) keys.push_back(copyKeys_[i]);
        if (keys.empty() || selected_.empty()) return;
        const Workspace* w = &ws();
        const std::string profile = selected_;
        auto alive = alive_;
        if (!host_.runSlow("Copier le profil " + profile, [w, profile, keys] { return w->copyProfileTo(profile, keys); },
                           [this, alive](const OpResult& r) {
                               if (!*alive) return;
                               if (r.ok) {
                                   adminDirty_ = false;
                                   openCopy(false);
                               }
                           })) {
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
            return;
        }
    }

    void publish() {
        const adm::Admin* a = adminOf(adminKey_);
        if (!a || !adminDirty_) return;
        const Workspace* w = &ws();
        const std::string key = a->key, profile = a->profile;
        const std::vector<std::string> rights = sorted(adminDraft_);
        auto alive = alive_;
        if (!host_.runSlow("Droits de " + a->name, [w, key, rights, profile] { return w->setAdminRights(key, rights, profile); },
                           [this, alive](const OpResult& r) {
                               if (*alive && r.ok) {
                                   adminDirty_ = false;
                                   refresh();
                               }
                           }))
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
    }

    // ------------------------------------------------------------------ dessiner
    void paintProfiles(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const float x = left_.x + 18.f, w = left_.w - 36.f;
        r.drawText({x, left_.y + 20.f}, "Profils", gfx::semibold(kF16), c.text);
        {   // 1.47.0 : sur deux lignes si la colonne est etroite
            const std::string intro = "Un profil, c'est une liste de fonctions.";
            if (r.measure(intro, kF12).width <= w) r.drawText({x, left_.y + 58.f}, intro, kF12, c.textMuted);
            else (void)drawWrapped(r, x, left_.y + 52.f, w, intro, kF12, c.textMuted, 15.f, 2);
        }
        r.fillRect({left_.x + 1.f, left_.y + 84.f, left_.w - 2.f, 1.f}, c.gridLine);
        profileRects_.clear();
        float y = left_.y + 96.f;
        const float rowH = 92.f;
        const float limit = left_.bottom() - 60.f;
        for (const auto& p : profiles_) {
            const Rect row{left_.x + 10.f, y, left_.w - 20.f, rowH - 8.f};
            profileRects_.push_back(row);
            if (row.bottom() > limit) {
                y += rowH;
                continue;
            }
            const bool sel = p.name == selected_;
            if (sel) {
                r.fillRoundedRect(row, c.accent.withAlpha(16), 10.f);
                ui::shapes::strokeRoundedRect(r, row, c.accent.withAlpha(70), 10.f);
            }
            const Rect ib{row.x + 10.f, row.y + 12.f, 32.f, 32.f};
            r.fillRoundedRect(ib, sel ? c.accent : c.rowAltBg, 9.f);
            ui::drawIcon(r, p.builtin ? ui::Icon::Star : ui::Icon::Layers, {ib.x + 8.f, ib.y + 8.f, 16.f, 16.f}, sel ? rgb(0xFFFFFF) : c.textMuted);
            const float nx = ib.right() + 12.f;
            const FontId fn = gfx::semibold(kF14);
            // 1.47.0 : << de depart >> seulement si le nom tient a cote (l'etoile le dit aussi)
            const bool tag = p.builtin && r.measure(p.name, fn).width <= row.right() - nx - 76.f;
            const std::string nm = fit(r, p.name, fn, row.right() - nx - (tag ? 76.f : 8.f));
            r.drawText({nx, row.y + 10.f}, nm, fn, c.text);
            if (tag) (void)pill(r, nx + r.measure(nm, fn).width + 8.f, row.y + 9.f, "de d\xC3\xA9part", gfx::semibold(kF11), c.textMuted, c.rowAltBg, 18.f);
            const auto users = usersOf(p.name);
            const std::string tail = p.builtin && users.empty() ? std::string(" \xC2\xB7 ne se modifie pas") : " \xC2\xB7 " + std::to_string(users.size()) + (users.size() > 1 ? " admins" : " admin");
            std::string sub = std::to_string(p.rights.size()) + " fonctions sur 13" + tail;
            if (r.measure(sub, kF12).width > row.right() - nx - 8.f) sub = std::to_string(p.rights.size()) + "/13" + tail;   // 1.47.0 : etroit
            r.drawText({nx, row.y + 30.f}, fit(r, sub, kF12, row.right() - nx - 8.f), kF12, c.textMuted);
            const Rect track{nx, row.y + 52.f, row.right() - nx - 12.f, 5.f};
            r.fillRoundedRect(track, c.gridLine, 2.5f);
            r.fillRoundedRect({track.x, track.y, std::max(5.f, track.w * static_cast<float>(p.rights.size()) / 13.f), track.h}, c.accent, 2.5f);
            float ax = nx;
            for (const adm::Admin* a : users) {
                if (ax > row.right() - 24.f) break;
                avatar(r, {ax, row.y + 62.f, 16.f, 16.f}, a->name, avatarColor(th, colorOf(a->key)), 5.f);
                ax += 19.f;
            }
            y += rowH;
        }
        (void)drawWrapped(r, x, left_.bottom() - 50.f, w, "Un admin ajout\xC3\xA9 re\xC3\xA7oit le profil que tu choisis (Ajouter un admin, \xC2\xAB Son profil \xC2\xBB).", kF12,
                          c.textMuted, 16.f, 3);
    }

    void paintProfile(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const float x = mid_.x + 18.f, w = mid_.w - 36.f;
        const adm::Profile* p = profileOf(selected_);
        if (!p) {
            r.drawText({x, mid_.y + 22.f}, "Aucun profil.", kF14, c.textMuted);
            return;
        }
        const FontId ft = gfx::semibold(FontId{17});
        const std::string head = fit(r, "Le profil \xC2\xAB " + p->name + " \xC2\xBB", ft, w - 80.f);
        r.drawText({x, mid_.y + 18.f}, head, ft, c.text);
        (void)pill(r, x + r.measure(head, ft).width + 10.f, mid_.y + 18.f, std::to_string(draft_.size()) + " / 13", gfx::semibold(kF12), c.accent, c.accent.withAlpha(22), 22.f);
        std::string sub;
        if (p->builtin) sub = "Toutes les fonctions, ne se modifie pas";
        else sub = p->changed ? "Modifi\xC3\xA9 par toi le " + dateFr(p->changed) : std::string("Jamais modifi\xC3\xA9");
        const auto users = usersOf(p->name);
        if (!users.empty()) {
            std::string names;
            for (std::size_t i = 0; i < users.size(); ++i) names += (i ? ", " : "") + users[i]->name;
            // 1.47.0 : les noms s'ils tiennent, sinon leur nombre
            const std::string tail = dirty_ ? std::string(" \xC2\xB7 pas encore enregistr\xC3\xA9") : std::string();
            sub += r.measure(sub + " \xC2\xB7 utilis\xC3\xA9 par " + names + tail, kF13).width <= w
                       ? " \xC2\xB7 utilis\xC3\xA9 par " + names
                       : " \xC2\xB7 " + std::to_string(users.size()) + (users.size() > 1 ? " admins" : " admin");
        }
        if (dirty_) sub += " \xC2\xB7 pas encore enregistr\xC3\xA9";
        r.drawText({x, mid_.y + 46.f}, fit(r, sub, kF13, w), kF13, dirty_ ? c.warning : c.textMuted);
        // L'en-tete du tableau
        const float hy = tableTop_;
        r.fillRect({mid_.x + 1.f, hy - 6.f, mid_.w - 2.f, 1.f}, c.gridLine);
        const FontId fh = gfx::semibold(kF11);
        const float colBy = x + w * 0.56f;
        r.drawText({x, hy + 6.f}, "FONCTION", fh, c.textMuted);
        r.drawText({colBy, hy + 6.f}, "QUI LA V\xC3\x89RIFIE", fh, c.textMuted);
        r.drawText({x + w - 56.f, hy + 6.f}, "PERMISE", fh, c.textMuted);
        r.fillRect({mid_.x + 1.f, hy + 26.f, mid_.w - 2.f, 1.f}, c.gridLine);
        float y = hy + 28.f;
        for (const char* g : kGroups) {
            r.drawText({x, y + (rowH_ - r.lineHeight(gfx::semibold(kF13))) * 0.5f}, adm::groupLabel(g), gfx::semibold(kF13), c.text);
            y += rowH_;
            for (const auto& ri : adm::rights()) {
                if (std::string(ri.group) != g) continue;
                const float ty = y + (rowH_ - r.lineHeight(kF13)) * 0.5f;
                r.drawText({x, ty}, fit(r, ri.label, kF13, colBy - x - 10.f), kF13, c.text);
                const std::string by = checkedByShort(ri.checkedBy);
                const bool studios = by.find("Studios") != std::string::npos;
                ui::drawIcon(r, studios ? ui::Icon::Ok : ui::Icon::Screen, {colBy, ty + 1.f, 12.f, 12.f}, studios ? c.ok : c.textMuted);
                r.drawText({colBy + 16.f, ty + 1.f}, fit(r, by, kF12, x + w - 60.f - colBy - 16.f), kF12, studios ? c.ok : c.textMuted);
                y += rowH_;
            }
        }
        if (plusRows_) {
            y += 4.f;
            r.fillRect({mid_.x + 1.f, y, mid_.w - 2.f, 1.f}, c.gridLine);
            y += 6.f;
            r.drawText({x, y}, "Toujours \xC3\xA0 toi, l'admin+", gfx::semibold(kF13), c.text);
            y += 22.f;
            const FontId fp = gfx::semibold(kF11);
            const std::string tag = "r\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+";
            const float tw = pillWidth(r, tag, fp);
            for (const auto& s : plusOnly()) {
                ui::drawIcon(r, ui::Icon::Lock, {x, y + 1.f, 13.f, 13.f}, c.textDisabled);
                r.drawText({x + 20.f, y}, fit(r, s, kF12, w - 30.f - tw), kF12, c.textMuted);
                (void)pill(r, x + w - tw, y - 2.f, tag, fp, c.accent, c.accent.withAlpha(22), 18.f);
                y += 20.f;
            }
        }
        r.fillRect({mid_.x + 1.f, delete_->bounds().y - 12.f, mid_.w - 2.f, 1.f}, c.gridLine);
        // 1.44.0 : la note seulement si elle tient (sinon : << s... g... >> ; la fenetre de Supprimer le dit aussi)
        const float room = cancel_->bounds().x - delete_->bounds().right() - 20.f;
        if (room >= r.measure("gardent leurs droits", kF12).width) {
            r.drawText({delete_->bounds().right() + 12.f, delete_->bounds().y + 2.f}, "ses admins", kF12, c.textMuted);
            r.drawText({delete_->bounds().right() + 12.f, delete_->bounds().y + 18.f}, "gardent leurs droits", kF12, c.textMuted);
        }
    }

    void paintAdmin(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const float x = right_.x + 18.f, w = right_.w - 36.f;
        const adm::Admin* a = adminOf(adminKey_);
        if (!a) {
            (void)drawWrapped(r, x, right_.y + 64.f, w,
                              "Aucun admin actif pour l'instant. Ajoute-en un (Administrateurs \xE2\x80\xBA Ajouter un admin\xE2\x80\xA6) : il re\xC3\xA7oit le profil que tu choisis.",
                              kF13, c.textMuted, 19.f, 4);
            return;
        }
        float y = right_.y + 60.f;
        avatar(r, {x, y, 40.f, 40.f}, a->name, avatarColor(th, colorOf(a->key)), 11.f);
        const FontId fn = gfx::semibold(kF16);
        const std::string nm = fit(r, a->name, fn, w - 120.f);
        r.drawText({x + 52.f, y + 1.f}, nm, fn, c.text);
        (void)pill(r, x + 52.f + r.measure(nm, fn).width + 8.f, y, "Admin", gfx::semibold(kF11), c.textMuted, c.rowAltBg, 20.f);
        if (const std::string line = a->pc + " \xC2\xB7 admin depuis le " + dateFr(a->since); r.measure(line, kF12).width <= w - 52.f) {
            r.drawText({x + 52.f, y + 22.f}, line, kF12, c.textMuted);
        } else {   // 1.47.0 : sur deux lignes
            r.drawText({x + 52.f, y + 21.f}, fit(r, a->pc, kF12, w - 52.f), kF12, c.textMuted);
            r.drawText({x + 52.f, y + 36.f}, fit(r, "admin depuis le " + dateFr(a->since), kF12, w - 52.f), kF12, c.textMuted);
        }
        y += 56.f;
        // L'origine : son profil, et combien de differences.
        const adm::Profile* origin = profileOf(a->profile);
        int diffs = 0;
        if (origin)
            for (const auto& ri : adm::rights()) {
                const bool inP = std::find(origin->rights.begin(), origin->rights.end(), ri.id) != origin->rights.end();
                if (inP != (adminDraft_.count(ri.id) != 0)) ++diffs;
            }
        const Rect box{x, y, w, 46.f};
        r.fillRoundedRect(box, c.rowAltBg, 10.f);
        ui::drawIcon(r, ui::Icon::Layers, {box.x + 10.f, box.y + 15.f, 15.f, 15.f}, c.textMuted);
        const float tw = recopy_->bounds().x - box.x - 40.f;
        if (a->profile.empty()) r.drawText({box.x + 32.f, box.y + 14.f}, fit(r, "Aucun profil d'origine", kF12, tw), kF12, c.textMuted);
        else {
            const std::string from = "Copi\xC3\xA9s de " + a->profile + (origin ? std::string() : " (supprim\xC3\xA9)");
            r.drawText({box.x + 32.f, box.y + 6.f}, fit(r, r.measure(from, gfx::semibold(kF12)).width <= tw ? from : a->profile, gfx::semibold(kF12), tw), gfx::semibold(kF12), c.text);
            r.drawText({box.x + 32.f, box.y + 24.f}, diffs ? std::to_string(diffs) + (diffs > 1 ? " ajustements" : " ajustement") : std::string("aucun ajustement"), kF12,
                       diffs ? c.accent : c.textMuted);
        }
        // Ses droits, les differences marquees.
        float yy = adminTop_;
        const std::string who = a->name.substr(0, a->name.find(' '));
        for (const char* g : kGroups) {
            r.drawText({x, yy + (adminRowH_ - r.lineHeight(gfx::semibold(kF13))) * 0.5f}, adm::groupLabel(g), gfx::semibold(kF13), c.text);
            yy += adminRowH_;
            for (const auto& ri : adm::rights()) {
                if (std::string(ri.group) != g) continue;
                const bool has = adminDraft_.count(ri.id) != 0;
                const bool inP = origin && std::find(origin->rights.begin(), origin->rights.end(), ri.id) != origin->rights.end();
                std::string mark;
                if (origin && inP && !has) mark = "retir\xC3\xA9 pour " + who;
                else if (origin && !inP && has) mark = "ajout\xC3\xA9 pour " + who;
                if (!mark.empty()) r.fillRect({right_.x + 1.f, yy, right_.w - 2.f, adminRowH_}, rgb(0xFFF7E6));
                const float ty = yy + (adminRowH_ - r.lineHeight(kF13)) * 0.5f;
                float mw = 0.f;
                if (!mark.empty()) {
                    const FontId fm = gfx::semibold(kF11);
                    mw = pillWidth(r, mark, fm) + 8.f;
                    (void)pill(r, x + w - 50.f - mw, yy + (adminRowH_ - 18.f) * 0.5f, mark, fm, rgb(0xC2410C), rgb(0xFFEDD5), 18.f);
                }
                r.drawText({x, ty}, fit(r, ri.label, kF13, w - 60.f - mw), kF13, c.text);
                yy += adminRowH_;
            }
        }
        const Rect note{x, std::min(publish_->bounds().y, adminCancel_->bounds().y) - 90.f, w, 78.f};   // 1.47.0 : au-dessus des deux boutons
        r.fillRoundedRect(note, c.accent.withAlpha(16), 10.f);
        (void)drawWrapped(r, note.x + 12.f, note.y + 8.f, note.w - 24.f,
                          "En enregistrant, la cl\xC3\xA9 de l'\xC3\xA9" "diteur signe la liste des admins avec ses droits ; le service et les Studios l'ont dans la minute. "
                          "\xC3\x89" "crit dans le journal (Critique).",
                          kF12, c.accent, 16.f, 4);
    }

    // --------------------------------------------------------------------- l'etat
    std::vector<adm::Profile>   profiles_;
    std::vector<adm::Admin>     admins_;
    std::string                 selected_, adminKey_;
    std::set<std::string>       draft_, adminDraft_;
    bool                        dirty_{false}, adminDirty_{false}, syncing_{false};
    std::vector<Rect>           profileRects_;
    std::map<std::string, float> rowY_, adminRowY_;
    float                       scroll_{0.f}, content_{0.f};   // 1.47.0 : une fenetre basse, la page defile
    // les widgets
    ui::Button*                 back_{nullptr};
    ui::Button*                 newProfile_{nullptr};
    ui::Button*                 duplicate_{nullptr};
    ui::Button*                 rename_{nullptr};
    ui::Button*                 copyTo_{nullptr};
    ui::Button*                 delete_{nullptr};
    ui::Button*                 cancel_{nullptr};
    ui::Button*                 save_{nullptr};
    ui::DropDown*               admin_{nullptr};
    ui::Button*                 recopy_{nullptr};
    ui::Button*                 adminCancel_{nullptr};
    ui::Button*                 publish_{nullptr};
    std::vector<Switch*>        profileSw_, adminSw_;
    ui::TableView*              table_{nullptr};
    CopyPanel*                  panel_{nullptr};
    ui::Button*                 copyOk_{nullptr};
    ui::Button*                 copyClose_{nullptr};
    std::vector<ui::Checkbox*>  copyBoxes_;
    std::vector<std::string>    copyKeys_;
    // la mise en page
    float                       x0_{0.f}, w_{0.f}, titleY_{0.f}, tableTop_{0.f}, rowH_{26.f}, adminTop_{0.f}, adminRowH_{26.f};
    bool                        plusRows_{true};
    Rect                        left_{}, mid_{}, right_{};
    std::shared_ptr<bool>       alive_ = std::make_shared<bool>(true);
    core::ConnectionScope       links_;
};

} // namespace

PagePtr makeRightsPage(ShellHost host) { return std::make_unique<RightsPage>(std::move(host)); }

} // namespace license_admin::gui
