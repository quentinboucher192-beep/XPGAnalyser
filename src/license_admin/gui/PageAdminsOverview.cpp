// =============================================================================
//  license_admin/gui/PageAdminsOverview.cpp - 1.47.0 : ADMINISTRATEURS, LA VUE
//  D'ENSEMBLE (maquette 1.47.0, planche 01)
// -----------------------------------------------------------------------------
//  Sous l'en-tete de la page (la periode, les onglets : PageAdmins.cpp) :
//    - SIX TUILES : les admins (actifs, en attente, retires), leurs actions (et
//      l'ecart a la periode d'avant), les demandes en attente (le service), les
//      licences signees (les admins et toi), le montant encaisse, les alertes ;
//    - LE CLASSEMENT : la barre de filtres (ui::FilterBar : chercher un admin, un
//      PC, une cle ; Etat, Profil, Version de l'Admin), les colonnes triees d'un
//      clic sur leur titre, une ligne par admin (ses actions et leur courbe, ses
//      licences, ses demandes, son delai, son montant, sa derniere action, son
//      etat) ; LA LIGNE DEPLIEE (un clic) : ses chiffres, sa courbe de la
//      periode et celle d'avant en gris, ses 5 dernieres actions, ses droits (un
//      droit jamais utilise : en ambre), Ouvrir sa fiche (double-clic, Entree),
//      Son journal, Ses droits... ; le clic droit (ui::ListMenu : l'ordre de
//      toutes les listes) ; une demande recue : << Verifier et accepter... >> ;
//    - LES ALERTES (JournalStats) : chacune avec son geste, puis << Rien a
//      signaler >> pour ce qui va bien.
// =============================================================================
#include "AdminsBoard.hpp"

#include "../../ui/widgets/FilterBar.hpp"
#include "../../ui/widgets/ListMenu.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace license_admin::gui::adminsboard {

namespace {

constexpr FontId kF11{11}, kF12{12}, kF13{13}, kF14{14};
const FontId kB12 = gfx::semibold(kF12), kB13 = gfx::semibold(kF13), kB14 = gfx::semibold(kF14);

const char* const kHeads[8] = {"ADMIN", "ACTIONS", "LICENCES", "DEMANDES", "D\xC3\x89LAI MOY.", "MONTANT", "DERNI\xC3\x88RE ACTION", "\xC3\x89TAT"};
const float kColShare[8] = {0.21f, 0.17f, 0.075f, 0.095f, 0.09f, 0.11f, 0.16f, 0.09f};

std::string stateOf(const Person& p) {
    if (p.kind == Person::Kind::Pending) return "En attente";
    if (p.retired) return "Retir\xC3\xA9";
    return "Actif";
}

// Un texte en deux morceaux : le debut en gras (<< Hugo Essai >>), la suite (<< : 4 actions... >>).
struct Rich {
    std::string text;
    FontId      font;
    Color       color;
};
// Couper en lignes des morceaux de styles differents ; rend la hauteur.
float paintRich(gfx::IRenderer* r, float x, float y, float w, const std::vector<Rich>& parts, float lineH) {
    float cx = x, cy = y;
    for (const auto& p : parts) {
        std::size_t i = 0;
        while (i < p.text.size()) {
            std::size_t j = p.text.find(' ', i);
            const std::string word = p.text.substr(i, j == std::string::npos ? std::string::npos : j - i + 1);
            i = j == std::string::npos ? p.text.size() : j + 1;
            const float ww = ui::measureWidth(word, p.font);
            if (cx + ww > x + w + 0.5f && cx > x) {
                cx = x;
                cy += lineH;
            }
            if (r) r->drawText({cx, cy}, word, p.font, p.color);
            cx += ww;
        }
    }
    return cy - y + lineH;
}

class OverviewView final : public ScrolledView {
public:
    struct RowGeo {
        int  person{-1};
        Rect row{}, panel{};
        bool unfolded{false};
    };
    struct AlertGeo {
        float top{0}, bottom{0};
    };
    OverviewView(std::shared_ptr<Data> data, Actions actions, ShellHost host)
        : ScrolledView("admins.vue"), d_(std::move(data)), a_(std::move(actions)), host_(std::move(host)) {
        setFocusPolicy(true);
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> fields{
            {.title = "Admin", .kind = K::Text},
            {.title = "\xC3\x89tat", .kind = K::Values, .chip = true, .noun = "un \xC3\xA9tat", .order = {"Actif", "En attente", "Retir\xC3\xA9"}},
            {.title = "Profil", .kind = K::Values, .chip = true, .noun = "un profil"},
            {.title = "Version de l'Admin", .kind = K::Values, .chip = true, .noun = "une version"},
            {.title = "PC", .kind = K::Text},
        };
        bar_ = &adoptIn(*this, std::make_unique<ui::FilterBar>("admins.barre", std::move(fields), "admins.recherche"));
        bar_->setNoun("admins");
        bar_->searchField().setPlaceholder("Chercher un admin, un PC, une cl\xC3\xA9\xE2\x80\xA6");
        ui::FilterBar::Source src;
        src.rows = [this] { return d_->people.size(); };
        src.text = [this](std::size_t row, std::size_t field) { return cellText(row, field); };
        src.extra = [this](std::size_t row) {
            if (row >= d_->people.size()) return std::string();
            const Person& p = d_->people[row];
            return p.key + " " + p.fingerprint + " " + p.id;
        };
        bar_->setSource(std::move(src));
        links_ += bar_->changed->connect([this] {
            invalidateLayout();
            invalidate();
        });
        curve_ = &adoptIn(*this, std::make_unique<ch::TimeChart>("admins.deplie.courbe"));
        allJournal_ = &adoptIn(*this, std::make_unique<LinkButton>("Tout son journal", "admins.deplie.journal"));
        open_ = &makeButton(*this, "Ouvrir sa fiche", "admins.fiche", ui::Button::Style::Primary, ui::Icon::User);
        journal_ = &makeButton(*this, "Son journal", "admins.voir-journal", ui::Button::Style::Default, ui::Icon::History);
        rights_ = &makeButton(*this, "Ses droits\xE2\x80\xA6", "admins.droits", ui::Button::Style::Default, ui::Icon::Lock);
        retire_ = &makeButton(*this, "Retirer\xE2\x80\xA6", "admins.retirer", ui::Button::Style::Danger, ui::Icon::User);
        verify_ = &makeButton(*this, "V\xC3\xA9rifier et accepter\xE2\x80\xA6", "admins.detail.verifier", ui::Button::Style::Primary);
        refuse_ = &makeButton(*this, "Refuser la demande", "admins.detail.refuser", ui::Button::Style::Danger);
        regler_ = &adoptIn(*this, std::make_unique<LinkButton>("R\xC3\xA9gler les alertes\xE2\x80\xA6", "admins.alertes.regler", false));
        menu_ = &adoptIn(*this, std::make_unique<ui::PopupMenu>("admins.menu"));
        links_ += menu_->itemChosen->connect([this](int id) { (void)menuList_.run(id); });
        links_ += open_->clicked->connect([this] { openSelected(); });
        links_ += journal_->clicked->connect([this] {
            if (a_.journalOf && !selected_.empty()) a_.journalOf(selected_);
        });
        links_ += allJournal_->clicked->connect([this] {
            if (a_.journalOf && !selected_.empty()) a_.journalOf(selected_);
        });
        links_ += rights_->clicked->connect([this] {
            if (a_.rightsOf && !selected_.empty()) a_.rightsOf(selected_);
        });
        links_ += retire_->clicked->connect([this] {
            if (a_.retire && !selected_.empty()) a_.retire(selected_);
        });
        links_ += verify_->clicked->connect([this] {
            if (a_.verify && !selected_.empty()) a_.verify(selected_);
        });
        links_ += refuse_->clicked->connect([this] {
            if (a_.refuse && !selected_.empty()) a_.refuse(selected_);
        });
        links_ += regler_->clicked->connect([this] {
            if (a_.criticality) a_.criticality();
        });
        refreshData();
    }

