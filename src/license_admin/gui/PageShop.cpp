// =============================================================================
//  license_admin/gui/PageShop.cpp - 1.46.0 : LA VENTE EN LIGNE - les pages
//  << Boutique >> et << Commandes >>, et le dialogue << Brancher Stripe... >>
//  (docs/BOUTIQUE.md, par. 6)
// -----------------------------------------------------------------------------
//  BOUTIQUE : l'etat de Stripe (branche, mode test ou reel, le dernier evenement
//  recu), Brancher Stripe..., Ouvrir mon tableau de bord Stripe ; << Vendre en
//  ligne >> et << Livrer seul les commandes payees >> ; le CATALOGUE : chaque
//  offre du registre et chaque option (chacune de ses durees) - en vente ou non,
//  le prix affiche (le registre), le montant encaisse en EUR TTC et le libelle de
//  la facture (une case qui se change d'un double-clic) ; un ecart entre le prix
//  affiche et le montant est dit. Enregistrer -> PUT /admin/boutique (l'admin+).
//  COMMANDES : le tableau (n°, date, client, societe, article, montant, etat ;
//  un filtre), le detail de la commande choisie (le courriel, le PC, la licence
//  livree, Stripe, le recu), Delivrer maintenant, Voir dans Stripe, Ouvrir le
//  recu, Revoir..., Rembourser... (l'admin+, une question d'abord).
//  Un service plus ancien que la 1.46.0 : << deploie la 1.46.0 pour la boutique >>
//  et le bouton Deployer le service. Ce qui parle au service passe par runQuiet
//  (relever) et runSlow (enregistrer, livrer, rembourser, brancher).
//  LA CLE SECRETE DE STRIPE : un champ masque du dialogue ; elle quitte le champ
//  au clic sur Brancher (le champ est vide aussitot), n'est jamais ecrite, jamais
//  dans un message, et effacee apres l'appel.
// =============================================================================
#include "AdminsKit.hpp"
#include "Page.hpp"

#include "../Shop.hpp"
#include "../../deploy/Crypto.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui {

std::string shopServiceNote(const Workspace& w) {
    const auto& s = w.service();
    if (!s.ready() || !s.hasState || !s.state.reached || s.state.version.empty() || shop::serviceKnowsShop(s.state.version)) return {};
    return "Ton service en ligne est en " + s.state.version + " : d\xC3\xA9ploie la " + shop::kServiceRequired + " pour la boutique.";
}

namespace {

using namespace kit;
using gfx::Color;
using gfx::FontId;
using gfx::Rect;
using RowIndex = ui::RowIndex;

constexpr FontId kF28{28}, kF16{16}, kF15{15}, kF14{14}, kF13{13}, kF12{12};
constexpr const char* kBusy = "Un travail est en cours : attends qu'il finisse.";

// << 1 an >>, << 1 mois >>, << 30 jours >>, << sans fin >>.
std::string daysText(int days, bool option) {
    if (days <= 0) return option ? "jusqu'\xC3\xA0 la fin de la licence" : "sans fin";
    if (days == 365 || days == 366) return "1 an";
    if (days % 365 == 0) return std::to_string(days / 365) + " ans";
    if (days == 7) return "1 semaine";
    if (days == 30 || days == 31) return "1 mois";
    return std::to_string(days) + " jours";
}

// Le prix affiche (un texte : << 490 EUR HT / an >>) en centimes TTC (HT : + 20 %) ; faux : pas de nombre.
bool shownPriceTtc(const std::string& price, std::int64_t& cents) {
    std::string num;
    bool started = false;
    for (std::size_t i = 0; i < price.size(); ++i) {
        const char c = price[i];
        if ((c >= '0' && c <= '9') || ((c == ',' || c == '.') && started) || (c == ' ' && started && i + 1 < price.size() && price[i + 1] >= '0' && price[i + 1] <= '9')) {
            num += c;
            started = true;
        } else if (started) {
            break;
        }
    }
    std::int64_t v = 0;
    if (num.empty() || !shop::parseAmount(num, v)) return false;
    std::string low = price;
    for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (low.find("ht") != std::string::npos) v = static_cast<std::int64_t>(std::llround(static_cast<double>(v) * 1.2));
    cents = v;
    return true;
}

// LES COLONNES A LA LARGEUR DE LA TABLE (jamais une colonne hors de la vue) : une colonne de largeur fixe (weight 0)
// garde la sienne ; les souples se partagent le reste selon leur poids, chacune au moins `min` ; une colonne << a
// cacher d'abord >> (sa valeur est aussi dans le detail) disparait quand la place manque.
struct ColSpec {
    const char* title;
    float       width;    // fixe (weight 0) ; sinon ignoree
    float       weight;   // 0 : fixe
    float       min;
    bool        dropFirst;
};
void fitColumns(ui::TableView& t, const std::vector<ColSpec>& specs, float total) {
    const float avail = std::max(120.f, total - 18.f);   // la barre de defilement verticale, les bords
    std::vector<bool> shown(specs.size(), true);
    // Les largeurs des colonnes montrees ; vrai : tout tient. Une part (selon le poids) sous son minimum prend le
    // minimum, le reste se repartit entre les autres souples : la somme ne depasse jamais la place quand tout tient.
    const auto plan = [&](std::vector<float>& w) {
        w.assign(specs.size(), 0.f);
        float flex = avail, mins = 0.f;
        std::vector<std::size_t> free;
        for (std::size_t i = 0; i < specs.size(); ++i) {
            if (!shown[i]) continue;
            if (specs[i].weight <= 0.f) {
                w[i] = specs[i].width;
                flex -= specs[i].width;
            } else {
                free.push_back(i);
                mins += specs[i].min;
            }
        }
        const bool fits = flex >= mins;
        for (bool again = true; again && !free.empty();) {
            again = false;
            float weights = 0.f;
            for (std::size_t i : free) weights += specs[i].weight;
            for (auto it = free.begin(); it != free.end(); ++it)
                if (std::max(0.f, flex) * specs[*it].weight / weights < specs[*it].min) {
                    w[*it] = specs[*it].min;
                    flex -= specs[*it].min;
                    free.erase(it);
                    again = true;
                    break;
                }
        }
        float weights = 0.f;
        for (std::size_t i : free) weights += specs[i].weight;
        for (std::size_t i : free) w[i] = std::floor(std::max(0.f, flex) * specs[i].weight / weights);
        return fits;
    };
    std::vector<float> w;
    if (!plan(w))
        for (std::size_t i = 0; i < specs.size(); ++i)
            if (specs[i].dropFirst) {
                shown[i] = false;
                if (plan(w)) break;
            }
    std::vector<ui::TableView::Column> cols;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        ui::TableView::Column c;
        c.title = specs[i].title;
        c.width = std::max(30.f, w[i]);
        c.minWidth = 30.f;
        c.visible = shown[i];
        cols.push_back(std::move(c));
    }
    t.setColumns(std::move(cols));
}

void openUrl(const ShellHost& h, const std::string& url) {
    const std::string why = deployer::openLink(url);
    if (!why.empty() && h.flash) h.flash(why, Tone::Warning);
}

std::string problemText(std::string m) {
    const std::string pre = "Refus\xC3\xA9 : ";
    if (m.rfind(pre, 0) == 0) m = m.substr(pre.size());
    if (!m.empty() && m.back() == '.') m.pop_back();
    return m;
}

// Relever le reglage de la boutique (et les commandes, si ce poste a le droit << demandes >>) sans bruit ; adopte sur le
// fil de l'interface. Faux : rien n'est parti (le service n'est pas regle, un travail tourne).
bool fetchShop(const ShellHost& h, std::shared_ptr<bool> alive, std::function<void(const OpResult&)> done) {
    Workspace& w = h.workspace();
    if (!w.service().ready() || !w.signer()) return false;
    const Workspace* wp = &w;
    const bool orders = w.can("demandes");
    auto s = std::make_shared<shop::Settings>();
    auto v = std::make_shared<shop::OrdersView>();
    auto okOrders = std::make_shared<bool>(false);
    auto okSettings = std::make_shared<bool>(false);
    return runQuietly(
        h, "Boutique : relev\xC3\xA9",
        [wp, s, v, orders, okOrders, okSettings] {
            OpResult r = wp->shopSettings(*s);
            *okSettings = r.ok;
            if (r.ok && orders) {
                const OpResult o = wp->shopOrders(*v);
                *okOrders = o.ok;
                if (!o.ok) r = o;
            }
            return r;
        },
        [h, alive, s, v, okOrders, okSettings, done](const OpResult& r) {
            if (!*alive) return;
            Workspace& here = h.workspace();
            if (*okSettings) here.adoptShop(*s);
            if (*okOrders) here.adoptOrders(*v);
            if (done) done(r);
        });
}

// =================================================================================== le catalogue (modele)
struct CatalogueRow {
    std::string kind, edition, option, name, price;
    int         days{0};
    [[nodiscard]] std::string id() const { return shop::articleId(kind, kind == "option" ? option : edition, days); }
};

