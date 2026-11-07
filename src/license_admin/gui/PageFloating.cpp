// =============================================================================
//  license_admin/gui/PageFloating.cpp - 1.35.0 : LA PAGE << Licences flottantes >>
//  (le service des licences ; jusqu'a la 1.34, une page d'explication)
// -----------------------------------------------------------------------------
//  Une licence flottante est une licence EN LIGNE de type << floating >> : un
//  nombre de postes EN MEME TEMPS, partage par toute une equipe. Chaque PC du
//  client l'active une fois avec le code ; puis, au demarrage du logiciel, il
//  prend un JETON au service (15 minutes, renouvele tant qu'il tourne) et le
//  rend en fermant. Le service tient le compte (un Durable Object : deux PC ne
//  prennent jamais le meme jeton).
//
//  La page montre le DERNIER RELEVE du service (Relever : GET /admin/licences,
//  lent, runSlow) - jamais un chiffre invente : jamais releve, c'est dit ;
//  chaque licence flottante (postes pris / total, le bail, son etat), et pour
//  celle choisie les jetons en cours (quel PC, jusqu'a quand) et ses PC actives.
//  << Retirer ce PC >> : son activation n'est plus renouvelee (sa place se
//  libere ; son jeton tombe a sa fin). << Nouvelle licence flottante... >> :
//  l'assistant d'une licence en ligne (choisir le type << flottante >>).
// =============================================================================
#include "Page.hpp"

#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace licdlg {
// Definis dans LicenceWizard.cpp.
menu::MenuPtr makeConfirmDialog(std::string id, std::string title, std::string text, std::string okLabel, bool danger,
                                std::function<void()> onOk);
std::string fingerprintOfKey(const std::string& hex);
} // namespace licdlg

namespace {

using RowIndex = ui::RowIndex;
constexpr float kPad = 16.f;
constexpr float kLineH = 20.f;

class Rows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(RowIndex, std::size_t)>;
    Rows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, Style style = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), style_(std::move(style)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string(); }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override { return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string(); }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override { return style_ ? style_(r, c) : ui::CellStyle{}; }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }

private:
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;
    Style                                 style_;
};

template <class T>
T* add(ui::Widget& parent, std::unique_ptr<T> w) {
    T* p = w.get();
    parent.addChild(std::move(w));
    return p;
}

std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

class FloatingPage final : public Page {
public:
    explicit FloatingPage(ShellHost host) : Page("page.flottantes", std::move(host)) {
        refresh_ = add(*this, std::make_unique<ui::Button>("Relever", "flottantes.relever"));
        refresh_->setStyle(ui::Button::Style::Primary);
        refresh_->setIcon(ui::Icon::Refresh);
        refresh_->setTooltip("Lit sur le service les licences en ligne, leurs postes pris et leurs jetons en cours.");
        create_ = add(*this, std::make_unique<ui::Button>("Nouvelle licence flottante\xE2\x80\xA6", "flottantes.nouvelle"));
        create_->setTooltip("L'assistant d'une licence en ligne : choisis le type \xC2\xAB flottante \xC2\xBB et le nombre de postes en m\xC3\xAAme temps.");
        retire_ = add(*this, std::make_unique<ui::Button>("Retirer ce PC\xE2\x80\xA6", "flottantes.retirer"));
        retire_->setStyle(ui::Button::Style::Danger);
        retire_->setTooltip("Le PC choisi n'est plus renouvel\xC3\xA9 : sa place se lib\xC3\xA8re, son jeton tombe \xC3\xA0 sa fin.");
        settings_ = add(*this, std::make_unique<ui::Button>("R\xC3\xA9gler le service", "flottantes.regler"));
        settings_->setIcon(ui::Icon::Settings);
        licences_ = add(*this, std::make_unique<ui::TableView>("flottantes.table"));
        licences_->setSelectionMode(ui::SelectionMode::Single);
        licences_->setColumns({{"Licence", 150.f}, {"Client", 200.f}, {"Postes pris", 120.f}, {"Bail", 80.f}, {"\xC3\x89tat", 150.f}, {"\xC3\x89mise le", 110.f}});
        seats_ = add(*this, std::make_unique<ui::TableView>("flottantes.postes"));
        seats_->setSelectionMode(ui::SelectionMode::Single);
        seats_->setColumns({{"PC", 180.f}, {"Empreinte", 220.f}, {"Jeton jusqu'\xC3\xA0", 150.f}, {"Activation", 150.f}, {"Vu le", 130.f}});

        links_ += refresh_->clicked->connect([this] { fetch(); });
        links_ += create_->clicked->connect([this] {
            if (host_.openDialog) host_.openDialog(makeLicenceWizard(host_, {}, {}, {}, true));
        });
        links_ += retire_->clicked->connect([this] { askRetire(); });
        links_ += settings_->clicked->connect([this] {
            if (host_.navigate) host_.navigate("service");
        });
        links_ += licences_->selectionChanged->connect([this](const std::vector<RowIndex>& sel) {
            if (syncing_) return;
            selectedId_ = !sel.empty() && sel.front() < ids_.size() ? ids_[sel.front()] : std::string();
            showSeats();
        });
        links_ += seats_->selectionChanged->connect([this](const std::vector<RowIndex>&) { syncButtons(); });
        refresh();
    }
    ~FloatingPage() override {
        links_.clear();
        *life_ = false;
    }