    // ---- la page, les essais ----
    void select(const std::string& key) {
        if (selected_ == key) return;
        selected_ = key;
        fillCurve();
        invalidateLayout();
        invalidate();
    }
    [[nodiscard]] const std::string& selected() const noexcept { return selected_; }
    void sortBy(int col) {
        if (col == sortCol_) sortDesc_ = !sortDesc_;
        else {
            sortCol_ = col;
            sortDesc_ = col != 0;   // le nom : de A a Z ; un chiffre : le plus grand d'abord
        }
        invalidateLayout();
        invalidate();
    }
    [[nodiscard]] std::vector<std::string> order() const {
        std::vector<std::string> out;
        for (int i : sortedRows()) out.push_back(d_->people[static_cast<std::size_t>(i)].key);
        return out;
    }
    void refreshData() override {
        bar_->refreshCounts();
        // une ligne par demande : << Verifier et accepter... >>
        std::size_t pending = 0;
        for (const auto& p : d_->people) pending += p.kind == Person::Kind::Pending ? 1 : 0;
        while (verifyRow_.size() < pending) {
            const std::size_t n = verifyRow_.size() + 1;
            auto& v = adoptIn(*this, std::make_unique<LinkButton>("V\xC3\xA9rifier et accepter\xE2\x80\xA6", "admins.verifier." + std::to_string(n)));
            links_ += v.clicked->connect([this, n] {
                std::size_t k = 0;
                for (const auto& p : d_->people)
                    if (p.kind == Person::Kind::Pending && ++k == n) {
                        selected_ = p.key;
                        if (a_.verify) a_.verify(p.key);
                        return;
                    }
            });
            verifyRow_.push_back(&v);
        }
        // un geste par alerte
        while (gestures_.size() < d_->ov.alerts.size()) {
            const std::size_t n = gestures_.size();
            auto& g = adoptIn(*this, std::make_unique<LinkButton>("", "admins.alerte." + std::to_string(n + 1)));
            links_ += g.clicked->connect([this, n] { gesture(n); });
            gestures_.push_back(&g);
        }
        for (std::size_t i = 0; i < gestures_.size(); ++i)
            gestures_[i]->setText(i < d_->ov.alerts.size() ? d_->ov.alerts[i].gesture : std::string());
        fillCurve();
        invalidateLayout();
        invalidate();
    }
    // Le clic droit sur une ligne (les essais : ses libelles dans l'ordre ; run : un libelle).
    [[nodiscard]] ui::ListMenu menuFor(const std::string& key) const { return buildMenu(key); }

protected:
    // ======================================================================== la mise en page
    float place(const Rect& area) override {
        const float x0 = area.x + 2.f, w = area.w - 4.f;
        float y = area.y + 4.f;
        // ---- les tuiles ----
        const int perRow = w >= 900.f ? 6 : 3;
        const float gap = 12.f;
        const float tw = (w - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow);
        tiles_.clear();
        for (int i = 0; i < 6; ++i) {
            const int row = i / perRow, col = i % perRow;
            tiles_.push_back({x0 + static_cast<float>(col) * (tw + gap), y + static_cast<float>(row) * (108.f + gap), tw, 108.f});
        }
        y += (perRow == 6 ? 1.f : 2.f) * (108.f + gap) + 4.f;
        // ---- le classement et les alertes ----
        const bool side = w >= 1040.f;
        const float alertsW = side ? std::clamp(w * 0.27f, 290.f, 380.f) : w;
        const float rankW = side ? w - alertsW - 16.f : w;
        rank_ = {x0, y, rankW, 0.f};
        float ry = y + 14.f;
        const float barH = bar_->heightFor(rankW - 28.f);
        put(bar_, {x0 + 14.f, ry, rankW - 28.f, barH});
        ry += barH + 12.f;
        headY_ = ry;
        const float inner = rankW - 28.f;
        float cx = x0 + 14.f;
        for (int c = 0; c < 8; ++c) {
            colX_[c] = cx + (c == 0 ? 26.f : 0.f);
            colW_[c] = inner * kColShare[c] - (c == 0 ? 26.f : 0.f);
            cx += inner * kColShare[c];
        }
        ry += 30.f;
        rows_.clear();
        const auto shownRows = sortedRows();
        std::size_t pendingSeen = 0;
        for (auto* v : verifyRow_) v->setVisibility(ui::Visibility::Collapsed);
        bool unfoldedShown = false;
        for (const int i : shownRows) {
            const Person& p = d_->people[static_cast<std::size_t>(i)];
            RowGeo g;
            g.person = i;
            g.row = {x0 + 1.f, ry, rankW - 2.f, 62.f};
            ry += 62.f;
            if (p.kind == Person::Kind::Pending) {
                ++pendingSeen;
                std::size_t n = 0, k = 0;
                for (const auto& q : d_->people) {
                    if (q.kind == Person::Kind::Pending) ++k;
                    if (&q == &p) n = k;
                }
                if (n >= 1 && n <= verifyRow_.size()) {
                    LinkButton* v = verifyRow_[n - 1];
                    const float vw = v->naturalWidth();
                    put(v, {colX_[6], g.row.y + 34.f, std::min(vw, colW_[6] + colW_[7]), 20.f});
                }
            }
            if (p.key == selected_) {
                g.unfolded = true;
                unfoldedShown = true;
                const float ph = placePanel(p, {x0 + 1.f, ry, rankW - 2.f, 0.f});
                g.panel = {x0 + 1.f, ry, rankW - 2.f, ph};
                ry += ph;
            }
            rows_.push_back(g);
        }
        (void)pendingSeen;
        if (!unfoldedShown) hidePanel();
        if (rows_.empty()) ry += 70.f;   // << Aucun admin ne correspond >>
        rank_.h = ry + 10.f - rank_.y;
        // ---- les alertes ----
        float ay = side ? y : rank_.bottom() + 16.f;
        const float ax = side ? x0 + rankW + 16.f : x0;
        alerts_ = {ax, ay, alertsW, 0.f};
        ay += 16.f;
        regler_->setVisibility(ui::Visibility::Visible);
        const float rw = regler_->naturalWidth();
        put(regler_, {ax + alertsW - 16.f - rw, ay, rw, 22.f}, d_->journal);
        ay += 36.f;
        alertGeo_.clear();
        const float textW = alertsW - 32.f - 46.f - 60.f;
        for (std::size_t i = 0; i < d_->ov.alerts.size(); ++i) {
            const auto& al = d_->ov.alerts[i];
            AlertGeo g;
            g.top = ay;
            const float th = paintRich(nullptr, ax + 62.f, ay, textW, richOf(al), 18.f);
            ay += th + 4.f;
            LinkButton* gb = i < gestures_.size() ? gestures_[i] : nullptr;
            if (gb) {
                const float gw = std::min(gb->naturalWidth(), textW + 40.f);
                put(gb, {ax + 62.f, ay, gw, 20.f}, !al.gesture.empty());
                ay += al.gesture.empty() ? 0.f : 24.f;
            }
            ay += 12.f;
            g.bottom = ay;
            alertGeo_.push_back(g);
        }
        for (std::size_t i = d_->ov.alerts.size(); i < gestures_.size(); ++i) gestures_[i]->setVisibility(ui::Visibility::Collapsed);
        okY_ = ay + 4.f;
        const auto ok = okLines();
        ay = okY_ + (ok.empty() ? 0.f : 26.f + 20.f * static_cast<float>(ok.size()));
        if (d_->ov.alerts.empty() && ok.empty()) ay += 40.f;
        alerts_.h = ay + 14.f - alerts_.y;
        menu_->setBounds(viewport());
        const float bottom = std::max(rank_.bottom(), alerts_.bottom()) + 16.f;
        return bottom - area.y;
    }