class CatalogueModel final : public ui::ITableModel {
public:
    std::vector<CatalogueRow> rows;
    shop::Settings*           draft{nullptr};
    std::function<void()>     changed;
    std::string               refusal;   // le dernier refus d'une case (montant illisible...)

    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 6; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* h[] = {"Article", "En vente", "Prix affich\xC3\xA9", "Montant encaiss\xC3\xA9 (TTC)", "Libell\xC3\xA9 de la facture", "\xC3\x89" "cart"};
        return c < 6 ? h[c] : "";
    }
    [[nodiscard]] const shop::Article* article(RowIndex r) const { return draft && r < rows.size() ? draft->find(rows[r].id()) : nullptr; }
    [[nodiscard]] std::string gap(RowIndex r) const {
        const shop::Article* a = article(r);
        std::int64_t shown = 0;
        if (!a || rows[r].price.empty() || !shownPriceTtc(rows[r].price, shown)) return {};
        if (std::llabs(shown - a->amount) <= 1) return {};
        return "\xE2\x89\xA0 affich\xC3\xA9 (" + shop::amountText(shown) + " TTC)";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= rows.size()) return {};
        const shop::Article* a = article(r);
        switch (c) {
            case 0: return rows[r].name;
            case 1: return a ? "Oui" : "Non";
            case 2: return rows[r].price.empty() ? std::string("\xE2\x80\x94") : rows[r].price;
            case 3: return a ? shop::amountText(a->amount, draft->currency) : std::string("\xE2\x80\x94");
            case 4: return a ? a->label : std::string();
            case 5: return gap(r);
            default: return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        const shop::Article* a = article(r);
        if (c == 1) {
            s.bold = a != nullptr;
            if (a) s.fg = Color::rgb(0x15803D);
            else s.fg = Color::rgb(0x64748B);
        }
        if (c == 5 && !gap(r).empty()) s.fg = Color::rgb(0xB45309);
        if (!a && c != 0 && c != 1) s.fg = Color::rgb(0x94A3B8);
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
    [[nodiscard]] bool editable(RowIndex r, std::size_t c) const override { return draft && r < rows.size() && (c == 1 || c == 3 || c == 4); }
    [[nodiscard]] std::vector<std::string> cellChoices(RowIndex, std::size_t c) const override {
        if (c == 1) return {"Oui", "Non"};
        return {};
    }
    // Le montant propose quand un article entre en vente : le prix affiche (TTC), sinon 1,00 EUR.
    [[nodiscard]] shop::Article fresh(RowIndex r) const {
        const CatalogueRow& row = rows[r];
        shop::Article a;
        a.kind = row.kind;
        a.edition = row.edition;
        a.option = row.option;
        a.days = row.days;
        a.id = row.id();
        std::int64_t v = 0;
        a.amount = !row.price.empty() && shownPriceTtc(row.price, v) && v >= shop::kMinAmount ? v : 100;
        a.label = row.kind == "option" ? "Option " + row.name : "Licence " + row.name;
        if (a.label.size() > 110) a.label.resize(110);
        return a;
    }
    bool setCellText(RowIndex r, std::size_t c, std::string_view text) override {
        if (!draft || r >= rows.size()) return false;
        refusal.clear();
        const std::string id = rows[r].id();
        auto it = std::find_if(draft->articles.begin(), draft->articles.end(), [&](const shop::Article& a) { return a.id == id; });
        const std::string t(text);
        if (c == 1) {
            const bool on = t == "Oui" || t == "oui" || t == "1" || t == "true";
            if (on && it == draft->articles.end()) draft->articles.push_back(fresh(r));
            if (!on && it != draft->articles.end()) draft->articles.erase(it);
        } else if (c == 3) {
            std::int64_t v = 0;
            if (!shop::parseAmount(t, v) || v < shop::kMinAmount || v > shop::kMaxAmount) {
                refusal = "Montant illisible ou hors limites (de 0,50 \xE2\x82\xAC \xC3\xA0 1 000 000 \xE2\x82\xAC TTC) : \xC2\xAB " + t + " \xC2\xBB.";
                if (changed) changed();
                return false;
            }
            if (it == draft->articles.end()) {
                draft->articles.push_back(fresh(r));
                it = draft->articles.end() - 1;
            }
            it->amount = v;
        } else if (c == 4) {
            std::string l = t;
            while (!l.empty() && l.back() == ' ') l.pop_back();
            if (l.empty() || l.size() > 360) {
                refusal = "Le libell\xC3\xA9 de la facture : une ligne, 1 \xC3\xA0 120 caract\xC3\xA8res.";
                if (changed) changed();
                return false;
            }
            if (it == draft->articles.end()) {
                draft->articles.push_back(fresh(r));
                it = draft->articles.end() - 1;
            }
            it->label = l;
        } else {
            return false;
        }
        modelReset->emit();
        if (changed) changed();
        return true;
    }
    void reset() { modelReset->emit(); }
};

// =================================================================================== la page Boutique
class ShopPage final : public Page {
public:
    explicit ShopPage(ShellHost host) : Page("page.boutique", std::move(host)) {
        reload_ = &button(*this, "Relever", "boutique.relever", ui::Button::Style::Default, ui::Icon::Refresh);
        reload_->setTooltip("Relire tout de suite le r\xC3\xA9glage de la boutique et l'\xC3\xA9tat de Stripe sur le service.");
        connect_ = &button(*this, "Brancher Stripe\xE2\x80\xA6", "boutique.brancher", ui::Button::Style::Primary, ui::Icon::Lock);
        dashboard_ = &button(*this, "Ouvrir mon tableau de bord Stripe", "boutique.tableau-stripe", ui::Button::Style::Default, ui::Icon::GoTo);
        deploy_ = &button(*this, "D\xC3\xA9ployer le service", "boutique.deployer", ui::Button::Style::Primary, ui::Icon::Export);
        save_ = &button(*this, "Enregistrer", "boutique.enregistrer", ui::Button::Style::Primary, ui::Icon::Ok);
        cancel_ = &button(*this, "Annuler les changements", "boutique.annuler");
        active_ = &adopt(*this, std::make_unique<Switch>("Vendre en ligne", "boutique.vendre"));
        auto_ = &adopt(*this, std::make_unique<Switch>("Livrer seul les commandes pay\xC3\xA9" "es", "boutique.livrer-seul"));
        table_ = &adopt(*this, std::make_unique<ui::TableView>("boutique.catalogue"));
        table_->setColumns({{"Article", 300.f}, {"En vente", 90.f}, {"Prix affich\xC3\xA9", 170.f}, {"Montant encaiss\xC3\xA9 (TTC)", 180.f},
                            {"Libell\xC3\xA9 de la facture", 300.f}, {"\xC3\x89" "cart", 330.f}});
        table_->setSelectionMode(ui::SelectionMode::Single);
        model_ = std::make_shared<CatalogueModel>();
        model_->draft = &draft_;
        model_->changed = [this] {
            dirty_ = true;
            update();
        };
        table_->setModel(model_);
        links_ += reload_->clicked->connect([this] { fetch(true); });
        links_ += connect_->clicked->connect([this] {
            if (host_.openDialog) host_.openDialog(makeStripeDialog(host_, [this, alive = alive_] {
                if (*alive) fetch(true);
            }));
        });
        links_ += dashboard_->clicked->connect([this] { openUrl(host_, shop::dashboardUrl(view().stripe.mode)); });
        links_ += deploy_->clicked->connect([this] {
            if (host_.openDialog) host_.openDialog(makeDeployDialog(host_, [this, alive = alive_] {
                if (*alive) update();
            }));
        });
        links_ += active_->toggled->connect([this](bool on) {
            if (syncing_) return;
            draft_.active = on;
            dirty_ = true;
            update();
        });
        links_ += auto_->toggled->connect([this](bool on) {
            if (syncing_) return;
            draft_.autoDeliver = on;
            dirty_ = true;
            update();
        });
        links_ += save_->clicked->connect([this] { save(); });
        links_ += cancel_->clicked->connect([this] {
            dirty_ = false;
            takeDraft();
            update();
        });
        refresh();
    }
    ~ShopPage() override {
        links_.clear();
        *alive_ = false;
    }

    [[nodiscard]] std::string title() const override { return "Boutique"; }

    void refresh() override {
        if (!dirty_) takeDraft();
        // Un service plus ancien que la 1.46.0 : rien a relever (le bandeau dit de deployer).
        if (ws().service().ready() && ws().signer() && shopServiceNote(ws()).empty() && !fetching_ && (lastFetch_ < 0 || steadySeconds() - lastFetch_ > 30.0))
            fetch(false);
        update();
    }