    [[nodiscard]] std::string title() const override { return "Licences flottantes"; }

    void refresh() override {
        if (!busy()) fetching_ = false;
        rebuild();
        invalidateLayout();
        invalidate();
    }

    // Les essais.
    [[nodiscard]] std::string statusText() const { return status(); }
    [[nodiscard]] std::string emptyText() const { return empty(); }
    [[nodiscard]] const std::vector<std::string>& shownIds() const noexcept { return ids_; }

protected:
    void onLayout() override {
        const gfx::Rect b = bounds();
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        float y = b.y + kPad + 34.f;
        leadY_ = y;
        y += 2.f * kLineH + 8.f;
        float bx = x;
        for (ui::Button* btn : {refresh_, create_, retire_, settings_}) {
            if (!btn->visible()) continue;
            const float bw = btn->sizeHint().preferred.w;
            btn->setBounds({bx, y, bw, 32.f});
            bx += bw + 8.f;
        }
        y += 40.f;
        statusY_ = y;
        y += kLineH + 8.f;
        const float avail = std::max(120.f, b.bottom() - kPad - y);
        const float top = std::floor(avail * 0.5f);
        licences_->setBounds({x, y, w, top});
        seatsCaptionY_ = y + top + 8.f;
        seats_->setBounds({x, seatsCaptionY_ + 24.f, w, std::max(60.f, b.bottom() - kPad - seatsCaptionY_ - 24.f)});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        if (const bool bz = busy(); bz != lastBusy_) {
            lastBusy_ = bz;
            if (!bz) fetching_ = false;
            syncButtons();
        }
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const gfx::Rect b = bounds();
        r.fillRect(b, c.panelBg);
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        r.drawText({x, b.y + kPad}, "Licences flottantes", ctx.theme.font.title, c.text);
        r.drawText({x, leadY_}, fit(r, "Un nombre de postes EN M\xC3\x8AME TEMPS pour toute une \xC3\xA9quipe : chaque PC active la licence avec son code, puis prend un jeton "
                                       "au service en d\xC3\xA9marrant (15 min, renouvel\xC3\xA9) et le rend en fermant.", ctx.theme.font.ui, w),
                   ctx.theme.font.ui, c.textMuted);
        r.drawText({x, leadY_ + kLineH}, fit(r, "Le service tient le compte : deux PC ne prennent jamais le m\xC3\xAAme jeton. Les chiffres sont ceux du dernier relev\xC3\xA9.",
                                             ctx.theme.font.ui, w),
                   ctx.theme.font.ui, c.textMuted);
        const auto& s = ws().service();
        const ui::Tone tone = !s.problem.empty() ? ui::Tone::Error : !s.configured || s.snapshot.at == 0 ? ui::Tone::Warning : ui::Tone::None;
        r.drawText({x, statusY_}, fit(r, status(), ctx.theme.font.ui, w), ctx.theme.font.ui, tone == ui::Tone::None ? c.text : ctx.theme.tone(tone, c.text));
        r.drawText({x, seatsCaptionY_}, fit(r, selectedId_.empty() ? std::string("Choisis une licence pour voir ses jetons et ses PC.")
                                                                   : "Les jetons et les PC de " + selectedId_ + " :",
                                            ctx.theme.font.ui, w),
                   ctx.theme.font.ui, c.textMuted);
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!ids_.empty()) return;
        const gfx::Rect t = licences_->bounds();
        ctx.r.drawText({t.x + 16.f, t.y + 40.f}, fit(ctx.r, empty(), ctx.theme.font.ui, t.w - 32.f), ctx.theme.font.ui, ctx.theme.color.textMuted);
    }