    // Le panneau d'une ligne depliee ; rend sa hauteur.
    float placePanel(const Person& p, const Rect& at) {
        const float x = at.x + 20.f, w = at.w - 40.f;
        float y = at.y + 10.f;
        panelFigs_ = {};
        chartBox_ = recentBox_ = {};
        rightsTop_ = -1.f;
        const bool pending = p.kind == Person::Kind::Pending;
        for (ui::Widget* b : {static_cast<ui::Widget*>(open_), static_cast<ui::Widget*>(journal_), static_cast<ui::Widget*>(rights_),
                              static_cast<ui::Widget*>(retire_), static_cast<ui::Widget*>(verify_), static_cast<ui::Widget*>(refuse_),
                              static_cast<ui::Widget*>(curve_), static_cast<ui::Widget*>(allJournal_)})
            b->setVisibility(ui::Visibility::Collapsed);
        if (pending) {
            noteY_ = y;
            y += 44.f;
            const float bw1 = buttonW(*verify_, 120.f), bw2 = buttonW(*refuse_, 120.f);
            put(verify_, {x, y, bw1, 32.f});
            put(refuse_, {x + bw1 + 10.f, y, bw2, 32.f});
            verify_->setEnabled(!(a_.busy && a_.busy()));
            refuse_->setEnabled(!(a_.busy && a_.busy()));
            y += 46.f;
            return y - at.y;
        }
        // 1. ses chiffres
        panelFigs_ = {x, y, w, 62.f};
        y += 62.f + 12.f;
        // 2. sa courbe, ses 5 dernieres actions
        const bool two = w >= 640.f;
        const float boxH = 200.f;
        if (two) {
            const float lw = std::floor(w * 0.44f);
            chartBox_ = {x, y, lw, boxH};
            recentBox_ = {x + lw + 12.f, y, w - lw - 12.f, boxH};
            y += boxH + 12.f;
        } else {
            chartBox_ = {x, y, w, boxH};
            recentBox_ = {x, y + boxH + 10.f, w, boxH};
            y += 2.f * boxH + 22.f;
        }
        put(curve_, {chartBox_.x + 12.f, chartBox_.y + 44.f, chartBox_.w - 24.f, chartBox_.h - 52.f});
        const float lw = allJournal_->naturalWidth();
        put(allJournal_, {recentBox_.right() - 14.f - lw, recentBox_.y + 12.f, lw, 20.f});
        // 3. ses droits
        rightsTop_ = y;
        y += layoutRights(p, x, y, w, false) + 12.f;
        // 4. les boutons
        const float bh = 34.f;
        const float ow = buttonW(*open_, 130.f);
        put(open_, {x, y, ow, bh});
        hintX_ = x + ow + 12.f;
        buttonsY_ = y;
        float bx = x + w;
        std::vector<ui::Button*> right;
        const bool admin = p.kind == Person::Kind::Admin;
        if (admin && p.active()) right.push_back(retire_);
        if (admin && p.active()) right.push_back(rights_);
        if (p.key.size() == 64) right.push_back(journal_);
        float need = ow + 12.f;
        for (auto* b : right) need += buttonW(*b, 96.f) + 8.f;
        const bool oneRow = need <= w;
        if (!oneRow) {
            y += bh + 8.f;
            bx = x + w;
        }
        for (auto* b : right) {
            const float bw = buttonW(*b, 96.f);
            bx -= bw;
            put(b, {bx, y, bw, bh});
            bx -= 8.f;
        }
        showHint_ = oneRow && hintX_ + ui::measureWidth("ou double-clic, ou Entr\xC3\xA9" "e", kF13) + 12.f < bx;
        if (admin) {
            const std::string why = a_.whyNotRetire ? a_.whyNotRetire(p.key) : std::string();
            retire_->setEnabled(why.empty());
            retire_->setTooltip(why.empty() ? std::string("Son certificat est r\xC3\xA9voqu\xC3\xA9. Ce qu'il a sign\xC3\xA9 reste valide, sauf si tu choisis de r\xC3\xA9voquer aussi ses licences.")
                                            : why);
        }
        y += bh + 14.f;
        return y - at.y;
    }

    void hidePanel() {
        for (ui::Widget* b : {static_cast<ui::Widget*>(open_), static_cast<ui::Widget*>(journal_), static_cast<ui::Widget*>(rights_),
                              static_cast<ui::Widget*>(retire_), static_cast<ui::Widget*>(verify_), static_cast<ui::Widget*>(refuse_),
                              static_cast<ui::Widget*>(curve_), static_cast<ui::Widget*>(allJournal_)})
            b->setVisibility(ui::Visibility::Collapsed);
    }

    // Les droits d'un admin en une suite d'elements (le titre, puis chaque droit) ; rend la hauteur (paint : les dessine).
    float layoutRights(const Person& p, float x, float y, float w, bool paint, gfx::IRenderer* r = nullptr) {
        const js::AdminStats* s = d_->stats(p.key);
        struct Item {
            std::string text;
            int         kind;   // 0 permis, 1 jamais utilise, 2 pas ce droit
        };
        std::vector<Item> items;
        int held = 0;
        if (p.kind == Person::Kind::Plus) {
            items.push_back({"Tous les droits, et ceux que l'admin+ garde (les versions, la sauvegarde, les admins)", 0});
            held = 13;
        } else if (s) {
            std::set<std::string> added, removed;
            for (const auto& c : s->rightsChanges) {
                if (!d_->period.contains(js::localDay(c.line.when, js::TimeZone::paris()))) continue;
                for (const auto& a : c.added) added.insert(a);
                for (const auto& a : c.removed) removed.insert(a);
            }
            for (int pass = 0; pass < 2; ++pass)
                for (const auto& u : s->rights) {
                    if (u.held != (pass == 0)) continue;
                    std::string t = rightShort(u.id);
                    if (u.held && u.neverUsed) t += " : jamais";
                    if (added.count(u.id) && u.held) t += " (ajout\xC3\xA9)";
                    if (removed.count(u.id) && !u.held) t += " (retir\xC3\xA9)";
                    items.push_back({t, u.held ? (u.neverUsed ? 1 : 0) : 2});
                    held += u.held ? 1 : 0;
                }
        } else {
            for (const auto& id : p.rights) items.push_back({rightShort(id), 0});
            held = static_cast<int>(p.rights.size());
        }
        const std::string head = "Ses droits";
        const std::string count = std::to_string(held) + " sur 13";
        float cx = x, cy = y;
        const float lineH = 26.f;
        if (paint && r) {
            r->drawText({cx, cy + 4.f}, head, kB13, kInk);
        }
        cx += ui::measureWidth(head, kB13) + 10.f;
        if (paint && r) r->drawText({cx, cy + 4.f}, count, kF13, kMuted);
        cx += ui::measureWidth(count, kF13) + 16.f;
        for (const auto& it : items) {
            const FontId f = kF13;
            const float iw = 18.f + std::min(ui::measureWidth(it.text, f), w - 20.f) + 16.f;
            if (cx + iw > x + w && cx > x) {
                cx = x;
                cy += lineH;
            }
            if (paint && r) {
                const Color c = it.kind == 0 ? kInk2 : it.kind == 1 ? kAmber : kFaint;
                if (it.kind == 0) kit::drawTick(*r, cx, cy + 6.f, kGreen);
                else if (it.kind == 1) ui::drawIcon(*r, ui::Icon::Warning, {cx, cy + 5.f, 14.f, 14.f}, kAmber);
                else kit::drawCross(*r, cx + 2.f, cy + 8.f, kFaint);
                (void)text(*r, cx + 18.f, cy + 4.f, it.text, it.kind == 1 ? gfx::semibold(f) : f, c, w - 20.f);
            }
            cx += iw;
        }
        return cy - y + lineH;
    }

    // ======================================================================== le dessin
    void draw(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        r.pushClip(viewport());
        for (int i = 0; i < 6 && i < static_cast<int>(tiles_.size()); ++i) paintTile(ctx, i, tiles_[static_cast<std::size_t>(i)]);
        // ---- le classement ----
        card(r, th, rank_);
        r.fillRect({rank_.x + 1.f, headY_ - 6.f, rank_.w - 2.f, 1.f}, kLine);
        r.fillRect({rank_.x + 1.f, headY_ - 5.f, rank_.w - 2.f, 30.f}, Color::rgb(0xF8FAFC));
        for (int c = 0; c < 8; ++c) {
            const bool on = c == sortCol_;
            std::string h = kHeads[c];
            const float hw = ui::measureWidth(h, gfx::semibold(kF11));
            (void)text(r, colX_[c] - (c == 0 ? 26.f : 0.f), headY_ + 3.f, h, gfx::semibold(kF11), on ? kVioletInk : kFaint, colW_[c] - 14.f);
            const float ax = colX_[c] - (c == 0 ? 26.f : 0.f) + std::min(hw, colW_[c] - 14.f) + 4.f;
            if (c < 7) r.drawText({ax, headY_ + 3.f}, on ? (sortDesc_ ? "\xE2\x86\x93" : "\xE2\x86\x91") : "\xE2\x87\x85", kF11, on ? kVioletInk : kLine);
        }
        r.fillRect({rank_.x + 1.f, headY_ + 25.f, rank_.w - 2.f, 1.f}, kLine);
        for (const auto& g : rows_) paintRow(ctx, g);
        if (rows_.empty()) {
            const std::string t = d_->people.empty() ? std::string("Aucun admin pour l'instant : \xC2\xAB Ajouter un admin\xE2\x80\xA6 \xC2\xBB en haut.")
                                                     : std::string("Aucun admin ne correspond \xC3\xA0 ces filtres.");
            (void)text(r, rank_.x + 20.f, headY_ + 50.f, t, kF13, kMuted, rank_.w - 40.f);
        }
        // ---- les alertes ----
        paintAlerts(ctx);
        r.popClip();
    }