    // Les essais.
    [[nodiscard]] shop::Settings& draft() noexcept { return draft_; }
    [[nodiscard]] CatalogueModel& catalogue() noexcept { return *model_; }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float pad = b.w < 900.f ? 20.f : 28.f;
        x0_ = b.x + pad;
        w_ = std::max(560.f, b.w - 2.f * pad);
        titleY_ = b.y + 22.f;
        const float rw = buttonWidth(*reload_, 110.f);
        reload_->setBounds({x0_ + w_ - rw, titleY_ + 2.f, rw, 36.f});
        float y = titleY_ + 70.f;
        note_ = shopServiceNote(ws());
        show(deploy_, !note_.empty());
        if (!note_.empty()) {
            noteRect_ = {x0_, y, w_, 56.f};
            const float dw = buttonWidth(*deploy_, 170.f);
            deploy_->setBounds({noteRect_.right() - 14.f - dw, noteRect_.y + 10.f, dw, 36.f});
            y = noteRect_.bottom() + 14.f;
        } else {
            noteRect_ = {};
        }
        const float cardW = (w_ - 16.f) * 0.5f;
        stripeRect_ = {x0_, y, cardW, 150.f};
        saleRect_ = {x0_ + cardW + 16.f, y, cardW, 150.f};
        const float cw = buttonWidth(*connect_, 150.f), dbw = std::min(cardW - cw - 48.f, buttonWidth(*dashboard_, 200.f));
        connect_->setBounds({stripeRect_.x + 18.f, stripeRect_.bottom() - 50.f, cw, 34.f});
        dashboard_->setBounds({stripeRect_.x + 18.f + cw + 10.f, stripeRect_.bottom() - 50.f, std::max(120.f, dbw), 34.f});
        active_->setBounds({saleRect_.x + 18.f, saleRect_.y + 46.f, 40.f, 22.f});
        auto_->setBounds({saleRect_.x + 18.f, saleRect_.y + 88.f, 40.f, 22.f});
        y = stripeRect_.bottom() + 18.f;
        catY_ = y;
        const float sw = buttonWidth(*save_, 130.f), aw = buttonWidth(*cancel_, 120.f);
        save_->setBounds({x0_ + w_ - sw, y - 2.f, sw, 34.f});
        cancel_->setBounds({save_->bounds().x - 8.f - aw, y - 2.f, aw, 34.f});
        show(cancel_, dirty_);
        y += 56.f;
        table_->setBounds({x0_, y, w_, std::max(120.f, b.bottom() - 18.f - y)});
        if (std::fabs(w_ - fitW_) > 0.5f) {   // les colonnes a la largeur (aucune hors de la vue)
            fitW_ = w_;
            fitColumns(*table_,
                       {{"Article", 0.f, 3.f, 180.f, false},
                        {"En vente", 84.f, 0.f, 0.f, false},
                        {"Prix affich\xC3\xA9", 130.f, 0.f, 0.f, false},
                        {"Montant encaiss\xC3\xA9 (TTC)", 170.f, 0.f, 0.f, false},
                        {"Libell\xC3\xA9 de la facture", 0.f, 3.f, 150.f, false},
                        {"\xC3\x89" "cart", 0.f, 2.f, 150.f, false}},
                       w_);
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        r.fillRect(bounds(), c.windowBg);
        r.drawText({x0_, titleY_}, "Boutique", gfx::semibold(kF28), c.text);
        std::string lead = "La vente en ligne : Stripe encaisse sur sa page s\xC3\xA9" "curis\xC3\xA9" "e (la carte n'est ni vue ni gard\xC3\xA9" "e, redemand\xC3\xA9" "e \xC3\xA0 chaque achat), ton Admin livre la licence.";
        // Un service trop ancien : le bandeau le dit, une seule fois (pas aussi la panne du releve).
        const bool showProblem = !problem_.empty() && note_.empty();
        if (fetching_) lead = "Relev\xC3\xA9 en cours\xE2\x80\xA6";
        else if (showProblem) lead = "Service : " + problemText(problem_) + ".";
        r.drawText({x0_, titleY_ + 40.f}, fit(r, lead, kF14, reload_->bounds().x - 16.f - x0_), kF14, showProblem ? c.warning : c.textMuted);
        if (!note_.empty()) {
            r.fillRoundedRect(noteRect_, c.warning.withAlpha(90), 10.f);
            r.fillRoundedRect(noteRect_.inset(1.f, 1.f), c.panelBg, 9.f);
            r.fillRoundedRect(noteRect_.inset(1.f, 1.f), c.warning.withAlpha(24), 9.f);
            ui::drawIcon(r, ui::Icon::Warning, {noteRect_.x + 14.f, noteRect_.y + 18.f, 18.f, 18.f}, c.warning);
            r.drawText({noteRect_.x + 42.f, noteRect_.y + 18.f}, fit(r, note_, gfx::semibold(kF14), deploy_->bounds().x - noteRect_.x - 56.f), gfx::semibold(kF14), c.text);
        }
        paintStripe(ctx);
        paintSale(ctx);
        r.drawText({x0_, catY_}, "Catalogue", gfx::semibold(kF16), c.text);
        std::string hint = "Chaque offre et chaque option du registre : double-clic sur \xC2\xAB En vente \xC2\xBB, \xC2\xAB Montant \xC2\xBB ou \xC2\xAB Libell\xC3\xA9 \xC2\xBB pour les changer, puis Enregistrer.";
        Color hc = c.textMuted;
        if (!model_->refusal.empty()) {
            hint = model_->refusal;
            hc = c.error;
        } else if (!ws().hasRegistry()) {
            hint = "Pas de registre sign\xC3\xA9 dans l'archive : les offres et les options viennent de lui (Offres \xE2\x80\xBA Signer le registre).";
            hc = c.warning;
        } else if (dirty_) {
            hint = "Changements pas encore enregistr\xC3\xA9s (" + std::to_string(draft_.articles.size()) + " article(s) en vente) : Enregistrer les envoie au service.";
            hc = c.warning;
        }
        r.drawText({x0_, catY_ + 26.f}, fit(r, hint, kF13, (dirty_ ? cancel_->bounds().x : save_->bounds().x) - 16.f - x0_), kF13, hc);
    }

private:
    [[nodiscard]] const shop::Settings& view() const { return ws().shop(); }
    [[nodiscard]] bool busy() const { return host_.busy && host_.busy(); }

    void paintStripe(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        cardBox(r, ctx.theme, stripeRect_);
        const float x = stripeRect_.x + 18.f;
        r.drawText({x, stripeRect_.y + 16.f}, "Stripe", gfx::semibold(kF16), c.text);
        const shop::StripeState& s = view().stripe;
        const bool known = ws().hasShop() && note_.empty();
        std::string p = !note_.empty() ? "service " + ws().service().state.version : !known ? "pas encore lu" : shop::modeLabel(s.connected ? s.mode : std::string());
        Tint t{rgb(0x475569), rgb(0xF1F5F9)};
        if (known && s.connected && s.mode == "test") t = {rgb(0xB45309), rgb(0xFFF4E5)};
        if (known && s.connected && s.mode == "reel") t = {rgb(0x15803D), rgb(0xECFDF3)};
        pill(r, x + 70.f, stripeRect_.y + 14.f, p, kF12, t.fg, t.bg, 22.f, true);
        std::string l1, l2;
        if (!ws().service().ready()) l1 = "Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne).";
        else if (!note_.empty()) l1 = "Stripe se branche sur ton service une fois en " + std::string(shop::kServiceRequired) + " (D\xC3\xA9ployer le service).";
        else if (!known) l1 = "L'\xC3\xA9tat de Stripe se lit sur ton service (Relever).";
        else if (!s.connected) l1 = "Pas branch\xC3\xA9 : colle ta cl\xC3\xA9 secr\xC3\xA8te Stripe (Brancher Stripe\xE2\x80\xA6), le reste se fait seul.";
        else {
            l1 = std::string("Branch\xC3\xA9") + (s.mode == "test" ? " en mode test : paiement d'essai, aucune carte r\xC3\xA9" "elle n'est d\xC3\xA9" "bit\xC3\xA9" "e." : " en mode r\xC3\xA9" "el : de vrais paiements.");
            l2 = !s.webhook ? std::string("Le secret du webhook manque : branche Stripe de nouveau.")
                 : s.lastEvent ? "Webhook re\xC3\xA7u pour la derni\xC3\xA8re fois " + agoFr(ws().now(), s.lastEvent) + "."
                               : std::string("Webhook en place : aucun \xC3\xA9v\xC3\xA9nement re\xC3\xA7u pour l'instant.");
        }
        r.drawText({x, stripeRect_.y + 48.f}, fit(r, l1, kF14, stripeRect_.w - 36.f), kF14, c.text);
        if (!l2.empty()) r.drawText({x, stripeRect_.y + 70.f}, fit(r, l2, kF13, stripeRect_.w - 36.f), kF13, c.textMuted);
    }

    void paintSale(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        cardBox(r, ctx.theme, saleRect_);
        const float x = saleRect_.x + 18.f;
        r.drawText({x, saleRect_.y + 16.f}, "Vente en ligne", gfx::semibold(kF16), c.text);
        const float tx = x + 52.f, tw = saleRect_.right() - 18.f - tx;
        r.drawText({tx, saleRect_.y + 44.f}, "Vendre en ligne", gfx::semibold(kF14), c.text);
        r.drawText({tx, saleRect_.y + 63.f}, fit(r, "Les Studios montrent \xC2\xAB Acheter \xE2\x80\x94 montant TTC \xC2\xBB sur ce qui est en vente.", kF12, tw), kF12, c.textMuted);
        r.drawText({tx, saleRect_.y + 86.f}, "Livrer seul les commandes pay\xC3\xA9" "es", gfx::semibold(kF14), c.text);
        r.drawText({tx, saleRect_.y + 105.f}, fit(r, "L'Admin ouvert signe et d\xC3\xA9pose la licence d\xC3\xA8s le paiement (sinon : Commandes \xE2\x80\xBA D\xC3\xA9livrer).", kF12, tw), kF12,
                   c.textMuted);
        std::string st;
        Color sc = c.textMuted;
        if (!note_.empty()) {
            st = "Pas encore : ton service doit passer en " + std::string(shop::kServiceRequired) + ".";
        } else if (ws().hasShop()) {
            if (view().open) {
                st = "Ouverte : " + std::to_string(view().articles.size()) + " article(s) achetable(s) dans les Studios.";
                sc = c.ok;
            } else {
                st = !view().stripe.connected ? "Ferm\xC3\xA9" "e : Stripe n'est pas branch\xC3\xA9." : !view().active ? "Ferm\xC3\xA9" "e : la vente en ligne est arr\xC3\xAAt\xC3\xA9" "e."
                                                                                   : "Ferm\xC3\xA9" "e : aucun article en vente.";
            }
        }
        if (!st.empty()) r.drawText({x, saleRect_.y + 126.f}, fit(r, st, kF13, saleRect_.w - 36.f), kF13, sc);
    }