private:
    [[nodiscard]] bool busy() const { return host_.busy && host_.busy(); }

    [[nodiscard]] std::string status() const {
        const auto& s = ws().service();
        if (fetching_ && busy()) return "Relev\xC3\xA9 en cours\xE2\x80\xA6 (le r\xC3\xA9seau peut prendre quelques secondes)";
        if (!s.problem.empty()) return "Le service des licences est mal r\xC3\xA9gl\xC3\xA9 : " + s.problem;
        if (!s.configured) return "Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 : pas de licence flottante sans lui.";
        if (s.snapshot.at == 0) return "Service " + s.config.url + " \xC2\xB7 jamais relev\xC3\xA9 sur ce poste : clique \xC2\xAB Relever \xC2\xBB.";
        int floating = 0, used = 0, total = 0;
        for (const auto& l : s.snapshot.licences)
            if (l.floating()) {
                ++floating;
                used += static_cast<int>(l.leases.size());
                total += l.seats;
            }
        return "Relev\xC3\xA9 le " + dateTimeFr(s.snapshot.at) + " : " + std::to_string(floating) + (floating > 1 ? " licences flottantes, " : " licence flottante, ") +
               std::to_string(used) + " jeton(s) en cours sur " + std::to_string(total) + " poste(s).";
    }

    [[nodiscard]] std::string empty() const {
        const auto& s = ws().service();
        if (!s.ready()) return "R\xC3\xA8gle d'abord le service des licences (Service en ligne).";
        if (s.snapshot.at == 0) return "Pas encore relev\xC3\xA9 : clique \xC2\xAB Relever \xC2\xBB.";
        return "Aucune licence flottante sur le service : \xC2\xAB Nouvelle licence flottante\xE2\x80\xA6 \xC2\xBB pour en cr\xC3\xA9" "er une.";
    }

    void rebuild() {
        const auto& s = ws().service();
        ids_.clear();
        std::vector<std::vector<std::string>> rows;
        std::vector<int> tones;   // 0 ok, 1 pleine, 2 desactivee
        for (const auto& l : s.snapshot.licences) {
            if (!l.floating()) continue;
            const LicenceEntry* a = ws().licence(l.licenseId);
            const int used = static_cast<int>(l.leases.size());
            ids_.push_back(l.licenseId);
            rows.push_back({l.licenseId, a ? a->licence.customer : std::string("(pas dans l'archive)"), std::to_string(used) + " / " + std::to_string(l.seats),
                            std::to_string(l.leaseDays) + " j",
                            !l.active ? std::string("d\xC3\xA9sactiv\xC3\xA9" "e") : used >= l.seats && l.seats > 0 ? std::string("pleine") : std::string("active"),
                            dateFr(l.issuedAt)});
            tones.push_back(!l.active ? 2 : used >= l.seats && l.seats > 0 ? 1 : 0);
        }
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Licence", "Client", "Postes pris", "Bail", "\xC3\x89tat", "\xC3\x89mise le"}, std::move(rows),
                                        [tones](RowIndex r, std::size_t c) {
                                            ui::CellStyle st;
                                            if (c == 0) st.monospace = true;
                                            if (c == 4 && r < tones.size()) {
                                                static const ui::Tone map[3] = {ui::Tone::Ok, ui::Tone::Warning, ui::Tone::Error};
                                                st.fgTone = map[tones[r]];
                                            }
                                            return st;
                                        });
        syncing_ = true;
        licences_->setModel(model_);
        const auto it = std::find(ids_.begin(), ids_.end(), selectedId_);
        if (it != ids_.end()) licences_->selectModelRows({static_cast<RowIndex>(it - ids_.begin())}, false);
        else if (!ids_.empty()) {
            selectedId_ = ids_.front();
            licences_->selectModelRows({0}, false);
        } else {
            selectedId_.clear();
        }
        syncing_ = false;
        showSeats();
    }

    void showSeats() {
        seatKeys_.clear();
        std::vector<std::vector<std::string>> rows;
        if (const auto* l = ws().service().snapshot.licence(selectedId_)) {
            // Les PC actives, et le jeton de chacun s'il en tient un.
            for (const auto& a : l->activations) {
                Seconds until = 0;
                for (const auto& t : l->leases)
                    if (t.deviceKey == a.deviceKey) until = t.until;
                seatKeys_.push_back(a.deviceKey);
                rows.push_back({a.name.empty() ? std::string("(sans nom)") : a.name, licdlg::fingerprintOfKey(a.deviceKey),
                                until ? dateTimeFr(until) : std::string("\xE2\x80\x94 (pas de jeton)"), a.stateLabel(), dateTimeFr(a.seenAt)});
            }
            // Un jeton d'un PC sans activation listee (rare : une activation retiree pendant qu'il travaille).
            for (const auto& t : l->leases)
                if (!l->activation(t.deviceKey)) {
                    seatKeys_.push_back(t.deviceKey);
                    rows.push_back({"(PC retir\xC3\xA9)", licdlg::fingerprintOfKey(t.deviceKey), dateTimeFr(t.until), "\xE2\x80\x94", ""});
                }
        }
        seatsModel_ = std::make_shared<Rows>(std::vector<std::string>{"PC", "Empreinte", "Jeton jusqu'\xC3\xA0", "Activation", "Vu le"}, std::move(rows),
                                             [](RowIndex, std::size_t c) {
                                                 ui::CellStyle st;
                                                 if (c == 1) st.monospace = true;
                                                 return st;
                                             });
        seats_->setModel(seatsModel_);
        syncButtons();
        invalidate();
    }

    [[nodiscard]] int selectedSeat() const {
        const auto rows = seats_->selectedModelRows();
        if (rows.empty() || rows.front() >= seatKeys_.size()) return -1;
        return static_cast<int>(rows.front());
    }

    void syncButtons() {
        const bool b = busy();
        const bool on = ws().service().ready();
        refresh_->setEnabled(!b && on && ws().key().present);
        create_->setEnabled(on);
        retire_->setEnabled(!b && on && selectedSeat() >= 0);
        settings_->setVisibility(on ? ui::Visibility::Collapsed : ui::Visibility::Visible);
        invalidateLayout();
    }

    void fetch() {
        if (busy()) {
            if (host_.flash) host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
            return;
        }
        std::weak_ptr<bool> alive = life_;
        fetching_ = true;
        const bool started = refreshOnlineLicences(host_, [this, alive](const OpResult&) {
            if (alive.expired()) return;
            fetching_ = false;
            refresh();
        });
        if (!started) fetching_ = false;
        if (!alive.expired()) syncButtons();
    }

    void askRetire() {
        const int i = selectedSeat();
        if (i < 0 || selectedId_.empty()) {
            if (host_.flash) host_.flash("Choisis d'abord un PC dans la liste.", Tone::Warning);
            return;
        }
        const std::string id = selectedId_, key = seatKeys_[static_cast<std::size_t>(i)];
        std::weak_ptr<bool> alive = life_;
        ShellHost host = host_;
        host_.openDialog(licdlg::makeConfirmDialog(
            "flottantes.retrait", "Retirer ce PC",
            "Retirer le PC " + licdlg::fingerprintOfKey(key) + " de la licence " + id +
                " ?\n\nSon activation n'est plus renouvel\xC3\xA9" "e : il s'arr\xC3\xAAte \xC3\xA0 la fin de son bail, ou d\xC3\xA8s qu'il se connecte ; son jeton tombe \xC3\xA0 sa "
                "fin (15 min au plus). Le client peut l'activer de nouveau avec le code.",
            "Retirer", true, [this, alive, host, id, key] {
                if (host.busy && host.busy()) {
                    if (host.report) host.report(OpResult::fail("un travail est en cours : attends qu'il finisse, puis retire de nouveau"));
                    return;
                }
                const Workspace* w = &host.workspace();
                const bool started = host.runSlow && host.runSlow(
                                                         "Service : retrait d'un PC de " + id, [w, id, key] { return w->serviceRetire(id, key); },
                                                         [this, alive, host](const OpResult& r) {
                                                             if (alive.expired() || !r.ok) return;
                                                             (void)refreshOnlineLicences(host, [this, alive](const OpResult&) {
                                                                 if (!alive.expired()) refresh();
                                                             });
                                                         });
                if (!started && host.flash) host.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
            }));
    }

    std::shared_ptr<bool>    life_{std::make_shared<bool>(true)};
    core::ConnectionScope    links_;
    ui::Button*              refresh_{nullptr};
    ui::Button*              create_{nullptr};
    ui::Button*              retire_{nullptr};
    ui::Button*              settings_{nullptr};
    ui::TableView*           licences_{nullptr};
    ui::TableView*           seats_{nullptr};
    std::shared_ptr<Rows>    model_, seatsModel_;
    std::vector<std::string> ids_, seatKeys_;
    std::string              selectedId_;
    bool                     syncing_{false}, fetching_{false}, lastBusy_{false};
    float                    leadY_{0}, statusY_{0}, seatsCaptionY_{0};
};

} // namespace

PagePtr makeFloatingPage(ShellHost host) { return std::make_unique<FloatingPage>(std::move(host)); }

} // namespace license_admin::gui