    void paintTile(const ui::PaintContext& ctx, int i, const Rect& b) {
        auto& r = ctx.r;
        card(r, ctx.theme, b, 12.f);
        const float x = b.x + 16.f, w = b.w - 32.f;
        float y = b.y + 14.f;
        const Data& d = *d_;
        const auto label = [&](ui::Icon icon, const std::string& t) {
            ui::drawIcon(r, icon, {x, y + 1.f, 14.f, 14.f}, kMuted);
            (void)text(r, x + 20.f, y, t, kF13, kInk2, w - 20.f);
        };
        const auto big = [&](const std::string& v, float yy) {
            r.drawText({x, yy}, v, gfx::semibold(FontId{26}), kInk);
            return ui::measureWidth(v, gfx::semibold(FontId{26}));
        };
        const std::string svc = d.serviceLoading ? std::string("lecture du service\xE2\x80\xA6")
                                : d.serviceTooOld ? std::string("le service est trop ancien")
                                : !d.serviceNote.empty() ? std::string("le service ne r\xC3\xA9pond pas") : std::string();
        switch (i) {
            case 0: {   // les admins
                label(ui::Icon::User, "Admins");
                const float bw = big(std::to_string(d.activeCount()), y + 22.f);
                r.drawText({x + bw + 6.f, y + 34.f}, d.activeCount() > 1 ? "actifs" : "actif", kB14, kInk2);
                float ax = x + w;
                for (auto it = d.people.rbegin(); it != d.people.rend(); ++it) {
                    ax -= 20.f;
                    if (ax < x + w * 0.45f) break;
                    avatarOf(r, {ax, y - 2.f, 18.f, 18.f}, *it);
                    ax -= 2.f;
                }
                const int pend = d.pendingCount(), ret = d.retiredCount();
                std::string sub = std::to_string(pend) + " en attente \xC2\xB7 " + std::to_string(ret) + (ret > 1 ? " retir\xC3\xA9s" : " retir\xC3\xA9");
                (void)text(r, x, y + 66.f, sub, kF12, kMuted, w);
                break;
            }
            case 1: {   // les actions
                label(ui::Icon::Chart, "Actions");
                const float bw = big(num(d.ov.actions), y + 22.f);
                (void)delta(r, x + bw + 6.f, y + 34.f, d.ov.actions, d.ov.actionsBefore);
                std::vector<double> sum;
                for (const auto& a : d.ov.admins) {
                    if (sum.size() < a.perDay.size()) sum.resize(a.perDay.size(), 0.0);
                    for (std::size_t k = 0; k < a.perDay.size(); ++k) sum[k] += a.perDay[k];
                }
                if (w > 150.f && sum.size() > 1) ch::paintSparkline(r, ctx.theme, {x + w - std::min(110.f, w * 0.42f), y + 26.f, std::min(110.f, w * 0.42f), 28.f}, sum, nullptr, {.color = kViolet});
                (void)text(r, x, y + 66.f, num(d.ov.actionsBefore) + " avant \xC2\xB7 tous les admins", kF12, kMuted, w);
                break;
            }
            case 2: {   // les demandes en attente
                label(ui::Icon::Mail, "Demandes en attente");
                (void)big(num(d.totals.pending), y + 22.f);
                if (!svc.empty() && !known(d.totals.pending)) {
                    (void)text(r, x, y + 66.f, svc, kF12, kMuted, w);
                    break;
                }
                float sx = x + text(r, x, y + 60.f, "r\xC3\xA9ponse en " + hoursText(d.totals.meanDelay), kF12, kMuted, w) + 6.f;
                (void)delta(r, sx, y + 60.f, d.totals.meanDelay, d.totals.meanDelayBefore, false, kB12);
                if (known(d.totals.oldest) && d.totals.oldest > 0)
                    (void)text(r, x, y + 78.f, "la plus ancienne attend " + hoursText(d.totals.oldest), kF12, kMuted, w);
                break;
            }
            case 3: {   // les licences signees
                label(ui::Icon::Document, "Licences sign\xC3\xA9" "es");
                const float bw = big(num(d.totals.licences), y + 22.f);
                (void)delta(r, x + bw + 6.f, y + 34.f, d.totals.licences, d.totals.licencesBefore);
                if (!svc.empty() && !known(d.totals.licences)) {
                    (void)text(r, x, y + 66.f, svc, kF12, kMuted, w);
                    break;
                }
                const double plus = known(d.totals.licencesPlus) ? d.totals.licencesPlus : 0.0;
                const double all = known(d.totals.licences) ? d.totals.licences : 0.0;
                if (all > 0) {
                    ch::paintSegments(r, ctx.theme, {x, y + 62.f, w, 8.f}, {all - plus, plus}, {Color::rgb(0x0B7FA3), kViolet});
                    float lx = x;
                    r.fillRoundedRect({lx, y + 80.f, 8.f, 8.f}, Color::rgb(0x0B7FA3), 4.f);
                    lx += 12.f + text(r, lx + 12.f, y + 76.f, "les admins " + num(all - plus), kF12, kMuted, w * 0.55f);
                    r.fillRoundedRect({lx + 10.f, y + 80.f, 8.f, 8.f}, kViolet, 4.f);
                    (void)text(r, lx + 22.f, y + 76.f, "toi " + num(plus), kF12, kMuted, x + w - lx - 22.f);
                }
                break;
            }
            case 4: {   // le montant
                label(ui::Icon::Chart, "Montant encaiss\xC3\xA9");
                (void)big(euros(d.totals.amount), y + 22.f);
                if (!svc.empty() && !known(d.totals.amount)) {
                    (void)text(r, x, y + 66.f, svc, kF12, kMuted, w);
                    break;
                }
                if (w > 170.f && d.totals.amountPoints.size() > 1)
                    ch::paintSparkline(r, ctx.theme, {x + w - std::min(90.f, w * 0.36f), y + 26.f, std::min(90.f, w * 0.36f), 26.f}, d.totals.amountPoints, nullptr, {});
                float sx = x + delta(r, x, y + 64.f, d.totals.amount, d.totals.amountBefore);
                (void)text(r, sx + 6.f, y + 64.f, num(d.totals.orders) + " commandes livr\xC3\xA9" "es", kF12, kMuted, x + w - sx - 6.f);
                break;
            }
            default: {   // les alertes
                label(ui::Icon::Warning, "Alertes");
                (void)big(std::to_string(d.ov.alerts.size()), y + 22.f);
                int c = 0, h = 0, s = 0;
                for (const auto& a : d.ov.alerts) (a.severity == js::Severity::Critical ? c : a.severity == js::Severity::High ? h : s)++;
                float sx = x;
                if (c) sx += text(r, sx, y + 66.f, std::to_string(c) + (c > 1 ? " critiques" : " critique"), kB12, kRed, w) + 4.f;
                const std::string rest = (c ? std::string("\xC2\xB7 ") : std::string()) + std::to_string(h) + (h > 1 ? " hautes" : " haute") + " \xC2\xB7 " +
                                         std::to_string(s) + " \xC3\xA0 voir";
                (void)text(r, sx, y + 66.f, rest, kF12, kMuted, x + w - sx);
                break;
            }
        }
    }

    void paintRow(const ui::PaintContext& ctx, const RowGeo& g) const;
    void paintPanel(const ui::PaintContext& ctx, const Person& p, const Rect& panel) const;
    void paintAlerts(const ui::PaintContext& ctx) const;

    // ======================================================================== les gestes
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* md = std::get_if<ui::MouseDown>(&ev)) {
            if (menu_->isOpen()) return ui::EventResult::Ignored;
            if (!viewport().contains(md->pos)) return ui::EventResult::Ignored;
            // les titres des colonnes
            if (md->button == ui::MouseButton::Left && md->pos.y >= headY_ - 5.f && md->pos.y < headY_ + 25.f && rank_.contains(md->pos)) {
                for (int c = 0; c < 8; ++c)
                    if (md->pos.x >= colX_[c] - (c == 0 ? 26.f : 0.f) && md->pos.x < colX_[c] + colW_[c]) {
                        if (c < 7) sortBy(c);
                        return ui::EventResult::Consumed;
                    }
            }
            for (const auto& g : rows_) {
                if (!g.row.contains(md->pos)) continue;
                const std::string key = d_->people[static_cast<std::size_t>(g.person)].key;
                grabFocus();
                if (md->button == ui::MouseButton::Right) {
                    if (selected_ != key) choose(key);
                    openMenu(key, md->pos);
                    return ui::EventResult::Consumed;
                }
                if (md->button == ui::MouseButton::Left) {
                    if (md->clickCount >= 2) {
                        choose(key);
                        openSelected();
                    } else {
                        choose(selected_ == key ? std::string() : key);
                    }
                    return ui::EventResult::Consumed;
                }
            }
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused()) {
            const auto ord = sortedRows();
            int at = -1;
            for (std::size_t i = 0; i < ord.size(); ++i)
                if (d_->people[static_cast<std::size_t>(ord[i])].key == selected_) at = static_cast<int>(i);
            if ((k->key == ui::Key::Down || k->key == ui::Key::Up) && !ord.empty()) {
                const int n = static_cast<int>(ord.size());
                const int next = at < 0 ? 0 : std::clamp(at + (k->key == ui::Key::Down ? 1 : -1), 0, n - 1);
                choose(d_->people[static_cast<std::size_t>(ord[static_cast<std::size_t>(next)])].key);
                return ui::EventResult::Consumed;
            }
            if (k->key == ui::Key::Return && !selected_.empty()) {
                openSelected();
                return ui::EventResult::Consumed;
            }
            if (k->key == ui::Key::Escape && !selected_.empty()) {
                choose({});
                return ui::EventResult::Consumed;
            }
            if (ui::isContextMenuKey(*k) && !selected_.empty()) {
                for (const auto& g : rows_)
                    if (d_->people[static_cast<std::size_t>(g.person)].key == selected_) openMenu(selected_, {g.row.x + 40.f, g.row.y + 30.f});
                return ui::EventResult::Consumed;
            }
        }
        return ScrolledView::onEvent(ev);
    }