    // Le brouillon : le reglage releve (ou vide) ; les lignes du catalogue : le registre.
    void takeDraft() {
        draft_ = ws().hasShop() ? ws().shop() : shop::Settings{};
        model_->rows.clear();
        model_->refusal.clear();
        if (ws().hasRegistry()) {
            const auto& reg = ws().registry();
            for (const auto& e : reg.editions) {
                if (e.retired || e.type == licensing::LicenseType::Evaluation || e.id == reg.unlicensedEdition) continue;
                CatalogueRow row;
                row.kind = "edition";
                row.edition = e.id;
                row.name = (e.label.empty() ? e.id : e.label) + " - " + daysText(e.durationDays, false);
                row.price = e.price;
                model_->rows.push_back(std::move(row));
            }
            for (const auto& o : reg.options) {
                if (o.retired) continue;
                for (const auto& d : o.durations) {
                    CatalogueRow row;
                    row.kind = "option";
                    row.option = o.id;
                    row.days = d.days;
                    row.name = (o.label.empty() ? o.id : o.label) + " - " + daysText(d.days, true);
                    row.price = d.price;
                    model_->rows.push_back(std::move(row));
                }
            }
        }
        // Un article du reglage que le registre n'a plus : montre quand meme (a retirer de la vente).
        for (const auto& a : draft_.articles)
            if (std::none_of(model_->rows.begin(), model_->rows.end(), [&](const CatalogueRow& x) { return x.id() == a.id; })) {
                CatalogueRow row;
                row.kind = a.kind;
                row.edition = a.edition;
                row.option = a.option;
                row.days = a.days;
                row.name = a.id + " (plus au registre)";
                model_->rows.push_back(std::move(row));
            }
        model_->reset();
    }

    void update() {
        const bool plus = ws().role() == Role::Plus;
        const bool ready = ws().service().ready() && ws().signer();
        const bool bz = busy();
        note_ = shopServiceNote(ws());
        // Les deux interrupteurs disent le reglage RELEVE : pas encore lu, ou un service trop ancien - eteints et grises.
        const bool known = ws().hasShop() && note_.empty();
        syncing_ = true;
        active_->setChecked(known && draft_.active);
        auto_->setChecked(known && draft_.autoDeliver);
        syncing_ = false;
        active_->setEnabled(plus && ready && known);
        auto_->setEnabled(plus && ready && known);
        reload_->setEnabled(ready && !fetching_);
        connect_->setEnabled(plus && ready && !bz && note_.empty());
        connect_->setText(known && view().stripe.connected ? std::string("Rebrancher Stripe\xE2\x80\xA6") : std::string("Brancher Stripe\xE2\x80\xA6"));
        connect_->setTooltip(!plus ? std::string("R\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+ (la cl\xC3\xA9 de l'\xC3\xA9" "diteur).")
                                   : std::string("Colle ta cl\xC3\xA9 secr\xC3\xA8te Stripe : l'Admin cr\xC3\xA9" "e le webhook chez Stripe et pose les deux secrets sur ton service "
                                                 "(jamais gard\xC3\xA9s sur ce PC)."));
        dashboard_->setEnabled(true);
        save_->setEnabled(plus && ready && !bz && dirty_ && note_.empty());
        save_->setTooltip(!plus ? std::string("R\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+.") : std::string("Envoie le catalogue et les deux r\xC3\xA9glages au service (PUT /admin/boutique)."));
        show(cancel_, dirty_);
        invalidateLayout();
        invalidate();
    }

    void fetch(bool loud) {
        (void)loud;
        if (fetching_ || !shopServiceNote(ws()).empty()) return;
        fetching_ = true;
        lastFetch_ = steadySeconds();
        auto alive = alive_;
        if (!fetchShop(host_, alive, [this, alive](const OpResult& r) {
                if (!*alive) return;
                fetching_ = false;
                problem_ = r.ok ? std::string() : r.message;
                if (!dirty_) takeDraft();
                update();
            }))
            fetching_ = false;
        update();
    }

    void save() {
        if (busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return;
        }
        std::string why;
        if (!shop::validSettings(draft_, &why)) {
            if (host_.report) host_.report(OpResult::fail(why));
            return;
        }
        const Workspace* wp = &ws();
        auto s = std::make_shared<shop::Settings>(draft_);
        auto alive = alive_;
        (void)host_.runSlow("Boutique : enregistrement", [wp, s] { return wp->saveShop(*s); },
                            [this, alive, s](const OpResult& r) {
                                if (!*alive || !r.ok) return;
                                dirty_ = false;
                                lastFetch_ = -1;
                                fetch(false);
                            });
    }

    std::shared_ptr<bool>           alive_ = std::make_shared<bool>(true);
    ui::Button*                     reload_{nullptr};
    ui::Button*                     connect_{nullptr};
    ui::Button*                     dashboard_{nullptr};
    ui::Button*                     deploy_{nullptr};
    ui::Button*                     save_{nullptr};
    ui::Button*                     cancel_{nullptr};
    Switch*                         active_{nullptr};
    Switch*                         auto_{nullptr};
    ui::TableView*                  table_{nullptr};
    std::shared_ptr<CatalogueModel> model_;
    shop::Settings                  draft_;
    bool                            dirty_{false}, syncing_{false}, fetching_{false};
    double                          lastFetch_{-1};
    std::string                     problem_, note_;
    float                           x0_{0}, w_{0}, titleY_{0}, catY_{0}, fitW_{-1.f};
    Rect                            noteRect_{}, stripeRect_{}, saleRect_{};
    core::ConnectionScope           links_;
};

// =================================================================================== les commandes (modele)
Color stateInk(const std::string& s) {
    if (s == "payee") return Color::rgb(0xB45309);
    if (s == "livree") return Color::rgb(0x15803D);
    if (s == "a_revoir" || s == "echec_paiement") return Color::rgb(0xDC2626);
    if (s == "a_payer") return Color::rgb(0x2563EB);
    return Color::rgb(0x64748B);
}

class OrdersModel final : public ui::ITableModel {
public:
    std::vector<shop::Order> rows;
    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 7; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* h[] = {"N\xC2\xB0", "Date", "Soci\xC3\xA9t\xC3\xA9", "Article", "Montant", "\xC3\x89tat", "Client"};
        return c < 7 ? h[c] : "";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= rows.size()) return {};
        const auto& o = rows[r];
        switch (c) {
            case 0: return std::to_string(o.number);
            case 1: return dateTimeFr(o.created);
            case 2: return o.company;
            case 3: return o.label.empty() ? o.article : o.label;
            case 4: return shop::amountText(o.amount, o.currency);   // un paiement d'essai : la pastille du detail le dit
            case 5: return shop::stateLabel(o.state);
            case 6: return o.name.empty() ? o.email : o.name;
            default: return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r < rows.size() && c == 5) {
            s.fg = stateInk(rows[r].state);
            s.bold = rows[r].state == "payee" || rows[r].state == "a_revoir";
            s.keepFgSelected = true;   // l'etat garde sa couleur sur la ligne choisie
        }
        if (c == 0) s.monospace = true;
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t c) const override {
        if (c == 0 || c == 1) return rows[a].number < rows[b].number;
        if (c == 4) return rows[a].amount < rows[b].amount;
        return cellText(a, c) < cellText(b, c);
    }
    void reset() { modelReset->emit(); }
};

// =================================================================================== une question (Rembourser, Revoir)
class AskBody final : public ui::Widget {
public:
    AskBody(Dialog& owner, std::string title, std::string text, bool withReason, std::string okLabel, bool danger,
            std::function<void(const std::string&)> onOk)
        : ui::Widget("dialog.boutique.question"), owner_(owner), title_(std::move(title)), text_(std::move(text)), withReason_(withReason),
          onOk_(std::move(onOk)) {
        reason_ = &adopt(*this, std::make_unique<ui::InputText>("question.raison"));
        reason_->setMaxLength(400);
        show(reason_, withReason_);
        cancel_ = &button(*this, "Annuler", "question.annuler");
        ok_ = &button(*this, std::move(okLabel), "question.ok", danger ? ui::Button::Style::Danger : ui::Button::Style::Primary);
        links_ += cancel_->clicked->connect([this] { owner_.close(); });
        links_ += ok_->clicked->connect([this] { accept(); });
    }

protected:
    void onLayout() override {
        frame_.place(*this, 560.f, withReason_ ? 330.f : 280.f);
        const auto& p = frame_.panel;
        textH_ = wrappedHeight(text_, kF14, p.w - 48.f, 20.f, 6);
        reason_->setBounds({p.x + 24.f, p.y + 76.f + textH_ + 14.f, p.w - 48.f, 34.f});
        const float by = p.bottom() - 54.f;
        const float ow = buttonWidth(*ok_, 140.f), cw = buttonWidth(*cancel_, 100.f);
        ok_->setBounds({p.right() - 24.f - ow, by, ow, 34.f});
        cancel_->setBounds({ok_->bounds().x - 8.f - cw, by, cw, 34.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& p = frame_.panel;
        const float y = frame_.paint(ctx.r, ctx.theme, title_, {});
        (void)drawWrapped(ctx.r, p.x + 24.f, y + 16.f, p.w - 48.f, text_, kF14, ctx.theme.color.text, 20.f, 6);
        if (!error_.empty()) ctx.r.drawText({p.x + 24.f, p.bottom() - 84.f}, fit(ctx.r, error_, kF13, p.w - 48.f), kF13, ctx.theme.color.error);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (frame_.hitClose(ev)) {
            owner_.close();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    void accept() {
        std::string t = reason_->text();
        while (!t.empty() && t.back() == ' ') t.pop_back();
        if (withReason_ && t.empty()) {
            error_ = "Dis pourquoi (une ligne).";
            invalidate();
            return;
        }
        auto run = onOk_;
        owner_.close();
        if (run) run(t);
    }
    Dialog&                                 owner_;
    std::string                             title_, text_, error_;
    bool                                    withReason_{false};
    std::function<void(const std::string&)> onOk_;
    ui::InputText*                          reason_{nullptr};
    ui::Button*                             cancel_{nullptr};
    ui::Button*                             ok_{nullptr};
    Frame                                   frame_;
    float                                   textH_{0};
    core::ConnectionScope                   links_;
};

class AskDialog final : public Dialog {
public:
    AskDialog(std::string id, std::string title, std::string text, bool withReason, std::string okLabel, bool danger, std::function<void(const std::string&)> onOk)
        : Dialog("dialog." + std::move(id)), title_(std::move(title)), text_(std::move(text)), withReason_(withReason), ok_(std::move(okLabel)), danger_(danger),
          onOk_(std::move(onOk)) {}
    [[nodiscard]] std::string title() const override { return title_; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<AskBody>(*this, title_, text_, withReason_, ok_, danger_, onOk_));
        return core::ok();
    }

private:
    std::string                             title_, text_;
    bool                                    withReason_{false};
    std::string                             ok_;
    bool                                    danger_{false};
    std::function<void(const std::string&)> onOk_;
};

// =================================================================================== la page Commandes
class OrdersPage final : public Page {
public:
    explicit OrdersPage(ShellHost host) : Page("page.commandes", std::move(host)) {
        reload_ = &button(*this, "Relever", "commandes.relever", ui::Button::Style::Default, ui::Icon::Refresh);
        deploy_ = &button(*this, "D\xC3\xA9ployer le service", "commandes.deployer", ui::Button::Style::Primary, ui::Icon::Export);
        filter_ = &adopt(*this, std::make_unique<ui::DropDown>("commandes.filtre"));
        filter_->setItems({{"Toutes", ""},
                           {"\xC3\x80 livrer (pay\xC3\xA9" "es)", "payee"},
                           {"\xC3\x80 revoir", "a_revoir"},
                           {"En attente du paiement", "a_payer"},
                           {"Livr\xC3\xA9" "es", "livree"},
                           {"Rembours\xC3\xA9" "es", "remboursee"},
                           {"Expir\xC3\xA9" "es, annul\xC3\xA9" "es, refus\xC3\xA9" "es", "closes"}});
        filter_->setSelectedIndex(0);
        table_ = &adopt(*this, std::make_unique<ui::TableView>("commandes.table"));
        table_->setColumns({{"N\xC2\xB0", 44.f}, {"Date", 118.f}, {"Soci\xC3\xA9t\xC3\xA9", 150.f}, {"Article", 200.f}, {"Montant", 104.f}, {"\xC3\x89tat", 150.f}, {"Client", 120.f}});
        table_->setSelectionMode(ui::SelectionMode::Single);
        model_ = std::make_shared<OrdersModel>();
        table_->setModel(model_);
        deliver_ = &button(*this, "D\xC3\xA9livrer maintenant", "commandes.delivrer", ui::Button::Style::Primary, ui::Icon::Ok);
        stripe_ = &button(*this, "Voir dans Stripe", "commandes.stripe", ui::Button::Style::Default, ui::Icon::GoTo);
        receipt_ = &button(*this, "Ouvrir le re\xC3\xA7u", "commandes.recu", ui::Button::Style::Default, ui::Icon::Document);
        review_ = &button(*this, "Revoir\xE2\x80\xA6", "commandes.revoir");
        refund_ = &button(*this, "Rembourser\xE2\x80\xA6", "commandes.rembourser", ui::Button::Style::Danger);
        links_ += reload_->clicked->connect([this] { fetch(); });
        links_ += deploy_->clicked->connect([this] {
            if (host_.openDialog) host_.openDialog(makeDeployDialog(host_, [this, alive = alive_] {
                if (*alive) update();
            }));
        });
        links_ += filter_->selectionChanged->connect([this](int) { rebuild(); });
        links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>&) { update(); });
        links_ += deliver_->clicked->connect([this] { deliver(); });
        links_ += stripe_->clicked->connect([this] {
            if (const shop::Order* o = selected()) openUrl(host_, shop::dashboardUrl(o->mode, o->stripePayment));
        });
        links_ += receipt_->clicked->connect([this] {
            if (const shop::Order* o = selected(); o && o->receiptUrl.rfind("https://", 0) == 0) openUrl(host_, o->receiptUrl);
        });
        links_ += review_->clicked->connect([this] { askReview(); });
        links_ += refund_->clicked->connect([this] { askRefund(); });
        refresh();
    }
    ~OrdersPage() override {
        links_.clear();
        *alive_ = false;
    }

    [[nodiscard]] std::string title() const override { return "Commandes"; }

    void refresh() override {
        if (ws().service().ready() && ws().can("demandes") && shopServiceNote(ws()).empty() && !fetching_ && (lastFetch_ < 0 || steadySeconds() - lastFetch_ > 20.0))
            fetch();
        rebuild();
    }

    // Les essais : choisir la commande n.
    bool select(int number) {
        for (std::size_t i = 0; i < model_->rows.size(); ++i)
            if (model_->rows[i].number == number) {
                table_->selectModelRows({static_cast<RowIndex>(i)});
                update();
                return true;
            }
        return false;
    }
    [[nodiscard]] std::size_t shownCount() const { return model_->rows.size(); }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float pad = b.w < 900.f ? 20.f : 28.f;
        x0_ = b.x + pad;
        w_ = std::max(600.f, b.w - 2.f * pad);
        titleY_ = b.y + 22.f;
        const float rw = buttonWidth(*reload_, 110.f), fw = 260.f;
        reload_->setBounds({x0_ + w_ - rw, titleY_ + 2.f, rw, 36.f});
        filter_->setBounds({reload_->bounds().x - 10.f - fw, titleY_ + 6.f, fw, 30.f});
        float y = titleY_ + 70.f;
        note_ = shopServiceNote(ws());
        show(deploy_, !note_.empty());
        if (!note_.empty()) {
            noteRect_ = {x0_, y, w_, 56.f};
            const float dw = buttonWidth(*deploy_, 170.f);
            deploy_->setBounds({noteRect_.right() - 14.f - dw, noteRect_.y + 10.f, dw, 36.f});
            y = noteRect_.bottom() + 14.f;
        } else {
            noteRect_ = {};
        }
        const float detailW = std::clamp(w_ * 0.30f, 300.f, 430.f);
        const float h = std::max(160.f, b.bottom() - 18.f - y);
        const float tw = w_ - detailW - 16.f;
        table_->setBounds({x0_, y, tw, h});
        if (std::fabs(tw - fitW_) > 0.5f) {   // les colonnes a la largeur : l'etat toujours en vue ; le client (dans le detail) cede le premier
            fitW_ = tw;
            fitColumns(*table_,
                       {{"N\xC2\xB0", 44.f, 0.f, 0.f, false},
                        {"Date", 120.f, 0.f, 0.f, false},
                        {"Soci\xC3\xA9t\xC3\xA9", 0.f, 3.f, 120.f, false},
                        {"Article", 0.f, 4.f, 170.f, false},
                        {"Montant", 104.f, 0.f, 0.f, false},
                        {"\xC3\x89tat", 150.f, 0.f, 0.f, false},
                        {"Client", 0.f, 2.f, 100.f, true}},
                       tw);
        }
        detail_ = {x0_ + w_ - detailW, y, detailW, h};
        // Les boutons du detail, en bas du volet.
        float by = detail_.bottom() - 16.f - 34.f;
        const float bx = detail_.x + 18.f, bw = detail_.w - 36.f;
        for (ui::Button* btn : {refund_, review_, receipt_, stripe_, deliver_}) {
            if (!btn->visible()) continue;
            btn->setBounds({bx, by, bw, 34.f});
            by -= 42.f;
        }
        buttonsTop_ = by + 34.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(bounds(), c.windowBg);
        r.drawText({x0_, titleY_}, "Commandes", gfx::semibold(kF28), c.text);
        r.drawText({x0_, titleY_ + 40.f}, fit(r, status(), kF14, filter_->bounds().x - 16.f - x0_), kF14, problem_.empty() || !note_.empty() ? c.textMuted : c.warning);
        if (!note_.empty()) {
            r.fillRoundedRect(noteRect_, c.warning.withAlpha(90), 10.f);
            r.fillRoundedRect(noteRect_.inset(1.f, 1.f), c.panelBg, 9.f);
            r.fillRoundedRect(noteRect_.inset(1.f, 1.f), c.warning.withAlpha(24), 9.f);
            ui::drawIcon(r, ui::Icon::Warning, {noteRect_.x + 14.f, noteRect_.y + 18.f, 18.f, 18.f}, c.warning);
            r.drawText({noteRect_.x + 42.f, noteRect_.y + 18.f}, fit(r, note_, gfx::semibold(kF14), deploy_->bounds().x - noteRect_.x - 56.f), gfx::semibold(kF14), c.text);
        }
        paintDetail(ctx);
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!model_->rows.empty()) return;
        const Rect t = table_->bounds();
        (void)drawWrapped(ctx.r, t.x + 16.f, t.y + 44.f, t.w - 32.f, empty(), kF14, ctx.theme.color.textMuted, 20.f, 6);
    }