private:

    [[nodiscard]] std::string cellText(std::size_t row, std::size_t field) const {
        if (row >= d_->people.size()) return {};
        const Person& p = d_->people[row];
        switch (field) {
            case 0: return p.name;
            case 1: return stateOf(p);
            case 2: return p.kind == Person::Kind::Plus ? std::string("Admin+") : p.profile.empty() ? std::string("(sans profil)") : p.profile;
            case 3: {
                const AdminFigures* f = d_->fig(p.key);
                const std::string v = f && f->hasUsage && !f->usage.softwareVersion.empty() ? f->usage.softwareVersion : p.version;
                return v.empty() ? std::string("(inconnue)") : v;
            }
            case 4: return p.pc;
            default: return {};
        }
    }

    // Les lignes montrees (la barre), triees par la colonne ; les demandes a la fin.
    [[nodiscard]] std::vector<int> sortedRows() const {
        std::vector<int> out;
        for (std::size_t i = 0; i < d_->people.size(); ++i)
            if (bar_->accepts(i)) out.push_back(static_cast<int>(i));
        const auto value = [this](int i) -> double {
            const Person& p = d_->people[static_cast<std::size_t>(i)];
            const js::AdminStats* s = d_->stats(p.key);
            const AdminFigures* f = d_->fig(p.key);
            switch (sortCol_) {
                case 1: return s ? s->actions : -1.0;
                case 2: return s && known(s->licences) ? s->licences : -1.0;
                case 3: return s && known(s->requests) ? s->requests : -1.0;
                case 4: return f && known(f->meanDelay) ? f->meanDelay : -1.0;
                case 5: return f && known(f->amount) ? f->amount : -1.0;
                case 6: return s ? static_cast<double>(s->last.when) : 0.0;
                default: return 0.0;
            }
        };
        std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
            const Person& pa = d_->people[static_cast<std::size_t>(a)];
            const Person& pb = d_->people[static_cast<std::size_t>(b)];
            const bool qa = pa.kind == Person::Kind::Pending, qb = pb.kind == Person::Kind::Pending;
            if (qa != qb) return qb;
            if (sortCol_ == 0 || qa) return sortDesc_ ? pa.name > pb.name : pa.name < pb.name;
            const double va = value(a), vb = value(b);
            if (va != vb) return sortDesc_ ? va > vb : va < vb;
            return pa.color < pb.color;
        });
        return out;
    }

    void choose(const std::string& key) {
        selected_ = key;
        fillCurve();
        if (a_.select) a_.select(key);
        invalidateLayout();
        invalidate();
    }
    void openSelected() {
        if (selected_.empty() || !a_.openCard) return;
        const Person* p = d_->person(selected_);
        if (p && p->kind == Person::Kind::Pending) {
            if (a_.verify) a_.verify(selected_);
            return;
        }
        a_.openCard(selected_);
    }

    void fillCurve() {
        const js::AdminStats* s = d_->stats(selected_);
        const Person* p = d_->person(selected_);
        ch::TimeChartData cd;
        cd.kind = ch::Kind::Line;
        const int n = d_->period.days();
        cd.x = ch::dailyAxis(d_->period.first, n, n <= 92);
        if (s && p) {
            std::vector<double> now(s->perDay.begin(), s->perDay.end()), before(s->perDayBefore.begin(), s->perDayBefore.end());
            ch::Series main{"admin", "cette p\xC3\xA9riode", now};
            main.color = personColor(*p);
            main.area = true;
            main.endDot = true;
            ch::Series cmp{"cmp", "la p\xC3\xA9riode d'avant", before};
            cmp.role = ch::Role::Compare;
            cd.series.push_back(std::move(cmp));
            cd.series.push_back(std::move(main));
            // le pic : une etiquette
            int peak = -1;
            for (std::size_t k = 0; k < now.size(); ++k)
                if (peak < 0 || now[k] > now[static_cast<std::size_t>(peak)]) peak = static_cast<int>(k);
            if (peak >= 0 && now[static_cast<std::size_t>(peak)] > 0)
                cd.notes.push_back({peak, now[static_cast<std::size_t>(peak)], ch::fr::integer(now[static_cast<std::size_t>(peak)]) + " " + ch::fr::kDot + " le " +
                                                                                     ch::fr::ddmm(d_->period.first + peak)});
        }
        cd.legend = ch::LegendMode::None;
        cd.yTickCount = 2;
        cd.valueFormat = [](double v) { return ch::fr::integer(v) + (v > 1 ? " actions" : " action"); };
        curve_->setData(std::move(cd));
    }

    [[nodiscard]] ui::ListMenu buildMenu(const std::string& key) const {
        const Person* p = d_->person(key);
        ui::ListMenu m({1, "admins", false});
        if (!p) return m;
        const bool pending = p->kind == Person::Kind::Pending;
        const std::string k = key;
        auto* self = const_cast<OverviewView*>(this);
        if (pending) {
            m.open("V\xC3\xA9rifier et accepter\xE2\x80\xA6", [self, k] {
                if (self->a_.verify) self->a_.verify(k);
            });
        } else {
            m.open("Ouvrir sa fiche", [self, k] {
                if (self->a_.openCard) self->a_.openCard(k);
            });
        }
        m.unfold(selected_ == key, [self, k] { self->choose(self->selected_ == k ? std::string() : k); });
        if (p->kind == Person::Kind::Admin && p->active())
            m.action({.label = "Ses droits\xE2\x80\xA6", .icon = ui::Icon::Lock, .run = [self, k] {
                          if (self->a_.rightsOf) self->a_.rightsOf(k);
                      }});
        if (!pending && p->key.size() == 64)
            m.goTo({.label = "Son journal", .icon = ui::Icon::History, .run = [self, k] {
                        if (self->a_.journalOf) self->a_.journalOf(k);
                    }});
        const auto filter = [&](std::size_t field, const std::string& label) {
            const std::string v = cellText(static_cast<std::size_t>(&*p - &d_->people[0]), field);
            if (v.empty()) return;
            m.filterOn({.label = label + " : " + v, .run = [self, field, v] { self->bar_->filterOn(field, v); }});
        };
        filter(1, "Cet \xC3\xA9tat");
        if (!pending) filter(2, "Ce profil");
        if (!pending) filter(3, "Cette version");
        if (!p->fingerprint.empty()) m.copy("Son empreinte", p->fingerprint, ui::Icon::Copy);
        if (p->key.size() == 64) m.copy("Sa cl\xC3\xA9 (64 caract\xC3\xA8res)", p->key, ui::Icon::Copy);
        if (!p->id.empty()) m.copy("Son identifiant", p->id, ui::Icon::Copy);
        m.exportRows([self] {
            if (self->a_.exportRows) self->a_.exportRows(self->order());
        });
        if (p->kind == Person::Kind::Admin && p->active()) {
            const std::string why = a_.whyNotRetire ? a_.whyNotRetire(key) : std::string();
            m.danger({.label = "Retirer cet admin", .why = why, .run = [self, k] {
                          if (self->a_.retire) self->a_.retire(k);
                      }});
        }
        if (pending)
            m.danger({.label = "Refuser la demande", .run = [self, k] {
                          if (self->a_.refuse) self->a_.refuse(k);
                      }});
        return m;
    }
    void openMenu(const std::string& key, gfx::Point at) {
        menuList_ = buildMenu(key);
        menu_->setItems(menuList_.items(1));
        menu_->openInWindow(at);
    }

    void gesture(std::size_t i) {
        if (i >= d_->ov.alerts.size()) return;
        const js::Alert& al = d_->ov.alerts[i];
        const std::string& g = al.gestureId;
        if (g == "journal") {
            if (a_.journalLines) a_.journalLines(al.lines, al.adminId);
        } else if (g == "envoyer-maj") {
            if (a_.versions) a_.versions();
        } else if (g == "retirer-droits") {
            if (a_.removeRights) a_.removeRights(al.adminId, al.rights);
        } else if (g == "fiche") {
            if (a_.openCard) a_.openCard(al.adminId);
        } else if (g == "accepter") {
            for (const auto& p : d_->people)
                if (p.kind == Person::Kind::Pending && p.name == al.adminName) {
                    if (a_.verify) a_.verify(p.key);
                    return;
                }
        }
    }

    [[nodiscard]] std::vector<Rich> richOf(const js::Alert& al) const {
        const std::string& t = al.text;
        const std::size_t c = t.find(" : ");
        if (c != std::string::npos && c < 60) return {{t.substr(0, c), kB13, kInk}, {t.substr(c), kF13, kInk2}};
        return {{t, kF13, kInk2}};
    }
    [[nodiscard]] std::vector<std::string> okLines() const {
        std::set<js::AlertKind> seen;
        for (const auto& a : d_->ov.alerts) seen.insert(a.kind);
        std::vector<std::string> out;
        if (!d_->journal) return out;
        if (!seen.count(js::AlertKind::Inactive)) out.push_back("Aucun admin inactif depuis 30 jours");
        if (!seen.count(js::AlertKind::RefusedSignatures)) out.push_back("Aucune signature refus\xC3\xA9" "e");
        if (!seen.count(js::AlertKind::UnusualHour)) out.push_back("Aucune action \xC3\xA0 une heure inhabituelle");
        if (!seen.count(js::AlertKind::OtherPc)) out.push_back("Aucune action depuis un autre PC");
        if (!seen.count(js::AlertKind::RightsUnused)) out.push_back("Chaque droit donn\xC3\xA9 sert");
        if (!seen.count(js::AlertKind::AdminOutdated) && d_->serviceLoaded) out.push_back("Chaque Admin est \xC3\xA0 jour");
        bool peak = false;
        for (const auto& a : d_->ov.admins) peak = peak || !a.peaks.empty();
        if (!peak) out.push_back("Aucun pic d'activit\xC3\xA9");
        if (d_->ov.chain.ok() && d_->ov.chain.lines > 0) out.push_back("La cha\xC3\xAEne du journal est intacte (" + std::to_string(d_->ov.chain.lines) + " lignes)");
        return out;
    }

    std::shared_ptr<Data>     d_;
    Actions                   a_;
    ShellHost                 host_;
    std::string               selected_;
    int                       sortCol_{1};
    bool                      sortDesc_{true};
    ui::FilterBar*            bar_{nullptr};
    ch::TimeChart*            curve_{nullptr};
    LinkButton*               allJournal_{nullptr};
    LinkButton*               regler_{nullptr};
    ui::Button*               open_{nullptr};
    ui::Button*               journal_{nullptr};
    ui::Button*               rights_{nullptr};
    ui::Button*               retire_{nullptr};
    ui::Button*               verify_{nullptr};
    ui::Button*               refuse_{nullptr};
    ui::PopupMenu*            menu_{nullptr};
    ui::ListMenu              menuList_;
    std::vector<LinkButton*>  verifyRow_;
    std::vector<LinkButton*>  gestures_;
    std::vector<Rect>         tiles_;
    Rect                      rank_{}, alerts_{};
    float                     headY_{0}, okY_{0};
    float                     colX_[8]{}, colW_[8]{};
    std::vector<RowGeo>       rows_;
    std::vector<AlertGeo>     alertGeo_;
    Rect                      panelFigs_{}, chartBox_{}, recentBox_{};
    float                     rightsTop_{-1.f}, buttonsY_{0}, hintX_{0}, noteY_{0};
    bool                      showHint_{false};
    core::ConnectionScope     links_;
};