private:
    [[nodiscard]] bool busy() const { return host_.busy && host_.busy(); }
    [[nodiscard]] const shop::Order* selected() const {
        const auto sel = table_->selectedModelRows();
        return sel.empty() || sel.front() >= model_->rows.size() ? nullptr : &model_->rows[sel.front()];
    }

    [[nodiscard]] std::string status() const {
        if (fetching_) return "Relev\xC3\xA9 des commandes en cours\xE2\x80\xA6";
        if (!problem_.empty() && note_.empty()) return "Service : " + problemText(problem_) + ".";
        const auto& w = ws();
        if (!w.service().ready()) return "Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne).";
        if (!w.can("demandes")) return w.whyNot("demandes");
        if (!w.hasOrders()) return "Les commandes de la boutique (pay\xC3\xA9" "es sur la page de Stripe) : pas encore relev\xC3\xA9" "es.";
        std::string t = std::to_string(w.orders().orders.size()) + " commande(s) \xC2\xB7 " + std::to_string(w.orders().toDeliver) + " \xC3\xA0 livrer";
        if (w.hasShop()) t += std::string(" \xC2\xB7 livraison automatique ") + (w.shop().autoDeliver ? "en marche" : "arr\xC3\xAAt\xC3\xA9" "e (D\xC3\xA9livrer maintenant)");
        if (w.hasShop() && w.shop().stripe.connected) t += " \xC2\xB7 " + shop::modeLabel(w.shop().stripe.mode);
        return t;
    }
    [[nodiscard]] std::string empty() const {
        const auto& w = ws();
        if (!w.service().ready()) return "Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 : la boutique en a besoin (Service en ligne).";
        if (!w.can("demandes")) return w.whyNot("demandes");
        if (!w.hasOrders()) return "Pas encore relev\xC3\xA9" "es : clique \xC2\xAB Relever \xC2\xBB.";
        if (!w.orders().orders.empty()) return "Aucune commande dans ce filtre.";
        return "Aucune commande pour l'instant. Les clients ach\xC3\xA8tent depuis leur Studio (Licence \xE2\x80\xBA Demander ma licence) une fois la boutique ouverte (Boutique).";
    }

    void rebuild() {
        const int keep = selected() ? selected()->number : 0;
        const auto* it = filter_->selectedItem();
        const std::string want = it ? it->value : std::string();
        model_->rows.clear();
        if (ws().hasOrders())
            for (const auto& o : ws().orders().orders) {
                const bool closes = o.state == "expiree" || o.state == "annulee" || o.state == "echec_paiement";
                if (!want.empty() && !(want == "closes" ? closes : o.state == want)) continue;
                model_->rows.push_back(o);
            }
        model_->reset();
        if (keep) (void)select(keep);
        update();
    }

    void update() {
        const shop::Order* o = selected();
        const bool bz = busy();
        note_ = shopServiceNote(ws());
        const bool ready = ws().service().ready() && ws().can("demandes");
        reload_->setEnabled(ready && !fetching_);
        for (ui::Button* btn : {deliver_, stripe_, receipt_, review_, refund_}) show(btn, o != nullptr);
        if (o) {
            const std::string why = ws().whyNotDeliver(o->kind);
            deliver_->setEnabled(o->deliverable() && why.empty() && !bz && note_.empty());
            deliver_->setTooltip(!o->deliverable() ? "Cette commande n'est pas \xC3\xA0 livrer (" + shop::stateLabel(o->state) + ")."
                                 : !why.empty() ? why
                                                : std::string("Signer la licence et la d\xC3\xA9poser tout de suite (le code part scell\xC3\xA9 au PC du client)."));
            stripe_->setEnabled(!o->stripePayment.empty() || !o->stripeSession.empty());
            receipt_->setEnabled(o->receiptUrl.rfind("https://", 0) == 0);
            review_->setEnabled(o->state == "payee" && ws().can("demandes") && !bz);
            refund_->setEnabled(o->refundable() && ws().role() == Role::Plus && !bz);
            refund_->setTooltip(ws().role() != Role::Plus ? std::string("R\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+.")
                                                          : std::string("Stripe rembourse le client en entier ; la licence livr\xC3\xA9" "e n'est pas r\xC3\xA9voqu\xC3\xA9" "e d'office."));
        }
        invalidateLayout();
        invalidate();
    }

    void paintDetail(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        cardBox(r, ctx.theme, detail_);
        const shop::Order* o = selected();
        const float x = detail_.x + 18.f, w = detail_.w - 36.f;
        float y = detail_.y + 16.f;
        if (!o) {
            r.drawText({x, y}, "Le d\xC3\xA9tail", gfx::semibold(kF16), c.text);
            (void)drawWrapped(r, x, y + 30.f, w, "Choisis une commande dans le tableau : son client, son PC, sa licence, son paiement chez Stripe.", kF13, c.textMuted, 18.f, 4);
            return;
        }
        r.drawText({x, y}, "Commande n\xC2\xB0 " + std::to_string(o->number), gfx::semibold(kF16), c.text);
        const Color ink = stateInk(o->state);
        pill(r, x, y + 28.f, shop::stateLabel(o->state), kF12, ink, ink.withAlpha(26), 22.f, true);
        if (o->mode == "test") pill(r, x + pillWidth(r, shop::stateLabel(o->state), kF12, true) + 8.f, y + 28.f, "paiement d'essai", kF12, rgb(0xB45309), rgb(0xFFF4E5));
        y += 62.f;
        const auto row = [&](const std::string& k, const std::string& v) {
            if (y + 18.f > buttonsTop_ - 8.f) return;
            r.drawText({x, y}, k, kF12, c.textMuted);
            r.drawText({x + 104.f, y}, fit(r, v.empty() ? std::string("\xE2\x80\x94") : v, kF13, w - 104.f), kF13, c.text);
            y += 21.f;
        };
        row("Article", o->label.empty() ? o->article : o->label);
        row("Montant", shop::amountText(o->amount, o->currency) + " TTC");
        row("Soci\xC3\xA9t\xC3\xA9", o->company);
        row("Client", o->name);
        row("Courriel", o->email);
        {
            const std::string name = pcName(*o);
            const std::string id = o->device.substr(0, 8) + (o->device.size() > 8 ? "\xE2\x80\xA6" : "");
            row("PC", name.empty() ? id : name + " \xC2\xB7 " + id);
        }
        if (o->kind == "option") row("Sa licence", o->existingLicence);
        row("Command\xC3\xA9" "e", dateTimeFr(o->created));
        if (o->paid) row("Pay\xC3\xA9" "e", dateTimeFr(o->paid));
        row("Licence livr\xC3\xA9" "e", o->licence);
        row("Paiement", o->stripePayment);
        if (!o->stripeRefund.empty()) row("Remboursement", o->stripeRefund + (o->state == "remboursee" ? "" : " (demand\xC3\xA9)"));
        row("Re\xC3\xA7u", o->receiptUrl.empty() ? std::string() : std::string("la facture de Stripe (Ouvrir le re\xC3\xA7u)"));
        if (!o->error.empty() && y + 20.f < buttonsTop_ - 8.f) {
            y += 4.f;
            (void)drawWrapped(r, x, y, w, o->error, kF13, c.error, 18.f, std::max(1, static_cast<int>((buttonsTop_ - 8.f - y) / 18.f)));
        }
        if (o->state == "remboursee" && !o->licence.empty() && y + 40.f < buttonsTop_)
            (void)drawWrapped(r, x, y + 4.f, w, "Rembours\xC3\xA9" "e : la licence " + o->licence + " n'est pas r\xC3\xA9voqu\xC3\xA9" "e d'office (Licences \xE2\x80\xBA R\xC3\xA9voquer, si tu le d\xC3\xA9" "cides).", kF12,
                              c.warning, 17.f, 2);
    }

    // Le nom que le client a donne a son PC : dans la demande scellee de la commande (pour la cle de ce poste), ouverte
    // une fois ; illisible ici (scellee pour un autre admin, effacee apres la livraison) : vide.
    std::string pcName(const shop::Order& o) {
        if (const auto it = pcNames_.find(o.number); it != pcNames_.end()) return it->second;
        std::string name;
        if (!o.sealed.empty() && !o.forOther && ws().signer()) {
            online::Entry e;
            std::string why;
            if (online::openSealedRequest(*ws().signer(), o.device, o.keyId, o.sealed, ws().now(), e, &why) && e.deviceKey == o.deviceKey) name = e.deviceName;
        }
        pcNames_[o.number] = name;
        return name;
    }

    void fetch() {
        if (fetching_ || !shopServiceNote(ws()).empty()) return;
        fetching_ = true;
        lastFetch_ = steadySeconds();
        auto alive = alive_;
        if (!fetchShop(host_, alive, [this, alive](const OpResult& r) {
                if (!*alive) return;
                fetching_ = false;
                problem_ = r.ok ? std::string() : r.message;
                rebuild();
            }))
            fetching_ = false;
        update();
    }

    void deliver() {
        const shop::Order* o = selected();
        if (!o) return;
        if (busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return;
        }
        const int n = o->number;
        const Workspace* wp = &ws();
        auto alive = alive_;
        (void)host_.runSlow("Boutique : livraison de la commande n\xC2\xB0 " + std::to_string(n), [wp, n] { return wp->deliverOrders(n, false); },
                            [this, alive](const OpResult&) {
                                if (!*alive) return;
                                lastFetch_ = -1;
                                fetch();
                            });
    }

    void askReview() {
        const shop::Order* o = selected();
        if (!o || !host_.openDialog) return;
        const int n = o->number;
        auto alive = alive_;
        host_.openDialog(std::make_unique<AskDialog>(
            "boutique.revoir", "Mettre la commande n\xC2\xB0 " + std::to_string(n) + " \xC3\xA0 revoir",
            "Elle ne sera pas livr\xC3\xA9" "e seule : dis pourquoi (l'Admin et les autres admins le lisent). Tu pourras la d\xC3\xA9livrer plus tard, ou la rembourser.", true,
            "Mettre \xC3\xA0 revoir", false, [this, alive, n](const std::string& reason) {
                if (!*alive) return;
                const Workspace* wp = &ws();
                (void)host_.runSlow("Boutique : commande \xC3\xA0 revoir", [wp, n, reason] { return wp->reviewOrder(n, reason); },
                                    [this, alive](const OpResult&) {
                                        if (!*alive) return;
                                        lastFetch_ = -1;
                                        fetch();
                                    });
            }));
    }

    void askRefund() {
        const shop::Order* o = selected();
        if (!o || !host_.openDialog) return;
        const int n = o->number;
        std::string text = "Stripe rembourse " + shop::amountText(o->amount, o->currency) + " TTC \xC3\xA0 " + (o->company.empty() ? std::string("ce client") : o->company) +
                           " (la carte de son paiement). La commande passe \xC2\xAB rembours\xC3\xA9" "e \xC2\xBB quand Stripe le confirme.";
        if (!o->licence.empty()) text += " La licence " + o->licence + " reste valable : r\xC3\xA9voque-la toi-m\xC3\xAA" "me si tu le d\xC3\xA9" "cides (Licences).";
        auto alive = alive_;
        host_.openDialog(std::make_unique<AskDialog>("boutique.rembourser", "Rembourser la commande n\xC2\xB0 " + std::to_string(n) + " ?", text, false, "Rembourser", true,
                                                     [this, alive, n](const std::string&) {
                                                         if (!*alive) return;
                                                         const Workspace* wp = &ws();
                                                         (void)host_.runSlow("Boutique : remboursement", [wp, n] { return wp->refundOrder(n); },
                                                                             [this, alive](const OpResult&) {
                                                                                 if (!*alive) return;
                                                                                 lastFetch_ = -1;
                                                                                 fetch();
                                                                             });
                                                     }));
    }

    std::shared_ptr<bool>        alive_ = std::make_shared<bool>(true);
    ui::Button*                  reload_{nullptr};
    ui::Button*                  deploy_{nullptr};
    ui::DropDown*                filter_{nullptr};
    ui::TableView*               table_{nullptr};
    std::shared_ptr<OrdersModel> model_;
    ui::Button*                  deliver_{nullptr};
    ui::Button*                  stripe_{nullptr};
    ui::Button*                  receipt_{nullptr};
    ui::Button*                  review_{nullptr};
    ui::Button*                  refund_{nullptr};
    bool                         fetching_{false};
    double                       lastFetch_{-1};
    std::string                  problem_, note_;
    float                        x0_{0}, w_{0}, titleY_{0}, buttonsTop_{0}, fitW_{-1.f};
    std::map<int, std::string>   pcNames_;   // le nom du PC de chaque commande (sa demande scellee, ouverte ici)
    Rect                         noteRect_{}, detail_{};
    core::ConnectionScope        links_;
};