// ---------------------------------------------------------------------------- une ligne
void OverviewView::paintRow(const ui::PaintContext& ctx, const RowGeo& g) const {
    auto& r = ctx.r;
    const Person& p = d_->people[static_cast<std::size_t>(g.person)];
    const js::AdminStats* s = d_->stats(p.key);
    const AdminFigures* f = d_->fig(p.key);
    const Rect b = g.row;
    const bool pending = p.kind == Person::Kind::Pending;
    const bool faded = pending || p.retired;
    if (g.unfolded) {
        r.fillRect({b.x, b.y, b.w, b.h + g.panel.h}, Color::rgb(0xFAF9FF));
        r.fillRect({b.x, b.y, 3.f, b.h + g.panel.h}, kViolet);
    } else if (hovered() && false) {
        r.fillRect(b, Color::rgb(0xF8FAFC));
    }
    r.fillRect({b.x, b.bottom() - 1.f + (g.unfolded ? g.panel.h : 0.f), b.w, 1.f}, kLine2);
    // le chevron, l'avatar, le nom
    if (!pending) {
        const float cx = colX_[0] - 22.f, cy = b.y + 24.f;
        if (g.unfolded) {
            r.line({cx, cy}, {cx + 5.f, cy + 5.f}, kMuted, 1.5f);
            r.line({cx + 5.f, cy + 5.f}, {cx + 10.f, cy}, kMuted, 1.5f);
        } else {
            r.line({cx + 2.f, cy - 4.f}, {cx + 7.f, cy + 1.f}, kFaint, 1.5f);
            r.line({cx + 7.f, cy + 1.f}, {cx + 2.f, cy + 6.f}, kFaint, 1.5f);
        }
    }
    avatarOf(r, {colX_[0], b.y + 14.f, 32.f, 32.f}, p);
    const float nx = colX_[0] + 42.f, nw = colW_[0] - 46.f;
    (void)text(r, nx, b.y + 11.f, p.name + (p.me ? "" : ""), kB14, faded ? kMuted : kInk, nw);
    {
        float sx = nx;
        const float sy = b.y + 32.f;
        if (p.kind == Person::Kind::Plus) {
            sx += text(r, sx, sy, "Admin+", kB12, kVioletInk, nw);
            (void)text(r, sx, sy, std::string(" \xC2\xB7 ") + (p.me ? "toi" : p.pc), kF12, kMuted, nx + nw - sx);
        } else if (pending) {
            (void)text(r, sx, sy, "demande re\xC3\xA7ue " + ago(d_->now, p.received), kF12, kMuted, nw);
        } else if (p.retired) {
            (void)text(r, sx, sy, "retir\xC3\xA9 le " + dayMonth(p.retired), kF12, kMuted, nw);
        } else {
            std::string sub = p.profile.empty() ? std::string("sans profil") : p.profile;
            if (p.gaps > 0) sub += " \xC2\xB7 " + std::to_string(p.gaps) + (p.gaps > 1 ? " \xC3\xA9" "carts" : " \xC3\xA9" "cart");
            sx += text(r, sx, sy, sub, kF12, kMuted, nw);
            const std::string ver = f && f->hasUsage ? f->usage.softwareVersion : p.version;
            if (!ver.empty() && !d_->latestAdmin.empty() && olderVersion(ver, d_->latestAdmin))
                (void)text(r, sx, sy, " \xC2\xB7 " + ver, kB12, kAmber, nx + nw - sx);
        }
    }
    if (pending) {
        (void)text(r, colX_[1], b.y + 13.f, "Pas encore de chiffres : sa demande d'admin attend ton accord (empreinte \xC3\xA0 v\xC3\xA9rifier au t\xC3\xA9l\xC3\xA9phone).", kF13,
                   kMuted, colX_[7] - colX_[1] - 10.f);
    } else {
        // les actions, leur ecart, la courbe
        if (s) {
            const float aw = text(r, colX_[1], b.y + 11.f, num(s->actions), kB14, kInk, colW_[1]);
            (void)aw;
            if (p.retired && !known(s->change)) (void)text(r, colX_[1], b.y + 33.f, "avant : " + num(s->actionsBefore), kF12, kMuted, colW_[1] * 0.5f);
            else (void)delta(r, colX_[1], b.y + 33.f, s->actions, s->actionsBefore, true, kB12);
            if (colW_[1] > 110.f && s->perDay.size() > 1) {
                std::vector<double> v(s->perDay.begin(), s->perDay.end());
                const float sw = std::min(90.f, colW_[1] - 64.f);
                ch::SparkStyle st;
                st.color = personColor(p);
                st.area = false;
                ch::paintSparkline(r, ctx.theme, {colX_[1] + colW_[1] - sw - 6.f, b.y + 18.f, sw, 24.f}, v, nullptr, st);
            }
        }
        // les licences
        if (s) {
            (void)text(r, colX_[2], b.y + 11.f, num(s->licences), kB14, faded ? kMuted : kInk, colW_[2]);
            if (!p.retired) {
                const std::string dc = signedCount(s->licences, s->licencesBefore);
                const bool up = known(s->licences) && known(s->licencesBefore) && s->licences >= s->licencesBefore;
                (void)text(r, colX_[2], b.y + 33.f, dc, kB12, dc == "=" ? kMuted : up ? kGreen : kRed, colW_[2]);
            }
        }
        // les demandes
        if (s) {
            (void)text(r, colX_[3], b.y + 11.f, num(s->requests), kB14, faded ? kMuted : kInk, colW_[3]);
            if (f && known(f->accepted) && known(f->refused))
                (void)text(r, colX_[3], b.y + 33.f, num(f->accepted) + " \xE2\x9C\x93 \xC2\xB7 " + num(f->refused) + " \xC3\x97", kF12, kMuted, colW_[3]);
        }
        // le delai
        if (f) {
            const bool slow = known(f->meanDelay) && known(d_->totals.meanDelay) && f->meanDelay > 1.5 * d_->totals.meanDelay;
            (void)text(r, colX_[4], b.y + 11.f, hoursText(f->meanDelay), kB14, slow ? kAmber : faded ? kMuted : kInk, colW_[4]);
            if (known(f->medianDelay)) (void)text(r, colX_[4], b.y + 33.f, "m\xC3\xA9" "d. " + hoursText(f->medianDelay), kF12, kMuted, colW_[4]);
        } else {
            (void)text(r, colX_[4], b.y + 11.f, "\xE2\x80\x94", kB14, kFaint, colW_[4]);
        }
        // le montant
        if (f) {
            (void)text(r, colX_[5], b.y + 11.f, euros(f->amount), kB14, faded ? kMuted : kInk, colW_[5]);
            if (known(f->orders)) (void)text(r, colX_[5], b.y + 33.f, num(f->orders) + (f->orders > 1 ? " commandes" : " commande"), kF12, kMuted, colW_[5]);
        } else {
            (void)text(r, colX_[5], b.y + 11.f, "\xE2\x80\x94", kB14, kFaint, colW_[5]);
        }
        // la derniere action
        if (s && s->last.number) {
            (void)text(r, colX_[6], b.y + 11.f, ago(d_->now, s->last.when), kF13, kInk2, colW_[6] - 4.f);
            (void)text(r, colX_[6], b.y + 33.f, p.retired ? std::string("avant son retrait") : shortSummary(s->last), kF12, kMuted, colW_[6] - 4.f);
        } else {
            (void)text(r, colX_[6], b.y + 20.f, d_->journal ? "aucune" : "le journal n'est pas relev\xC3\xA9", kF12, kMuted, colW_[6] - 4.f);
        }
    }
    // l'etat
    {
        const std::string st = stateOf(p);
        const Color fg = pending ? kAmber : p.retired ? kRed : kGreen;
        const Color bg = pending ? kAmberPale : p.retired ? kRedPale : kGreenPale;
        const float pw = std::min(colW_[7], ui::measureWidth(st, kB12) + 28.f);
        (void)kit::pill(r, colX_[7] + std::max(0.f, colW_[7] - pw), b.y + 20.f, kit::fit(r, st, kB12, pw - 28.f), kB12, fg, bg, 22.f, !pending && !p.retired);
    }
    if (g.unfolded) paintPanel(ctx, p, g.panel);
}