// =================================================================================== Brancher Stripe...
class StripeBody final : public ui::Widget {
public:
    StripeBody(Dialog& owner, ShellHost host, std::function<void()> done)
        : ui::Widget("dialog.stripe.corps"), owner_(owner), host_(std::move(host)), done_(std::move(done)) {
        // Deja branche : << Rebrancher >> - la nouvelle cle remplace l'ancienne (et le webhook est refait).
        const Workspace& w = host_.workspace();
        connected_ = w.hasShop() && w.shop().stripe.connected;
        connectedMode_ = connected_ ? w.shop().stripe.mode : std::string();
        key_ = &adopt(*this, std::make_unique<ui::InputText>("stripe.cle"));
        key_->setMasked(true);
        key_->setMaxLength(300);
        key_->setPlaceholder("sk_live_\xE2\x80\xA6 ou sk_test_\xE2\x80\xA6 (ou une cl\xC3\xA9 restreinte rk_\xE2\x80\xA6)");
        show_ = &button(*this, "Afficher", "stripe.afficher");
        cancel_ = &button(*this, "Fermer", "stripe.fermer");
        go_ = &button(*this, "Brancher", "stripe.brancher", ui::Button::Style::Primary, ui::Icon::Lock);
        node_ = &button(*this, "Ouvrir nodejs.org", "stripe.nodejs", ui::Button::Style::Default, ui::Icon::GoTo);
        login_ = &button(*this, "Se connecter \xC3\xA0 Cloudflare\xE2\x80\xA6", "stripe.cloudflare", ui::Button::Style::Default, ui::Icon::Lock);
        keys_ = &button(*this, "Ouvrir mes cl\xC3\xA9s Stripe", "stripe.cles", ui::Button::Style::Flat, ui::Icon::GoTo);
        links_ += key_->textChanged->connect([this](const std::string&) { invalidate(); });
        links_ += show_->clicked->connect([this] {
            key_->setMasked(!key_->masked());
            show_->setText(key_->masked() ? "Afficher" : "Masquer");
        });
        links_ += cancel_->clicked->connect([this] { close(); });
        links_ += go_->clicked->connect([this] { start(); });
        links_ += node_->clicked->connect([this] { openUrl(host_, "https://nodejs.org"); });
        links_ += keys_->clicked->connect([this] { openUrl(host_, "https://dashboard.stripe.com/apikeys"); });
        links_ += login_->clicked->connect([this] { loginCloudflare(); });
        sync();
    }
    ~StripeBody() override {
        links_.clear();
        *alive_ = false;
        wipeField();
    }