// ---------------------------------------------------------------------------- le panneau deplie
void OverviewView::paintPanel(const ui::PaintContext& ctx, const Person& p, const Rect& panel) const {
    auto& r = ctx.r;
    const js::AdminStats* s = d_->stats(p.key);
    const AdminFigures* f = d_->fig(p.key);
    const float x = panel.x + 20.f, w = panel.w - 40.f;
    if (p.kind == Person::Kind::Pending) {
        std::string t = "Sa demande est arriv\xC3\xA9" "e " + ago(d_->now, p.received) + (p.pc.empty() ? std::string() : " depuis " + p.pc) +
                        ". Lis-lui son empreinte au t\xC3\xA9l\xC3\xA9phone avant de l'accepter";
        if (!p.fingerprint.empty()) t += " : " + p.fingerprint;
        if (p.mismatch) t = "ATTENTION : l'empreinte que le service rend n'est pas celle de sa cl\xC3\xA9. Ne l'accepte pas.";
        (void)kit::drawWrapped(r, x, noteY_, w, t, kF13, p.mismatch ? kRed : kInk2, 18.f, 2);
        return;
    }
    // 1. ses chiffres
    if (!panelFigs_.empty()) {
        const Rect fb = panelFigs_;
        r.fillRoundedRect(fb, kLine, 10.f);
        r.fillRoundedRect(fb.inset(1.f, 1.f), Color::rgb(0xFFFFFF), 9.f);
        const float cw = fb.w / 6.f;
        for (int i = 0; i < 6; ++i) {
            const float cx = fb.x + static_cast<float>(i) * cw + 14.f, ww = cw - 22.f;
            if (i) r.fillRect({fb.x + static_cast<float>(i) * cw, fb.y + 1.f, 1.f, fb.h - 2.f}, kLine);
            std::string label, value, extra;
            Color ec = kMuted;
            bool warn = false;
            double cur = kNaN, prev = kNaN;
            bool upGood = true, count = false;
            switch (i) {
                case 0:
                    label = "Licences sign\xC3\xA9" "es";
                    value = s ? num(s->licences) : num(kNaN);
                    if (s) cur = s->licences, prev = s->licencesBefore, count = true;
                    break;
                case 1:
                    label = "Demandes trait\xC3\xA9" "es";
                    value = s ? num(s->requests) : num(kNaN);
                    if (f && known(f->accepted)) extra = num(f->accepted) + " \xE2\x9C\x93 \xC2\xB7 " + num(f->refused) + " \xC3\x97";
                    break;
                case 2:
                    label = "D\xC3\xA9lai de r\xC3\xA9ponse";
                    value = f ? hoursText(f->meanDelay) : num(kNaN);
                    if (f) cur = f->meanDelay / 3600.0, prev = f->meanDelayBefore / 3600.0, upGood = false;
                    break;
                case 3:
                    label = "Montant encaiss\xC3\xA9";
                    value = f ? euros(f->amount) : num(kNaN);
                    if (f) cur = f->amount, prev = f->amountBefore;
                    break;
                case 4:
                    label = "Jours actifs";
                    value = s ? std::to_string(s->activeDays) : num(kNaN);
                    if (s) extra = "sur " + std::to_string(d_->period.days()) + " \xC2\xB7 s\xC3\xA9rie " + std::to_string(s->streak) + " j";
                    break;
                default:
                    label = "Signature refus\xC3\xA9" "e";
                    value = s ? std::to_string(s->refused.size()) : num(kNaN);
                    if (s && !s->refused.empty()) {
                        extra = s->refused.back().reason == js::RefusalReason::MissingRight ? "droit manquant" : "cl\xC3\xA9 retir\xC3\xA9" "e";
                        ec = kRed;
                        warn = true;
                    } else {
                        extra = "aucune";
                    }
                    break;
            }
            (void)text(r, cx, fb.y + 10.f, label, kF12, kMuted, ww);
            const float vw = text(r, cx, fb.y + 30.f, value, gfx::semibold(FontId{17}), kInk, ww);
            float ex = cx + vw + 6.f;
            if (i == 2 && known(cur) && known(prev)) {
                // l'ecart d'un delai : en heures (<< -1 h 20 >>), vert s'il baisse
                const double dh = cur - prev;
                const std::string t = (dh < 0 ? std::string(ch::fr::kMinus) : std::string("+")) + ch::fr::hours(std::abs(dh));
                (void)text(r, ex, fb.y + 34.f, t, kB12, dh <= 0 ? kGreen : kRed, cx + ww - ex);
            } else if (known(cur) && known(prev)) {
                ex += delta(r, ex, fb.y + 34.f, cur, prev, upGood, kB12, count);
            }
            if (!extra.empty()) (void)text(r, ex, fb.y + 34.f, extra, warn ? kB12 : kF12, ec, cx + ww - ex);
        }
    }
    // 2. sa courbe ; ses 5 dernieres actions
    if (!chartBox_.empty()) {
        for (const Rect& bx : {chartBox_, recentBox_}) {
            r.fillRoundedRect(bx, kLine, 10.f);
            r.fillRoundedRect(bx.inset(1.f, 1.f), Color::rgb(0xFFFFFF), 9.f);
        }
        const float tw = text(r, chartBox_.x + 14.f, chartBox_.y + 12.f, "Ses actions par jour", kB13, kInk, chartBox_.w - 28.f);
        if (s)
            (void)text(r, chartBox_.x + 22.f + tw, chartBox_.y + 13.f, num(s->actions) + " \xC2\xB7 avant en gris", kF12, kMuted, chartBox_.w - tw - 40.f);
        (void)text(r, recentBox_.x + 14.f, recentBox_.y + 12.f, "Ses 5 derni\xC3\xA8res actions", kB13, kInk, recentBox_.w - 140.f);
        float ry = recentBox_.y + 42.f;
        if (!s || s->recent.empty()) {
            (void)text(r, recentBox_.x + 14.f, ry, d_->journal ? "Aucune action au journal." : "Le journal n'est pas relev\xC3\xA9 sur ce poste.", kF13, kMuted,
                       recentBox_.w - 28.f);
        } else {
            for (const auto& ref : s->recent) {
                if (ry > recentBox_.bottom() - 26.f) break;
                const float lx = recentBox_.x + 14.f, lw = recentBox_.w - 28.f;
                const float ww = text(r, lx, ry, ago(d_->now, ref.when), kF12, kFaint, 92.f);
                (void)ww;
                // la criticite de la ligne
                js::Level lv = js::Level::Info;
                const auto it = std::lower_bound(d_->lines.begin(), d_->lines.end(), ref.number,
                                                 [](const js::Line& l, std::uint64_t n) { return l.number < n; });
                if (it != d_->lines.end() && it->number == ref.number) lv = it->level;
                const std::string lvText = levelName(lv);
                const kit::Tint tint = kit::levelTint(lv);
                const float pw = ui::measureWidth(lvText, gfx::semibold(kF11)) + 16.f;
                (void)kit::pill(r, lx + lw - pw, ry - 2.f, lvText, gfx::semibold(kF11), tint.fg, tint.bg, 20.f);
                // le resume, l'objet en gras
                const std::string sum = shortSummary(ref);
                const float sx = lx + 96.f, sw = lw - 96.f - pw - 8.f;
                const std::size_t at = ref.object.empty() ? std::string::npos : sum.find(ref.object);
                if (at != std::string::npos && sw > 80.f) {
                    float cx = sx;
                    cx += text(r, cx, ry, sum.substr(0, at), kF13, kInk2, sw);
                    cx += text(r, cx, ry, ref.object, kB13, kInk, sx + sw - cx);
                    (void)text(r, cx, ry, sum.substr(at + ref.object.size()), kF13, kMuted, sx + sw - cx);
                } else {
                    (void)text(r, sx, ry, sum, kF13, kInk2, sw);
                }
                ry += 30.f;
                if (ry < recentBox_.bottom() - 20.f) r.fillRect({lx, ry - 8.f, lw, 1.f}, kLine2);
            }
        }
    }
    // 3. ses droits
    if (rightsTop_ >= 0.f) {
        auto* self = const_cast<OverviewView*>(this);
        (void)self->layoutRights(p, x, rightsTop_, w, true, &r);
    }
    // 4. la ligne des boutons : l'astuce ; un admin retire
    if (showHint_) (void)text(r, hintX_, buttonsY_ + 9.f, "ou double-clic, ou Entr\xC3\xA9" "e", kF13, kMuted);
    if (p.retired) {
        const float hy = buttonsY_ + 9.f;
        const float hx = showHint_ ? hintX_ + ui::measureWidth("ou double-clic, ou Entr\xC3\xA9" "e", kF13) + 20.f : hintX_;
        (void)text(r, hx, hy, "Retir\xC3\xA9 le " + dayMonth(p.retired) + " : certificat r\xC3\xA9voqu\xC3\xA9, ce qu'il a sign\xC3\xA9 reste valide.", kF12, kMuted,
                   x + w - hx - 120.f);
    }
}

// ---------------------------------------------------------------------------- les alertes
void OverviewView::paintAlerts(const ui::PaintContext& ctx) const {
    auto& r = ctx.r;
    card(r, ctx.theme, alerts_);
    const float x = alerts_.x + 16.f, w = alerts_.w - 32.f;
    float y = alerts_.y + 16.f;
    ui::drawIcon(r, ui::Icon::Warning, {x, y + 2.f, 16.f, 16.f}, kAmber);
    const float tw = text(r, x + 24.f, y, "Alertes", gfx::semibold(FontId{16}), kInk, w * 0.5f);
    const std::string n = std::to_string(d_->ov.alerts.size());
    const float nw = ui::measureWidth(n, kB12) + 14.f;
    r.fillRoundedRect({x + 32.f + tw, y + 1.f, nw, 20.f}, kSunken, 6.f);
    r.drawText({x + 39.f + tw, y + 3.f}, n, kB12, kInk2);
    y += 36.f;
    r.fillRect({alerts_.x + 1.f, y - 6.f, alerts_.w - 2.f, 1.f}, kLine);
    if (!d_->journal) {
        (void)kit::drawWrapped(r, x, y + 4.f, w, "Les alertes viennent du journal des admins : il n'est pas encore relev\xC3\xA9 sur ce poste (Journal des admins).",
                               kF13, kMuted, 18.f, 3);
    }
    for (std::size_t i = 0; i < d_->ov.alerts.size() && i < alertGeo_.size(); ++i) {
        const auto& al = d_->ov.alerts[i];
        const auto& g = alertGeo_[i];
        const Color sc = al.severity == js::Severity::Critical ? kRed : al.severity == js::Severity::High ? kAmber : kMuted;
        const Color sb = al.severity == js::Severity::Critical ? kRedPale : al.severity == js::Severity::High ? kAmberPale : kSunken;
        ui::Icon icon = ui::Icon::Warning;
        switch (al.kind) {
            case js::AlertKind::Inactive: icon = ui::Icon::History; break;
            case js::AlertKind::RightsUnused: icon = ui::Icon::Lock; break;
            case js::AlertKind::AdminOutdated: icon = ui::Icon::Import; break;
            case js::AlertKind::RefusedSignatures: icon = ui::Icon::Error; break;
            case js::AlertKind::RecentCritical: icon = ui::Icon::Error; break;
            case js::AlertKind::UnusualHour: icon = ui::Icon::History; break;
            case js::AlertKind::OtherPc: icon = ui::Icon::Screen; break;
            case js::AlertKind::Pending: icon = ui::Icon::User; break;
        }
        r.fillRoundedRect({x, g.top, 32.f, 32.f}, sb, 8.f);
        ui::drawIcon(r, icon, {x + 8.f, g.top + 8.f, 16.f, 16.f}, sc);
        const std::string sev = js::severityLabel(al.severity);
        const float sw = ui::measureWidth(sev, kF12);
        r.drawText({x + w - sw, g.top + 1.f}, sev, kF12, sc);
        auto* self = const_cast<OverviewView*>(this);
        (void)paintRich(&r, x + 46.f, g.top, w - 46.f - 60.f, self->richOf(al), 18.f);
        if (i + 1 < d_->ov.alerts.size()) r.fillRect({alerts_.x + 1.f, g.bottom - 6.f, alerts_.w - 2.f, 1.f}, kLine2);
    }
    const auto ok = okLines();
    if (!ok.empty()) {
        float oy = okY_;
        r.fillRect({alerts_.x + 1.f, oy - 4.f, alerts_.w - 2.f, 1.f}, kLine);
        oy += 6.f;
        r.drawText({x, oy}, "Rien \xC3\xA0 signaler", kB13, kInk);
        oy += 22.f;
        for (const auto& l : ok) {
            kit::drawTick(r, x, oy + 4.f, kGreen);
            (void)text(r, x + 20.f, oy, l, kF12, kInk2, w - 20.f);
            oy += 20.f;
        }
    } else if (d_->ov.alerts.empty() && d_->journal) {
        (void)text(r, x, okY_ + 8.f, "Aucune alerte sur la p\xC3\xA9riode.", kF13, kMuted, w);
    }
}

} // namespace

std::unique_ptr<ScrolledView> makeOverview(std::shared_ptr<Data> data, Actions actions, ShellHost host) {
    return std::make_unique<OverviewView>(std::move(data), std::move(actions), std::move(host));
}
void overviewSelect(ScrolledView& v, const std::string& key) {
    if (auto* o = dynamic_cast<OverviewView*>(&v)) o->select(key);
}
std::string overviewSelected(const ScrolledView& v) {
    if (const auto* o = dynamic_cast<const OverviewView*>(&v)) return o->selected();
    return {};
}
void overviewSort(ScrolledView& v, int column) {
    if (auto* o = dynamic_cast<OverviewView*>(&v)) o->sortBy(column);
}
std::vector<std::string> overviewOrder(const ScrolledView& v) {
    if (const auto* o = dynamic_cast<const OverviewView*>(&v)) return o->order();
    return {};
}
std::vector<std::string> overviewMenu(const ScrolledView& v, const std::string& key) {
    if (const auto* o = dynamic_cast<const OverviewView*>(&v)) return o->menuFor(key).labels();
    return {};
}
bool overviewMenuRun(const ScrolledView& v, const std::string& key, const std::string& label) {
    if (const auto* o = dynamic_cast<const OverviewView*>(&v)) return o->menuFor(key).runLabel(label);
    return false;
}

} // namespace license_admin::gui::adminsboard