    [[nodiscard]] std::string titleText() const { return connected_ ? "Rebrancher Stripe" : "Brancher Stripe"; }
    [[nodiscard]] std::string subtitleText() const {
        if (!connected_) return "Le paiement en ligne de ta boutique";
        return "D\xC3\xA9j\xC3\xA0 branch\xC3\xA9 (" + std::string(connectedMode_ == "reel" ? "mode r\xC3\xA9" "el" : "mode test") + ") : la nouvelle cl\xC3\xA9 remplace l'ancienne";
    }
    [[nodiscard]] std::string introText() const {
        std::string t = "Colle ta cl\xC3\xA9 secr\xC3\xA8te Stripe (dashboard.stripe.com \xE2\x80\xBA D\xC3\xA9veloppeurs \xE2\x80\xBA Cl\xC3\xA9s API). L'Admin cr\xC3\xA9" "e le point d'arriv\xC3\xA9" "e du "
                        "webhook chez Stripe, puis pose la cl\xC3\xA9 et le secret du webhook sur ton service (des secrets de ton Worker Cloudflare). Rien n'est gard\xC3\xA9 sur ce PC.";
        if (connected_)
            t += " Stripe est d\xC3\xA9j\xC3\xA0 branch\xC3\xA9 : rebrancher remplace la cl\xC3\xA9 et le webhook d'avant (un autre compte, ou le passage du mode test au mode r\xC3\xA9" "el).";
        return t;
    }

protected:
    // La hauteur suit le contenu : le texte, le champ, la phrase du mode, le resultat (s'il y en a un), les boutons.
    void onLayout() override {
        constexpr float kW = 600.f;
        const float tw = kW - 48.f;
        introH_ = wrappedHeight(introText(), kF13, tw, 18.f, 7);
        resultH_ = 0.f;
        if (running_) resultH_ = 18.f;
        else if (!result_.empty()) resultH_ = wrappedHeight(result_, gfx::semibold(kF13), tw, 18.f, 3) + (next_.empty() ? 0.f : 2.f + wrappedHeight(next_, kF13, tw, 18.f, 3));
        const float wantH = 76.f + 14.f + introH_ + 14.f + 36.f + 6.f + 28.f + 4.f + 16.f + (resultH_ > 0.f ? 12.f + resultH_ : 0.f) + 18.f + 34.f + 20.f;
        frame_.place(*this, kW, wantH);
        const auto& p = frame_.panel;
        const float y = p.y + 76.f + 14.f + introH_ + 14.f;
        const float sw = buttonWidth(*show_, 90.f);
        key_->setBounds({p.x + 24.f, y, p.w - 48.f - sw - 8.f, 36.f});
        show_->setBounds({p.right() - 24.f - sw, y, sw, 36.f});
        keys_->setBounds({p.x + 20.f, y + 42.f, buttonWidth(*keys_, 160.f), 28.f});
        const float by = p.bottom() - 54.f;
        const float gw = buttonWidth(*go_, 130.f), cw = buttonWidth(*cancel_, 90.f);
        go_->setBounds({p.right() - 24.f - gw, by, gw, 34.f});
        cancel_->setBounds({go_->bounds().x - 8.f - cw, by, cw, 34.f});
        const float nw = buttonWidth(*node_, 150.f), lw = buttonWidth(*login_, 200.f);
        node_->setBounds({p.x + 24.f, by, nw, 34.f});
        login_->setBounds({p.x + 24.f, by, lw, 34.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto& p = frame_.panel;
        const float top = frame_.paint(r, ctx.theme, titleText(), subtitleText(), ui::Icon::Lock);
        const float x = p.x + 24.f, w = p.w - 48.f;
        (void)drawWrapped(r, x, top + 14.f, w, introText(), kF13, c.text, 18.f, 7);
        const std::string mode = shop::modeOfKey(key_->text());
        std::string hint = key_->text().empty() ? std::string("Commence par sk_test_ pour essayer (aucune carte r\xC3\xA9" "elle n'est d\xC3\xA9" "bit\xC3\xA9" "e), sk_live_ pour vendre.")
                           : mode == "reel" ? std::string("Cl\xC3\xA9 du mode r\xC3\xA9" "el : de vrais paiements.")
                           : mode == "test" ? std::string("Cl\xC3\xA9 du mode test : des paiements d'essai seulement.")
                           : key_->text().rfind("pk_", 0) == 0 ? std::string("C'est la cl\xC3\xA9 publiable : il faut la cl\xC3\xA9 SECR\xC3\x88TE.")
                                                               : std::string("Ce n'est pas une cl\xC3\xA9 secr\xC3\xA8te de Stripe (sk_\xE2\x80\xA6 ou rk_\xE2\x80\xA6).");
        r.drawText({x, key_->bounds().bottom() + 40.f}, fit(r, hint, kF12, w), kF12, mode.empty() && !key_->text().empty() ? c.error : c.textMuted);
        float ry = key_->bounds().bottom() + 40.f + 16.f + 12.f;
        if (running_) {
            r.drawText({x, ry}, "Branchement en cours\xE2\x80\xA6 (Stripe, puis ton service par le kit : jusqu'\xC3\xA0 une minute)", kF13, c.textMuted);
        } else if (!result_.empty()) {
            ry += drawWrapped(r, x, ry, w, result_, gfx::semibold(kF13), ok_ ? c.ok : c.error, 18.f, 3);
            if (!next_.empty()) (void)drawWrapped(r, x, ry + 2.f, w, next_, kF13, c.text, 18.f, 3);
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (frame_.hitClose(ev)) {
            close();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    void wipeField() {
        if (!key_) return;
        std::string t = key_->text();
        if (!t.empty()) deploy::crypto::wipe(t.data(), t.size());
        key_->setText({});
    }
    void close() {
        wipeField();
        owner_.close();
    }
    void sync() {
        go_->setEnabled(!running_ && !(host_.busy && host_.busy()));
        go_->setText(connected_ || ok_ ? std::string("Rebrancher") : std::string("Brancher"));
        show(node_, !running_ && outcome_ == deployer::Outcome::NodeMissing);
        show(login_, !running_ && outcome_ == deployer::Outcome::NotLoggedIn);
        invalidateLayout();
        invalidate();
    }
    void start() {
        if (running_) return;
        if (host_.busy && host_.busy()) {
            result_ = kBusy;
            ok_ = false;
            sync();
            return;
        }
        // La cle quitte le champ tout de suite (le champ est vide), passe au fil du travail et y est effacee.
        auto secret = std::make_shared<std::string>(key_->text());
        wipeField();
        const std::string mode = shop::modeOfKey(*secret);
        if (mode.empty()) {
            ok_ = false;
            result_ = secret->rfind("pk_", 0) == 0 ? std::string("C'est la cl\xC3\xA9 publiable (pk_\xE2\x80\xA6) : colle la cl\xC3\xA9 secr\xC3\xA8te.")
                                                   : std::string("Ce n'est pas une cl\xC3\xA9 secr\xC3\xA8te de Stripe : copie-la enti\xC3\xA8re (sk_live_, sk_test_, rk_\xE2\x80\xA6).");
            next_.clear();
            deploy::crypto::wipe(secret->data(), secret->size());
            secret->clear();
            sync();
            return;
        }
        running_ = true;
        result_.clear();
        next_.clear();
        outcome_ = deployer::Outcome::Ok;
        sync();
        const Workspace* wp = &host_.workspace();
        auto outcome = std::make_shared<deployer::Outcome>(deployer::Outcome::Failed);
        auto next = std::make_shared<std::string>();
        auto alive = alive_;
        const bool started = host_.runSlow && host_.runSlow("Boutique : brancher Stripe",
                                                             [wp, secret, outcome, next] {
                                                                 OpResult r = wp->connectStripe(*secret);
                                                                 deploy::crypto::wipe(secret->data(), secret->size());
                                                                 secret->clear();
                                                                 if (!r.ok && !r.details.empty()) *next = r.details.front();
                                                                 *outcome = r.ok ? deployer::Outcome::Ok
                                                                            : r.message.find("Node.js") != std::string::npos ? deployer::Outcome::NodeMissing
                                                                            : r.message.find("Cloudflare") != std::string::npos ? deployer::Outcome::NotLoggedIn
                                                                                                                               : deployer::Outcome::Failed;
                                                                 return r;
                                                             },
                                                             [this, alive, outcome, next](const OpResult& r) {
                                                                 if (!*alive) return;
                                                                 running_ = false;
                                                                 ok_ = r.ok;
                                                                 result_ = r.message;
                                                                 next_ = r.ok ? std::string("Ensuite : ouvre la vente en ligne et r\xC3\xA8gle les montants (Boutique), puis Enregistrer.") : *next;
                                                                 outcome_ = *outcome;
                                                                 sync();
                                                                 if (r.ok && done_) done_();
                                                             });
        if (!started) {
            deploy::crypto::wipe(secret->data(), secret->size());
            secret->clear();
            running_ = false;
            ok_ = false;
            result_ = kBusy;
            sync();
        }
    }
    void loginCloudflare() {
        if (running_ || (host_.busy && host_.busy())) return;
        running_ = true;
        sync();
        auto rep = std::make_shared<deployer::Report>();
        auto alive = alive_;
        (void)host_.runSlow("Service : connexion \xC3\xA0 Cloudflare",
                            [rep] {
                                *rep = deployer::login(deployer::Options{});
                                return rep->ok() ? OpResult::done(rep->reason) : OpResult::fail(rep->reason);
                            },
                            [this, alive, rep](const OpResult&) {
                                if (!*alive) return;
                                running_ = false;
                                ok_ = false;
                                result_ = rep->ok() ? std::string("Ce PC est connect\xC3\xA9 \xC3\xA0 Cloudflare : colle de nouveau ta cl\xC3\xA9, puis Brancher.") : rep->reason;
                                next_ = rep->ok() ? std::string() : rep->next;
                                outcome_ = rep->ok() ? deployer::Outcome::Ok : rep->outcome;
                                sync();
                            });
    }

    Dialog&               owner_;
    ShellHost             host_;
    std::function<void()> done_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    ui::InputText*        key_{nullptr};
    ui::Button*           show_{nullptr};
    ui::Button*           cancel_{nullptr};
    ui::Button*           go_{nullptr};
    ui::Button*           node_{nullptr};
    ui::Button*           login_{nullptr};
    ui::Button*           keys_{nullptr};
    bool                  running_{false}, ok_{false};
    bool                  connected_{false};
    std::string           connectedMode_;
    float                 introH_{0}, resultH_{0};
    std::string           result_, next_;
    deployer::Outcome     outcome_{deployer::Outcome::Ok};
    Frame                 frame_;
    core::ConnectionScope links_;
};

class StripeDialog final : public Dialog {
public:
    StripeDialog(ShellHost host, std::function<void()> done) : Dialog("dialog.stripe"), host_(std::move(host)), done_(std::move(done)) {}
    [[nodiscard]] std::string title() const override {
        const Workspace& w = host_.workspace();
        return w.hasShop() && w.shop().stripe.connected ? "Rebrancher Stripe" : "Brancher Stripe";
    }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<StripeBody>(*this, host_, done_));
        return core::ok();
    }

private:
    ShellHost             host_;
    std::function<void()> done_;
};

} // namespace

PagePtr makeShopPage(ShellHost host) { return std::make_unique<ShopPage>(std::move(host)); }
PagePtr makeOrdersPage(ShellHost host) { return std::make_unique<OrdersPage>(std::move(host)); }
menu::MenuPtr makeStripeDialog(ShellHost host, std::function<void()> done) { return std::make_unique<StripeDialog>(std::move(host), std::move(done)); }

} // namespace license_admin::gui
