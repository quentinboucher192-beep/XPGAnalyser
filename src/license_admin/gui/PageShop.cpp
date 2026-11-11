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
//  1.46.1 : LE MONTANT SUIT LE PRIX AFFICHE (le registre : HT + 20 % = TTC) et sa
//  reduction en cours (<< 12,00 EUR TTC (reduit jusqu'au 22/12, puis 60,00 EUR) >> :
//  le service passe seul au montant de base apres la date) ; un montant tape est
//  LIBRE (vide : il suit de nouveau). Le registre a change : le brouillon le suit,
//  Enregistrer l'envoie. Les produits chez Stripe (crees par le service) : un
//  refus de Stripe se dit dans un bandeau (la vente marche quand meme).
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
#include "ListKit.hpp"   // 1.47.0 : la barre de filtres, le clic droit, le depliage
#include "Page.hpp"

#include "../Changes.hpp"   // 1.48.1 : << quelque chose a change >> (toutes les pages se relisent)
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
// 1.47.0 : l'erreur du service en une ligne (<< Le service ne r\xC3\xA9pond pas. >>, << Service : Refus\xC3\xA9 : droit manquant. >>).
std::string serviceLine(const std::string& problem) {
    const std::string e = listkit::shortError(problem);
    return (e.rfind("Le service", 0) == 0 ? e : "Service : " + e) + ".";
}

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

// 1.46.1 : le prix affiche en centimes TTC - shop::shownPriceTtc (Shop.cpp : le montant suit le registre ; un prix
// reduit sans << HT >> ni << TTC >> herite du prix normal).

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
    // 1.47.0 : la reduction de l'offre (le registre : promo_price, promo_until) ; une option n'en a pas.
    std::string promo;
    Seconds     promoUntil{0};
    [[nodiscard]] std::string id() const { return shop::articleId(kind, kind == "option" ? option : edition, days); }
};

class CatalogueModel final : public ui::ITableModel {
public:
    std::vector<CatalogueRow> rows;
    shop::Settings*           draft{nullptr};
    std::function<void()>     changed;
    std::string               refusal;   // le dernier refus d'une case (montant illisible...)
    std::function<Seconds()>  clock;     // 1.47.0 : l'heure (celle du Workspace) ; vide : celle du systeme
    const licensing::Registry* registry{nullptr};   // 1.46.1 : ce que les montants suivent

    [[nodiscard]] Seconds now() const { return clock ? clock() : licensing::systemNow(); }
    // 1.46.1 : le prix du moment en centimes TTC (un prix reduit sans HT ni TTC herite du prix normal).
    [[nodiscard]] bool shownNowTtc(const CatalogueRow& row, std::int64_t& cents) const {
        if (promoNow(row)) return shop::shownPriceTtc(row.promo, cents, row.price);
        return !row.price.empty() && shop::shownPriceTtc(row.price, cents);
    }
    [[nodiscard]] bool anyReduction() const {
        if (!draft) return false;
        const Seconds t = now();
        return std::any_of(draft->articles.begin(), draft->articles.end(), [t](const shop::Article& a) { return !a.free && a.reducedAt(t); });
    }

    // 1.47.0 : le prix que le client voit MAINTENANT - comme la boutique des Studios (promoOf, LicenceWindow.cpp) et
    // l'historique des achats (Workspace.cpp) : le prix reduit tant que la fin de la reduction n'est pas passee, sinon
    // le prix normal.
    [[nodiscard]] bool promoNow(const CatalogueRow& row) const {
        return !row.promo.empty() && (clock ? clock() : licensing::systemNow()) < row.promoUntil;
    }
    [[nodiscard]] const std::string& currentPrice(const CatalogueRow& row) const { return promoNow(row) ? row.promo : row.price; }
    // La colonne << Prix affiche >> : le prix du moment ; reduit : << 390 EUR HT / an (au lieu de 490) >>.
    [[nodiscard]] std::string priceText(const CatalogueRow& row) const {
        if (!promoNow(row)) return row.price.empty() ? std::string("\xE2\x80\x94") : row.price;
        std::string base;   // le nombre du prix normal (<< 490 >>)
        for (char c : row.price) {
            if ((c >= '0' && c <= '9') || ((c == ',' || c == '.' || c == ' ') && !base.empty())) base += c;
            else if (!base.empty()) break;
        }
        while (!base.empty() && (base.back() == ' ' || base.back() == ',' || base.back() == '.')) base.pop_back();
        return row.promo + (base.empty() ? std::string(" (r\xC3\xA9" "duit)") : " (au lieu de " + base + ")");
    }

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
        // 1.47.0 : le prix du moment (une reduction en cours) ; 1.46.1 : face au montant du moment (un montant qui suit
        // le registre n'a pas d'ecart)
        if (!a || !shownNowTtc(rows[r], shown)) return {};
        if (std::llabs(shown - a->amountAt(now())) <= 1) return {};
        return "\xE2\x89\xA0 affich\xC3\xA9 (" + shop::amountText(shown) + " TTC)";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= rows.size()) return {};
        const shop::Article* a = article(r);
        switch (c) {
            case 0: return rows[r].name;
            case 1: return a ? "Oui" : "Non";
            case 2: return priceText(rows[r]);
            case 3: return a ? shop::articleAmountText(*a, now(), draft->currency) : std::string("\xE2\x80\x94");   // 1.46.1
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
        if (c == 3 && a && !a->free && a->reducedAt(now())) s.fg = Color::rgb(0x15803D);   // 1.46.1 : une reduction en cours
        if (!a && c != 0 && c != 1) s.fg = Color::rgb(0x94A3B8);
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
    [[nodiscard]] bool editable(RowIndex r, std::size_t c) const override { return draft && r < rows.size() && (c == 1 || c == 3 || c == 4); }
    [[nodiscard]] std::vector<std::string> cellChoices(RowIndex, std::size_t c) const override {
        if (c == 1) return {"Oui", "Non"};
        return {};
    }
    // Un article qui entre en vente : 1.46.1 - son montant SUIT le prix affiche et sa reduction (le montant du moment : le
    // prix reduit tant que la reduction court, comme 1.47.0) ; un prix qui ne se lit pas : 1,00 EUR, libre (a taper).
    [[nodiscard]] shop::Article fresh(RowIndex r) const {
        const CatalogueRow& row = rows[r];
        shop::Article a;
        a.kind = row.kind;
        a.edition = row.edition;
        a.option = row.option;
        a.days = row.days;
        a.id = row.id();
        a.amount = 100;
        a.free = true;
        if (registry) {
            shop::Settings one;
            a.free = false;
            one.articles.push_back(a);
            (void)shop::followRegistry(one, *registry, now());
            a = one.articles.front();
            if (a.free) a.amount = 100;
        }
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
            // 1.46.1 : vide (ou << auto >>) - le montant suit de nouveau le prix affiche ; un montant - il est libre.
            std::string low = t;
            while (!low.empty() && low.back() == ' ') low.pop_back();
            while (!low.empty() && low.front() == ' ') low.erase(0, 1);
            for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (low.empty() || low == "auto" || low == "suivre") {
                if (it == draft->articles.end()) {
                    draft->articles.push_back(fresh(r));
                    it = draft->articles.end() - 1;
                }
                shop::Settings one;
                one.articles.push_back(*it);
                one.articles.front().free = false;
                one.articles.front().legacy = false;
                if (!registry || ((void)shop::followRegistry(one, *registry, now()), one.articles.front().free)) {
                    refusal = "Le prix affich\xC3\xA9 de cet article ne se lit pas : tape un montant (\xC2\xAB 490 \xC2\xBB, \xC2\xAB 490,00 \xC2\xBB).";
                    if (changed) changed();
                    return false;
                }
                *it = one.articles.front();
                modelReset->emit();
                if (changed) changed();
                return true;
            }
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
            it->free = true;   // 1.46.1 : un montant tape ne suit plus le prix affiche
            it->legacy = false;
            it->reduced = 0;
            it->reducedUntil = 0;
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
    // 1.47.0 : l'erreur du service tient sur une ligne (listkit::shortError) ; sous la souris, son texte entier.
    [[nodiscard]] bool hasTooltip() const override { return !problem_.empty() || Page::hasTooltip(); }
    [[nodiscard]] std::string liveTooltip(gfx::Point m) const override {
        if (!problem_.empty() && m.y >= (titleY_ + 40.f) - 4.f && m.y <= (titleY_ + 40.f) + 22.f) return problem_;
        return Page::liveTooltip(m);
    }

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
        model_->clock = [this] { return ws().now(); };   // 1.47.0 : une reduction en cours (le prix du moment)
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
    [[nodiscard]] bool following() const noexcept { return following_; }   // 1.46.1 : le registre a change, a enregistrer

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
        // 1.46.1 : une ligne de plus dans la carte Stripe (les produits chez Stripe)
        stripeRect_ = {x0_, y, cardW, 170.f};
        saleRect_ = {x0_ + cardW + 16.f, y, cardW, 170.f};
        {   // 1.47.0 : a 1100 px, << Tableau de bord Stripe >> (le libelle entier en infobulle)
            const std::string full = "Ouvrir mon tableau de bord Stripe", brief = "Tableau de bord Stripe";
            const float pad = buttonWidth(*dashboard_, 0.f) - ui::measureWidth(dashboard_->text(), gfx::FontId{16});
            const std::string want = pad + ui::measureWidth(full, gfx::FontId{16}) <= cardW - buttonWidth(*connect_, 150.f) - 48.f ? full : brief;
            if (dashboard_->text() != want) {
                dashboard_->setText(want);
                dashboard_->setTooltip(want == full ? std::string() : full);
            }
        }
        const float cw = buttonWidth(*connect_, 150.f), dbw = std::min(cardW - cw - 48.f, buttonWidth(*dashboard_, 200.f));
        connect_->setBounds({stripeRect_.x + 18.f, stripeRect_.bottom() - 50.f, cw, 34.f});
        dashboard_->setBounds({stripeRect_.x + 18.f + cw + 10.f, stripeRect_.bottom() - 50.f, std::max(120.f, dbw), 34.f});
        active_->setBounds({saleRect_.x + 18.f, saleRect_.y + 46.f, 40.f, 22.f});
        auto_->setBounds({saleRect_.x + 18.f, saleRect_.y + 88.f, 40.f, 22.f});
        y = stripeRect_.bottom() + 18.f;
        // 1.46.1 : Stripe refuse les produits (une cle restreinte...) : un bandeau qui dit pourquoi (la vente marche quand meme)
        catalogNote_ = catalogProblem();
        if (!catalogNote_.empty()) {
            const float h = 20.f + wrappedHeight(catalogNote_, kF13, w_ - 60.f, 18.f, 3);
            catalogRect_ = {x0_, y - 4.f, w_, h};
            y = catalogRect_.bottom() + 14.f;
        } else {
            catalogRect_ = {};
        }
        catY_ = y;
        const float sw = buttonWidth(*save_, 130.f), aw = buttonWidth(*cancel_, 120.f);
        save_->setBounds({x0_ + w_ - sw, y - 2.f, sw, 34.f});
        cancel_->setBounds({save_->bounds().x - 8.f - aw, y - 2.f, aw, 34.f});
        show(cancel_, dirty_);
        y += 56.f;
        table_->setBounds({x0_, y, w_, std::max(120.f, b.bottom() - 18.f - y)});
        // 1.47.0 : une reduction en cours - le prix affiche dit le prix du moment et le prix normal : sa colonne s'elargit.
        // 1.46.1 : le montant aussi (<< 12,00 EUR TTC (reduit jusqu'au 22/12, puis 60,00 EUR) >>) ; les deux restent assez
        // etroits pour que l'ecart tienne a 1373 x 917.
        const bool promo = std::any_of(model_->rows.begin(), model_->rows.end(), [this](const CatalogueRow& r) { return model_->promoNow(r); });
        const bool red = model_->anyReduction();
        if (std::fabs(w_ - fitW_) > 0.5f || promo != fitPromo_ || red != fitRed_) {   // les colonnes a la largeur (aucune hors de la vue)
            fitW_ = w_;
            fitPromo_ = promo;
            fitRed_ = red;
            fitColumns(*table_,
                       {{"Article", 0.f, 3.f, 150.f, false},
                        {"En vente", 84.f, 0.f, 0.f, false},
                        {"Prix affich\xC3\xA9", promo ? 270.f : 130.f, 0.f, 0.f, false},
                        {"Montant encaiss\xC3\xA9 (TTC)", red ? 330.f : 210.f, 0.f, 0.f, false},
                        {"Libell\xC3\xA9 de la facture", 0.f, 3.f, 130.f, false},
                        {"\xC3\x89" "cart", 0.f, 2.f, 120.f, true}},
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
        else if (showProblem) lead = serviceLine(problem_);
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
        if (!catalogNote_.empty()) {   // 1.46.1
            r.fillRoundedRect(catalogRect_, c.warning.withAlpha(90), 10.f);
            r.fillRoundedRect(catalogRect_.inset(1.f, 1.f), c.panelBg, 9.f);
            r.fillRoundedRect(catalogRect_.inset(1.f, 1.f), c.warning.withAlpha(24), 9.f);
            ui::drawIcon(r, ui::Icon::Warning, {catalogRect_.x + 14.f, catalogRect_.y + 11.f, 18.f, 18.f}, c.warning);
            (void)drawWrapped(r, catalogRect_.x + 42.f, catalogRect_.y + 10.f, catalogRect_.w - 60.f, catalogNote_, kF13, c.text, 18.f, 3);
        }
        r.drawText({x0_, catY_}, "Catalogue", gfx::semibold(kF16), c.text);
        std::string hint = "Le montant suit le prix affich\xC3\xA9 et ses r\xC3\xA9" "ductions ; double-clic sur \xC2\xAB Montant \xC2\xBB : un montant libre (vide : il suit de nouveau).";
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
        } else if (following_) {   // 1.46.1
            hint = "Les montants suivent le registre (un prix, une r\xC3\xA9" "duction) : Enregistrer les envoie au service et \xC3\xA0 Stripe.";
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
        // 1.46.1 : les produits chez Stripe (crees par le service)
        if (known && s.connected) {
            const shop::CatalogState& k = view().catalog;
            std::string l3;
            Color c3 = c.textMuted;
            if (k.ok()) {
                l3 = "Produits chez Stripe : " + std::to_string(k.products) + " \xC3\xA0 jour (Catalogue de produits).";
                c3 = c.ok;
            } else if (k.state == "partiel") {
                l3 = "Produits chez Stripe : " + std::to_string(k.products) + " \xC3\xA0 jour, les autres au prochain Enregistrer.";
            } else if (k.state == "refus" || k.state == "panne") {
                l3 = "Produits pas visibles dans Stripe (voir plus bas) : la vente marche quand m\xC3\xAAme.";
                c3 = c.warning;
            } else {
                l3 = "Produits chez Stripe : cr\xC3\xA9\xC3\xA9s \xC3\xA0 l'enregistrement du catalogue.";
            }
            r.drawText({x, stripeRect_.y + 92.f}, fit(r, l3, kF13, stripeRect_.w - 36.f), kF13, c3);
        }
    }

    // 1.46.1 : Stripe a refuse les produits (le service le dit) : le bandeau sous les cartes ; vide : rien a dire.
    [[nodiscard]] std::string catalogProblem() const {
        if (!ws().hasShop() || !note_.empty() || !view().stripe.connected) return {};
        const shop::CatalogState& k = view().catalog;
        if (k.state != "refus" && k.state != "panne") return {};
        return "Tes produits ne se voient pas dans Stripe : " + k.message;
    }

    // 1.47.0 : la phrase entiere si elle tient, sinon sa forme courte (raccourcie s'il le faut).
    static std::string fitOr(const gfx::IRenderer& r, const std::string& full, const std::string& brief, gfx::FontId f, float w) {
        return r.measure(full, f).width <= w ? full : fit(r, brief, f, w);
    }
    void paintSale(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        cardBox(r, ctx.theme, saleRect_);
        const float x = saleRect_.x + 18.f;
        r.drawText({x, saleRect_.y + 16.f}, "Vente en ligne", gfx::semibold(kF16), c.text);
        const float tx = x + 52.f, tw = saleRect_.right() - 18.f - tx;
        r.drawText({tx, saleRect_.y + 44.f}, "Vendre en ligne", gfx::semibold(kF14), c.text);
        r.drawText({tx, saleRect_.y + 63.f}, fitOr(r, "Les Studios montrent \xC2\xAB Acheter \xE2\x80\x94 montant TTC \xC2\xBB sur ce qui est en vente.",
                                                              "Les Studios montrent \xC2\xAB Acheter \xE2\x80\x94 montant TTC \xC2\xBB.", kF12, tw), kF12, c.textMuted);
        r.drawText({tx, saleRect_.y + 86.f}, "Livrer seul les commandes pay\xC3\xA9" "es", gfx::semibold(kF14), c.text);
        r.drawText({tx, saleRect_.y + 105.f}, fitOr(r, "L'Admin ouvert signe et d\xC3\xA9pose la licence d\xC3\xA8s le paiement (sinon : Commandes et demandes \xE2\x80\xBA D\xC3\xA9livrer).",
                                                               "L'Admin ouvert livre d\xC3\xA8s le paiement (sinon : Commandes).", kF12, tw), kF12,
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
        model_->registry = ws().hasRegistry() ? &ws().registry() : nullptr;
        // 1.46.1 : les montants suivent le registre (un reglage d'avant : suivi s'il avait le prix du registre) ; un
        // changement : a enregistrer (<< Les montants suivent le registre >>).
        following_ = ws().hasShop() && ws().hasRegistry() && shopServiceNote(ws()).empty() && shop::followRegistry(draft_, ws().registry(), ws().now()) > 0;
        if (ws().hasRegistry()) {
            const auto& reg = ws().registry();
            for (const auto& e : reg.editions) {
                if (e.retired || e.type == licensing::LicenseType::Evaluation || e.id == reg.unlicensedEdition) continue;
                CatalogueRow row;
                row.kind = "edition";
                row.edition = e.id;
                row.name = (e.label.empty() ? e.id : e.label) + " - " + daysText(e.durationDays, false);
                row.price = e.price;
                row.promo = e.promoPrice;   // 1.47.0 : la reduction (le prix du moment : CatalogueModel::currentPrice)
                row.promoUntil = e.promoUntil;
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
        save_->setEnabled(plus && ready && !bz && (dirty_ || following_) && note_.empty());
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
                                following_ = false;
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
    bool                            following_{false};   // 1.46.1 : le brouillon suit un registre change
    double                          lastFetch_{-1};
    std::string                     problem_, note_, catalogNote_;
    float                           x0_{0}, w_{0}, titleY_{0}, catY_{0}, fitW_{-1.f};
    bool                            fitPromo_{false};   // 1.47.0 : les colonnes faites pour un prix reduit
    bool                            fitRed_{false};     // 1.46.1 : ... pour un montant reduit
    Rect                            noteRect_{}, stripeRect_{}, saleRect_{}, catalogRect_{};
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

// 1.48.1 : les trois genres de rangees de << Commandes et demandes >> (la puce << Type >>).
constexpr const char* kKindPaid = "Achat";
constexpr const char* kKindCart = "Demande";
constexpr const char* kKindLicence = "Demande de licence";   // l'assistant << Demander ma licence >> (POST /v1/demandes)

class OrdersModel final : public ui::ITableModel {
public:
    std::vector<shop::Order> rows;
    std::vector<std::string> by;          // 1.47.0 : qui l'a livree (le journal des admins) ; une colonne pour la barre, pas montree
    std::vector<int>         req;         // 1.48.1 : une demande de licence (son rang dans les demandes de la page) ; -1 : une commande
    std::vector<std::string> stateText;   // 1.48.1 : l'etat d'une demande de licence, en mots (vide : shop::stateLabel)
    [[nodiscard]] bool isRequest(RowIndex r) const { return r < req.size() && req[r] >= 0; }
    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 10; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* h[] = {"N\xC2\xB0", "Date", "Soci\xC3\xA9t\xC3\xA9", "Article", "Montant", "\xC3\x89tat", "Client", "Livr\xC3\xA9" "e par", "Type", "Cl\xC3\xA9"};
        return c < 10 ? h[c] : "";
    }
    [[nodiscard]] std::string kindText(RowIndex r) const {
        if (isRequest(r)) return kKindLicence;
        return r < rows.size() && !rows[r].payment ? kKindCart : kKindPaid;
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= rows.size()) return {};
        const auto& o = rows[r];
        const bool request = isRequest(r);
        switch (c) {
            case 0: return !request && o.lineCount > 1 ? std::to_string(o.number) + "-" + std::to_string(o.line) : std::to_string(o.number);   // 1.48.0 : une ligne
            case 1: return dateTimeFr(o.created);
            case 2: return o.company;
            case 3: return o.label.empty() ? o.article : o.label;
            case 4: return request ? std::string("\xE2\x80\x94") : shop::amountText(o.amount, o.currency);   // un paiement d'essai : la pastille du detail le dit
            case 5: return r < stateText.size() && !stateText[r].empty() ? stateText[r] : shop::stateLabel(o.state);
            case 6: return o.name.empty() ? o.email : o.name;
            case 7: return r < by.size() ? by[r] : std::string();
            case 8: return kindText(r);   // 1.48.1 : le type (la puce)
            case 9: return (request ? "d" : "c") + std::to_string(o.number) + "-" + std::to_string(o.line);   // la cle du depliage
            default: return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r < rows.size() && c == 5) {
            s.fg = stateInk(rows[r].state);
            s.bold = rows[r].state == "payee" || rows[r].state == "a_revoir" || rows[r].state == "a_valider";
            s.keepFgSelected = true;   // l'etat garde sa couleur sur la ligne choisie
        }
        if (r < rows.size() && c == 3 && isRequest(r)) s.bold = rows[r].state == "a_valider";   // 1.48.1 : une demande qui attend
        if (c == 0) s.monospace = true;
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t c) const override {
        if (c == 1 && rows[a].created != rows[b].created) return rows[a].created < rows[b].created;   // 1.48.1 : commandes et demandes melees
        if (c == 0 || c == 1) return rows[a].number != rows[b].number ? rows[a].number < rows[b].number : rows[a].line < rows[b].line;
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

// =================================================================================== la page Commandes et demandes
// 1.48.1 : UNE SEULE PAGE pour tout ce qui attend une decision - les commandes de la boutique (payees), les demandes du
// panier (sans paiement) et les DEMANDES DE LICENCE de l'assistant << Demander ma licence >> du Studio (POST
// /v1/demandes : le profil, les PC, la duree, l'offre ; avant : la page << Demandes en ligne >>). Une demande de licence
// est une rangee (type << Demande de licence >>, << a valider >>) ; Delivrer... lance EXACTEMENT l'acceptation d'avant
// (requestflow::accept : l'assistant de licence, sa fenetre de confirmation, la signature, le depot, le journal),
// Refuser... le refus d'avant (la raison) ; le droit << demandes >> comme avant. Rembourser... : 1.48.1 - UN article
// (la ligne choisie) ; Rembourser la commande... : toute la commande (comme avant).

// Une demande de licence vue comme une ligne : son etat (le code d'une ligne de commande : la couleur, la puce) et ses mots.
std::pair<std::string, std::string> requestState(const online::Entry& e) {
    using RS = online::RequestState;
    if (e.acceptedHere()) return {"a_revoir", "accept\xC3\xA9" "e ici \xE2\x80\x94 r\xC3\xA9ponse \xC3\xA0 renvoyer"};
    switch (e.state) {
        case RS::Pending: return {"a_valider", shop::stateLabel("a_valider")};
        case RS::Accepted: return {"livree", shop::stateLabel("livree")};
        case RS::Refused: return {"refusee", shop::stateLabel("refusee")};
        case RS::Cancelled: return {"annulee", shop::stateLabel("annulee")};
        default: return {"demande", online::requestStateLabel(e.state)};
    }
}
std::string oneLine(std::string s) {
    for (auto& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    return s;
}

class OrdersPage final : public Page {
public:
    // 1.47.0 : l'erreur du service tient sur une ligne (listkit::shortError) ; sous la souris, son texte entier.
    [[nodiscard]] bool hasTooltip() const override { return !problem_.empty() || Page::hasTooltip(); }
    [[nodiscard]] std::string liveTooltip(gfx::Point m) const override {
        if (!problem_.empty() && m.y >= (titleY_ + 40.f) - 4.f && m.y <= (titleY_ + 40.f) + 22.f) return problem_;
        return Page::liveTooltip(m);
    }

    explicit OrdersPage(ShellHost host) : Page("page.commandes", std::move(host)) {
        reload_ = &button(*this, "Relever", "commandes.relever", ui::Button::Style::Default, ui::Icon::Refresh);
        reload_->setTooltip("Relit les commandes de la boutique et les demandes de licence envoy\xC3\xA9" "es par les Studios des clients.");
        deploy_ = &button(*this, "D\xC3\xA9ployer le service", "commandes.deployer", ui::Button::Style::Primary, ui::Icon::Export);
        table_ = &adopt(*this, std::make_unique<ui::TableView>("commandes.table"));
        table_->setColumns({{"N\xC2\xB0", 64.f}, {"Date", 118.f}, {"Soci\xC3\xA9t\xC3\xA9", 150.f}, {"Article", 200.f}, {"Montant", 104.f}, {"\xC3\x89tat", 150.f}, {"Client", 120.f}});
        table_->setSelectionMode(ui::SelectionMode::Extended);   // 1.47.0 : plusieurs lignes (Exporter 12 lignes)
        model_ = std::make_shared<OrdersModel>();
        table_->setModel(model_);
        // 1.47.0 (planche 12) : LA BARRE - chercher, l'etat, l'article, la periode, livree par ; << Mes filtres >>. 1.48.1 : le type.
        bar_ = &adopt(*this, std::make_unique<ui::FilterBar>("commandes", barFields()));
        bar_->setNoun("commandes");
        bar_->setClock([this] { return listkit::seconds(ws().now()); });
        {
            ui::FilterBar::Source src;
            src.rows = [this] { return all_->rows.size(); };
            src.text = [this](std::size_t r, std::size_t f) { return all_->cellText(static_cast<RowIndex>(r), f); };
            src.time = [this](std::size_t r, std::size_t f) -> std::int64_t { return f == 1 && r < all_->rows.size() ? listkit::seconds(all_->rows[r].created) : 0; };
            src.extra = [this](std::size_t r) -> std::string {
                if (r >= all_->rows.size()) return {};
                if (all_->isRequest(static_cast<RowIndex>(r))) {
                    const auto& e = entries_[static_cast<std::size_t>(all_->req[r])];
                    return e.fingerprint + " " + e.email + " " + e.deviceName + " " + e.licenseId + " " + e.message;
                }
                return all_->rows[r].email + " " + all_->rows[r].licence + " " + all_->rows[r].stripePayment;
            };
            bar_->setSource(std::move(src));
        }
        bar_->linkTable(*table_);
        bar_->setMemoryKey("commandes");
        links_ += bar_->changed->connect([this] { rebuild(); });
        // 1.47.0 (planche 06) : le clic droit ; (planche 07) : une ligne se deplie (reperee par sa cle : une commande et une
        // demande de licence peuvent avoir le meme numero).
        links_ += ui::ListMenu::bind(*table_, [this] { return menuFor(); });
        table_->setUnfold([this](RowIndex r) { return unfoldFor(r); }, 9);
        deliver_ = &button(*this, "D\xC3\xA9livrer maintenant", "commandes.delivrer", ui::Button::Style::Primary, ui::Icon::Ok);
        acceptOnline_ = &button(*this, "D\xC3\xA9livrer en ligne\xE2\x80\xA6", "commandes.delivrer-en-ligne", ui::Button::Style::Default, ui::Icon::Globe);   // 1.48.1
        resend_ = &button(*this, "Renvoyer la r\xC3\xA9ponse", "commandes.renvoyer", ui::Button::Style::Primary, ui::Icon::Refresh);                     // 1.48.1
        stripe_ = &button(*this, "Voir dans Stripe", "commandes.stripe", ui::Button::Style::Default, ui::Icon::GoTo);
        receipt_ = &button(*this, "Ouvrir le re\xC3\xA7u", "commandes.recu", ui::Button::Style::Default, ui::Icon::Document);
        review_ = &button(*this, "Refuser\xE2\x80\xA6", "commandes.revoir");   // 1.48.0 : refuser une ligne (la raison)
        refund_ = &button(*this, "Rembourser\xE2\x80\xA6", "commandes.rembourser", ui::Button::Style::Danger);   // 1.48.1 : cet article
        refundAll_ = &button(*this, "Rembourser la commande\xE2\x80\xA6", "commandes.rembourser-commande", ui::Button::Style::Danger);   // 1.48.1
        delete_ = &button(*this, "Supprimer\xE2\x80\xA6", "commandes.supprimer", ui::Button::Style::Danger, ui::Icon::Close);   // 1.48.1 : sans argent
        links_ += reload_->clicked->connect([this] { fetchAll(); });
        links_ += deploy_->clicked->connect([this] {
            if (host_.openDialog) host_.openDialog(makeDeployDialog(host_, [this, alive = alive_] {
                if (*alive) update();
            }));
        });
        links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>&) { update(); });
        links_ += deliver_->clicked->connect([this] { deliver(); });
        links_ += acceptOnline_->clicked->connect([this] { acceptOnline(); });
        links_ += resend_->clicked->connect([this] { resend(); });
        links_ += stripe_->clicked->connect([this] {
            if (const shop::Order* o = selectedOrder()) openUrl(host_, shop::dashboardUrl(o->mode, o->stripePayment));
        });
        links_ += receipt_->clicked->connect([this] {
            if (const shop::Order* o = selectedOrder(); o && o->receiptUrl.rfind("https://", 0) == 0) openUrl(host_, o->receiptUrl);
        });
        links_ += review_->clicked->connect([this] { askReview(); });
        links_ += refund_->clicked->connect([this] { askRefundLine(); });
        links_ += refundAll_->clicked->connect([this] { askRefund(); });
        links_ += delete_->clicked->connect([this] { askDelete(); });
        refresh();
    }
    ~OrdersPage() override {
        links_.clear();
        *alive_ = false;
    }

    [[nodiscard]] std::string title() const override { return "Commandes et demandes"; }   // 1.48.0 : le panier

    void refresh() override {
        if (ws().service().ready() && ws().can("demandes") && shopServiceNote(ws()).empty() && !fetching_ && (lastFetch_ < 0 || steadySeconds() - lastFetch_ > 20.0))
            fetch();
        reloadAll();
        // 1.47.0 : << Ses commandes >> d'un client (Clients, le clic droit) : la barre filtree sur sa societe. 1.48.1 : les
        // liens de l'ancienne page << Demandes en ligne >> (la coquille, Shell::navigate) : les demandes de licence (d'un client).
        const std::string f = takePageFocus("commandes");
        if (f.rfind("client:", 0) == 0) {
            bar_->clearAll();
            bar_->filterOn(2, f.substr(7));
        } else if (f == "demandes" || f.rfind("demandes-client:", 0) == 0) {
            bar_->clearAll();
            if (f != "demandes") bar_->filterOn(2, f.substr(16));
            bar_->setValues(8, {kKindLicence});
        }
    }

    // Les essais : choisir la commande n (une rangee de la boutique) ; 1.48.1 : la demande de licence n.
    bool select(int number) { return selectWhere([&](std::size_t i) { return !model_->isRequest(static_cast<RowIndex>(i)) && model_->rows[i].number == number; }); }
    bool selectRequest(int number) { return selectWhere([&](std::size_t i) { return model_->isRequest(static_cast<RowIndex>(i)) && model_->rows[i].number == number; }); }
    [[nodiscard]] std::size_t shownCount() const { return model_->rows.size(); }
    [[nodiscard]] ui::FilterBar& bar() { return *bar_; }   // 1.47.0 : les essais

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float pad = b.w < 900.f ? 20.f : 28.f;
        x0_ = b.x + pad;
        w_ = std::max(600.f, b.w - 2.f * pad);
        titleY_ = b.y + 22.f;
        const float rw = buttonWidth(*reload_, 110.f);
        reload_->setBounds({x0_ + w_ - rw, titleY_ + 2.f, rw, 36.f});
        float y = titleY_ + 70.f;
        const float bh = bar_->heightFor(w_);   // 1.47.0 : la barre de filtres, sur toute la largeur
        bar_->setBounds({x0_, y, w_, bh});
        y += bh + 12.f;
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
                       {{"N\xC2\xB0", 64.f, 0.f, 0.f, false},   // 1.47.0 : + le chevron du depliage
                        {"Date", 120.f, 0.f, 0.f, false},
                        {"Soci\xC3\xA9t\xC3\xA9", 0.f, 3.f, 120.f, false},
                        {"Article", 0.f, 4.f, 170.f, false},
                        {"Montant", 104.f, 0.f, 0.f, false},
                        {"\xC3\x89tat", 150.f, 0.f, 0.f, false},
                        {"Client", 0.f, 2.f, 100.f, true}},
                       tw);
        }
        detail_ = {x0_ + w_ - detailW, y, detailW, h};
        // Les boutons du detail, en bas du volet (de haut en bas : Delivrer, en ligne, Renvoyer, Stripe, le recu, Refuser,
        // Rembourser cet article, Rembourser la commande).
        float by = detail_.bottom() - 16.f - 34.f;
        const float bx = detail_.x + 18.f, bw = detail_.w - 36.f;
        for (ui::Button* btn : {delete_, refundAll_, refund_, review_, receipt_, stripe_, resend_, acceptOnline_, deliver_}) {
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
        r.drawText({x0_, titleY_}, "Commandes et demandes", gfx::semibold(kF28), c.text);
        r.drawText({x0_, titleY_ + 40.f}, fit(r, status(), kF14, reload_->bounds().x - 16.f - x0_), kF14, problem_.empty() || !note_.empty() ? c.textMuted : c.warning);
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
    [[nodiscard]] int selectedRow() const {
        const auto sel = table_->selectedModelRows();
        return sel.empty() || sel.front() >= model_->rows.size() ? -1 : static_cast<int>(sel.front());
    }
    // La rangee choisie (une commande ou une demande de licence) ; selectedOrder : une commande seulement ; selectedRequest :
    // une demande de licence seulement.
    [[nodiscard]] const shop::Order* selected() const {
        const int r = selectedRow();
        return r < 0 ? nullptr : &model_->rows[static_cast<std::size_t>(r)];
    }
    [[nodiscard]] const shop::Order* selectedOrder() const {
        const int r = selectedRow();
        return r < 0 || model_->isRequest(static_cast<RowIndex>(r)) ? nullptr : &model_->rows[static_cast<std::size_t>(r)];
    }
    [[nodiscard]] const online::Entry* selectedRequest() const {
        const int r = selectedRow();
        if (r < 0 || !model_->isRequest(static_cast<RowIndex>(r))) return nullptr;
        const auto i = static_cast<std::size_t>(model_->req[static_cast<std::size_t>(r)]);
        return i < entries_.size() ? &entries_[i] : nullptr;
    }
    template <class F>
    bool selectWhere(F&& f) {
        for (std::size_t i = 0; i < model_->rows.size(); ++i)
            if (f(i)) {
                table_->selectModelRows({static_cast<RowIndex>(i)});
                update();
                return true;
            }
        return false;
    }

    [[nodiscard]] std::string status() const {
        if (fetching_) return "Relev\xC3\xA9 des commandes et des demandes en cours\xE2\x80\xA6";
        if (!problem_.empty() && note_.empty()) return serviceLine(problem_);
        const auto& w = ws();
        if (!w.service().ready()) return "Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne).";
        if (!w.can("demandes")) return w.whyNot("demandes");
        const int pending = w.service().pending;
        const std::string requests = pending > 0 ? " \xC2\xB7 " + std::to_string(pending) + " demande(s) de licence \xC3\xA0 valider" : std::string();
        if (!w.hasOrders()) return "Les commandes de la boutique (pay\xC3\xA9" "es sur la page de Stripe) : pas encore relev\xC3\xA9" "es" + requests + ".";
        std::string t = std::to_string(w.orders().orders.size()) + " commande(s) \xC2\xB7 " + std::to_string(w.orders().toDeliver) + " \xC3\xA0 livrer" + requests;
        if (w.hasShop()) t += std::string(" \xC2\xB7 livraison automatique ") + (w.shop().autoDeliver ? "en marche" : "arr\xC3\xAAt\xC3\xA9" "e (D\xC3\xA9livrer maintenant)");
        if (w.hasShop() && w.shop().stripe.connected) t += " \xC2\xB7 " + shop::modeLabel(w.shop().stripe.mode);
        return t;
    }
    [[nodiscard]] std::string empty() const {
        const auto& w = ws();
        if (!w.service().ready()) return "Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 : la boutique et les demandes de licence en ont besoin (Service en ligne).";
        if (!w.can("demandes")) return w.whyNot("demandes");
        if (!w.hasOrders() && entries_.empty()) return "Pas encore relev\xC3\xA9" "es : clique \xC2\xAB Relever \xC2\xBB.";
        if (!all_->rows.empty()) return "Aucune commande ni demande avec ces filtres : \xC2\xAB Effacer les filtres \xC2\xBB pour les voir toutes.";
        return "Aucune commande ni demande pour l'instant. Les clients ach\xC3\xA8tent ou demandent leur licence depuis leur Studio (Licence \xE2\x80\xBA Demander ma licence) ; "
               "la boutique s'ouvre dans Boutique.";
    }

    // 1.47.0 : toutes les commandes (la barre les filtre), et qui a livre chacune (le journal des admins). 1.48.1 : et les
    // demandes de licence (relevees par la coquille, rangees sur ce poste), melees aux commandes, les plus recentes d'abord.
    void reloadAll() {
        who_.reset();
        entries_ = ws().service().ready() ? ws().service().requests : std::vector<online::Entry>{};
        struct Row {
            shop::Order o;
            std::string by, stateText;
            int         req{-1};
        };
        std::vector<Row> rows;
        const auto deliveredBy = [&](int number) {
            const auto d = who_.last({"commande_livrer"}, "commande " + std::to_string(number));
            return d ? d->who : std::string();
        };
        if (ws().hasOrders())
            for (const auto& o : ws().orders().orders) {
                if (o.lines.empty()) {   // une commande sans lignes (faite a la main, les essais) : elle-meme
                    rows.push_back({o, o.state == "livree" || o.state == "remboursee" ? deliveredBy(o.number) : std::string(), {}, -1});
                    continue;
                }
                for (const auto& l : o.lines) {   // 1.48.0 : une rangee par ligne (chacune se valide a part)
                    shop::Order x = o;
                    x.line = l.line;
                    x.orderState = o.state;
                    x.state = l.state;
                    x.article = l.article;
                    x.kind = l.kind;
                    x.edition = l.edition;
                    x.option = l.option;
                    x.days = l.days;
                    x.amount = l.amount;
                    x.label = l.label;
                    x.existingLicence = l.existingLicence;
                    x.licence = l.licence;
                    x.error = l.error;
                    const std::string by = x.state == "livree" || x.state == "remboursee" ? deliveredBy(o.number) : std::string();
                    rows.push_back({std::move(x), by, {}, -1});
                }
            }
        for (std::size_t i = 0; i < entries_.size(); ++i) {   // 1.48.1 : une demande de licence = une rangee (une seule ligne)
            const auto& e = entries_[i];
            shop::Order x;
            x.number = e.number;
            x.device = e.device;
            x.deviceKey = e.deviceKey;
            x.created = e.createdAt ? e.createdAt : e.requestedAt;
            x.company = e.customer;
            x.name = e.contact == e.email ? std::string() : e.contact;
            x.email = e.email;
            x.kind = e.optionRequest() ? "option" : "edition";
            x.edition = e.edition;
            x.option = e.option;
            x.days = e.optionDays;
            x.existingLicence = e.licenseId;
            const std::string wanted = requestflow::wanted(ws(), e);
            x.label = std::string(kKindLicence) + (wanted.empty() || wanted == "\xE2\x80\x94" ? std::string() : " : " + wanted);
            x.payment = false;
            x.currency = "eur";
            const auto st = requestState(e);
            x.state = st.first;
            if (e.state == online::RequestState::Accepted) x.licence = e.answer;
            else if (e.state == online::RequestState::Refused) x.error = e.answer;
            std::string by;
            if (e.state == online::RequestState::Accepted || e.state == online::RequestState::Refused || e.state == online::RequestState::Handled)
                if (const auto d = who_.last({"demande_accepter", "demande_refuser"}, "demande " + std::to_string(e.number))) by = d->who;
            rows.push_back({std::move(x), by, st.second, static_cast<int>(i)});
        }
        // Les deux listes gardent leur ordre (les plus recentes d'abord) ; une demande de licence se place avant la premiere
        // commande plus ancienne qu'elle.
        const auto firstRequest = std::find_if(rows.begin(), rows.end(), [](const Row& x) { return x.req >= 0; });
        std::vector<Row> merged;
        merged.reserve(rows.size());
        auto oi = rows.begin(), ri = firstRequest;
        while (oi != firstRequest || ri != rows.end()) {
            if (ri != rows.end() && (oi == firstRequest || ri->o.created > oi->o.created)) merged.push_back(std::move(*ri++));
            else merged.push_back(std::move(*oi++));
        }
        all_->rows.clear();
        all_->by.clear();
        all_->req.clear();
        all_->stateText.clear();
        for (auto& r : merged) {
            all_->rows.push_back(std::move(r.o));
            all_->by.push_back(std::move(r.by));
            all_->req.push_back(r.req);
            all_->stateText.push_back(std::move(r.stateText));
        }
        bar_->refreshCounts();
        rebuild();
    }

    void rebuild() {
        const int sel = selectedRow();
        const std::string keep = sel >= 0 ? model_->cellText(static_cast<RowIndex>(sel), 9) : std::string();
        model_->rows.clear();
        model_->by.clear();
        model_->req.clear();
        model_->stateText.clear();
        for (std::size_t i = 0; i < all_->rows.size(); ++i) {
            if (!bar_->accepts(i)) continue;
            model_->rows.push_back(all_->rows[i]);
            model_->by.push_back(all_->by[i]);
            model_->req.push_back(all_->req[i]);
            model_->stateText.push_back(all_->stateText[i]);
        }
        model_->reset();
        if (!keep.empty()) (void)selectWhere([&](std::size_t i) { return model_->cellText(static_cast<RowIndex>(i), 9) == keep; });
        update();
    }

    // ---- 1.47.0 (planche 12) : les colonnes vues par la barre (dans l'ordre du modele) ----
    static std::vector<ui::FilterBar::Field> barFields() {
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> f(9);
        f[0] = {.title = "N\xC2\xB0", .kind = K::Text};
        f[1] = {.title = "Date", .kind = K::Period, .chip = true, .chipLabel = "P\xC3\xA9riode"};
        f[2] = {.title = "Soci\xC3\xA9t\xC3\xA9", .noun = "une soci\xC3\xA9t\xC3\xA9"};
        f[3] = {.title = "Article", .chip = true, .noun = "un article"};
        f[4] = {.title = "Montant", .kind = K::Text};
        f[5] = {.title = "\xC3\x89tat", .chip = true, .noun = "un \xC3\xA9tat"};
        for (const char* st : {"payee", "a_valider", "a_revoir", "a_payer", "livree", "refusee", "remboursee", "expiree", "annulee", "echec_paiement"})
            f[5].order.push_back(shop::stateLabel(st));
        f[6] = {.title = "Client", .noun = "un client"};
        f[7] = {.title = "Livr\xC3\xA9" "e par", .chip = true, .noun = "un admin"};
        f[8] = {.title = "Type", .chip = true, .noun = "un type"};   // 1.48.1 : achat, demande (le panier), demande de licence
        f[8].order = {kKindPaid, kKindCart, kKindLicence};
        return f;
    }

    // ---- 1.47.0 (planche 06) : le clic droit, dans l'ordre de la regle ----
    ui::ListMenu menuFor() {
        const auto rows = table_->selectedModelRows();
        ui::ListMenu m({rows.size(), "commandes", true});
        if (rows.empty()) return m;
        m.unfold(*table_);
        const bool one = rows.size() == 1 && rows.front() < model_->rows.size();
        const shop::Order* o = one ? &model_->rows[rows.front()] : nullptr;
        const auto whyOf = [](ui::Button* b, const std::string& other) { return b->enabled() ? std::string() : (b->tooltip().empty() ? other : b->tooltip()); };
        const online::Entry* e = one ? selectedRequest() : nullptr;
        if (o && e) {   // 1.48.1 : une demande de licence (les gestes de l'ancienne page, dans le meme ordre)
            const std::string no = "elle n'attend pas de d\xC3\xA9" "cision";
            m.action({.label = "D\xC3\xA9livrer\xE2\x80\xA6", .icon = ui::Icon::Ok, .why = whyOf(deliver_, no), .run = [this] { deliver(); }});
            if (!e->optionRequest())
                m.action({.label = "D\xC3\xA9livrer en ligne\xE2\x80\xA6", .icon = ui::Icon::Globe, .why = whyOf(acceptOnline_, no), .run = [this] { acceptOnline(); }});
            m.action({.label = "Refuser\xE2\x80\xA6", .icon = ui::Icon::Close, .why = whyOf(review_, no), .run = [this] { askReview(); }});
            m.action({.label = "Renvoyer la r\xC3\xA9ponse", .icon = ui::Icon::Refresh,
                      .why = e->acceptedHere() ? whyOf(resend_, std::string()) : std::string("pas de r\xC3\xA9ponse \xC3\xA0 renvoyer"), .run = [this] { resend(); }});
            const std::string lic = e->state == online::RequestState::Accepted && !e->answer.empty() ? e->answer : e->licenseId;
            if (!lic.empty() && ws().licence(lic))
                m.goTo({.label = "La licence " + lic, .icon = ui::Icon::Document, .run = [this, lic] {
                            requestPageFocus("licences", lic);
                            host_.navigate("licences");
                        }});
        } else if (o) {
            m.action({.label = "D\xC3\xA9livrer maintenant", .icon = ui::Icon::Ok, .why = whyOf(deliver_, "pas pour cette commande"), .run = [this] { deliver(); }});
            m.action({.label = "Ouvrir le re\xC3\xA7u", .icon = ui::Icon::Document, .why = receipt_->enabled() ? std::string() : std::string("pas de re\xC3\xA7u pour cette commande"),
                      .run = [this] { receipt_->clicked->emit(); }});
            m.action({.label = "Voir dans Stripe", .icon = ui::Icon::GoTo, .why = stripe_->enabled() ? std::string() : std::string("pas encore pay\xC3\xA9" "e"),
                      .run = [this] { stripe_->clicked->emit(); }});
            m.action({.label = "Refuser\xE2\x80\xA6", .icon = ui::Icon::Warning,
                      .why = review_->enabled() ? std::string() : std::string("seulement un article \xC3\xA0 livrer ou \xC3\xA0 valider"), .run = [this] { askReview(); }});
            if (!o->licence.empty()) {
                const std::string id = o->licence;
                m.goTo({.label = "Sa licence " + id, .icon = ui::Icon::Document, .run = [this, id] {
                            requestPageFocus("licences", id);
                            host_.navigate("licences");
                        }});
            }
        }
        if (o) {
            // Aller a : le client, l'admin qui l'a livree (ou qui a repondu).
            if (!o->company.empty()) {
                const std::string c = o->company;
                m.goTo({.label = "Le client : " + c, .icon = ui::Icon::User, .run = [this, c] {
                            requestPageFocus("clients", "client:" + c);
                            host_.navigate("clients");
                        }});
            }
            const auto d = e ? who_.last({"demande_accepter", "demande_refuser"}, "demande " + std::to_string(o->number))
                             : who_.last({"commande_livrer"}, "commande " + std::to_string(o->number));
            if (d) {
                const std::string key = d->key;
                m.goTo({.label = "L'admin : " + d->who, .icon = ui::Icon::User, .run = [this, key] {
                            requestPageFocus("admins", "admin:" + key);
                            host_.navigate("admins");
                        }});
            }
            // Filtrer sur : les memes puces que la barre.
            const RowIndex row = rows.front();
            const auto filterOn = [&](std::size_t f, const std::string& label, const std::string& v) {
                if (v.empty()) return;
                m.filterOn({.label = label + " : " + v, .run = [this, f, v] { bar_->filterOn(f, v); }});
            };
            filterOn(5, "Cet \xC3\xA9tat", model_->cellText(row, 5));
            filterOn(8, "Ce type", model_->kindText(row));
            if (!e) filterOn(3, "Cet article", o->label.empty() ? o->article : o->label);
            filterOn(2, "Cette soci\xC3\xA9t\xC3\xA9", o->company);
            if (o->created > 0) {
                const std::int64_t when = listkit::seconds(o->created);
                m.filterOn({.label = "Ce jour : " + dateFr(o->created), .run = [this, when] { bar_->filterOnDay(1, when); }});
            }
            filterOn(7, e ? "R\xC3\xA9pondue par" : "Livr\xC3\xA9" "e par", row < model_->by.size() ? model_->by[row] : std::string());
            m.copy("Le num\xC3\xA9ro", std::to_string(o->number), ui::Icon::Copy);
            if (e && !e->fingerprint.empty()) m.copy("L'empreinte du PC", e->fingerprint, ui::Icon::Lock);
            if (!o->email.empty()) m.copy("Le courriel", o->email, ui::Icon::Mail);
            if (e && !e->message.empty()) m.copy("Le message", e->message, ui::Icon::Comment);
            if (!e && !o->licence.empty()) m.copy("La licence", o->licence, ui::Icon::Document);
            if (!o->stripePayment.empty()) m.copy("Le paiement (Stripe)", o->stripePayment, ui::Icon::Link);
        }
        std::string lines;
        for (const RowIndex r : rows) {
            std::string line;
            for (std::size_t c = 0; c < 7; ++c) line += (c ? "\t" : "") + model_->cellText(r, c);
            lines += line + "\n";
        }
        m.copy("La ligne", lines, ui::Icon::Copy, "Copier les %n lignes", lines);
        m.exportRows([this, rows] { listkit::exportRows(host_, *table_, rows, "commandes.csv", "commandes"); });
        if (o && !e) {   // 1.48.1 : rembourser CET article ; toute la commande a part
            m.danger({.label = "Rembourser", .icon = ui::Icon::Undo, .why = refund_->enabled() ? std::string() : refund_->tooltip(), .run = [this] { askRefundLine(); }});
            if (o->line > 0)
                m.danger({.label = "Rembourser la commande", .icon = ui::Icon::Undo, .why = refundAll_->enabled() ? std::string() : refundAll_->tooltip(),
                          .run = [this] { askRefund(); }});
        }
        // 1.48.1 : Supprimer (une ou plusieurs lignes) - seulement ce qui n'a pas d'argent ; sinon grise, la raison.
        m.danger({.label = "Supprimer", .many = "Supprimer les %n lignes", .icon = ui::Icon::Close,
                  .why = delete_->enabled() ? std::string() : delete_->tooltip(), .run = [this] { askDelete(); }});
        return m;
    }

    // ---- 1.47.0 (planche 07) : la ligne depliee - le client et le paiement, la livraison, qui l'a livree ----
    ui::WidgetPtr unfoldFor(RowIndex r) {
        if (r >= model_->rows.size()) return nullptr;
        if (model_->isRequest(r)) {
            const auto i = static_cast<std::size_t>(model_->req[r]);
            return i < entries_.size() ? unfoldRequest(entries_[i]) : nullptr;
        }
        const shop::Order o = model_->rows[r];
        auto card = std::make_unique<ui::UnfoldCard>("commandes.depliage");
        ui::UnfoldCard::Box client;
        client.title = "LE CLIENT, LE PAIEMENT";
        if (!o.company.empty()) {
            const std::string c = o.company;
            client.lines.push_back({.icon = ui::Icon::User, .text = "**" + o.company + "**", .link = [this, c] {
                                        requestPageFocus("clients", "client:" + c);
                                        host_.navigate("clients");
                                    }});
        }
        if (!o.name.empty() || !o.email.empty()) client.lines.push_back({.icon = ui::Icon::Mail, .text = o.email.empty() ? o.name : o.email});
        if (!o.stripePayment.empty() || !o.stripeSession.empty()) {
            const std::string url = shop::dashboardUrl(o.mode, o.stripePayment);
            std::string pay = "Stripe";
            if (!o.stripePayment.empty()) pay += " \xC2\xB7 " + o.stripePayment.substr(0, std::min<std::size_t>(o.stripePayment.size(), 6)) + "\xE2\x80\xA6";
            if (o.mode == "test") pay += " \xC2\xB7 paiement d'essai";
            client.lines.push_back({.icon = ui::Icon::Link, .text = pay, .link = [this, url] { openUrl(host_, url); }});
        }
        if (o.receiptUrl.rfind("https://", 0) == 0) {
            const std::string url = o.receiptUrl;
            client.lines.push_back({.icon = ui::Icon::Document, .text = "le re\xC3\xA7u", .link = [this, url] { openUrl(host_, url); }});
        }
        client.lines.push_back({.icon = ui::Icon::Export, .text = "**" + shop::amountText(o.amount, o.currency) + "** TTC \xC2\xB7 " + (o.label.empty() ? o.article : o.label)});
        card->addBox(std::move(client));
        ui::UnfoldCard::Box life;
        life.title = "LA LIVRAISON";
        life.timeline = true;
        const Seconds now = ws().now();
        if (o.created) life.lines.push_back({.time = whenFr(now, o.created), .text = "cr\xC3\xA9\xC3\xA9" "e"});
        if (o.paid) life.lines.push_back({.time = whenFr(now, o.paid), .text = "pay\xC3\xA9" "e"});
        if (o.takenAt) life.lines.push_back({.time = whenFr(now, o.takenAt), .text = "prise pour la livraison"});
        const auto d = who_.last({"commande_livrer"}, "commande " + std::to_string(o.number));
        if (!o.licence.empty()) {
            const std::string id = o.licence;
            life.lines.push_back({.time = d ? whenFr(now, d->when) : std::string(), .text = "livr\xC3\xA9" "e : **" + o.licence + "**", .link = [this, id] {
                                      requestPageFocus("licences", id);
                                      host_.navigate("licences");
                                  }});
        }
        if (o.licence.empty() || o.state == "remboursee")   // 1.48.1 : un article rembourse le dit aussi
            life.lines.push_back({.text = shop::stateLabel(o.state), .tone = o.state == "payee" || o.state == "a_revoir" ? ui::Tone::Warning : ui::Tone::None});
        if (!o.error.empty()) life.lines.push_back({.icon = ui::Icon::Warning, .text = o.error, .tone = ui::Tone::Error});
        card->addBox(std::move(life));
        if (d) {
            card->addText("livr\xC3\xA9" "e par");
            (void)card->addPerson(listkit::person(host_, d->key, d->who), "commandes.depliage.admin");
        }
        if (o.receiptUrl.rfind("https://", 0) == 0) {
            const std::string url = o.receiptUrl;
            (void)card->addButton("Le re\xC3\xA7u", "commandes.depliage.recu", ui::Icon::Document, [this, url] { openUrl(host_, url); }, {}, true);
        }
        if (!o.stripePayment.empty() || !o.stripeSession.empty()) {
            const std::string url = shop::dashboardUrl(o.mode, o.stripePayment);
            (void)card->addButton("Stripe", "commandes.depliage.stripe", ui::Icon::GoTo, [this, url] { openUrl(host_, url); }, {}, true);
        }
        return card;
    }

    // 1.48.1 : une demande de licence depliee - la demande (le PC, son empreinte, le message), la reponse, qui l'a donnee.
    ui::WidgetPtr unfoldRequest(const online::Entry& e) {
        auto card = std::make_unique<ui::UnfoldCard>("commandes.depliage");
        const Seconds now = ws().now();
        ui::UnfoldCard::Box ask;
        ask.title = "LA DEMANDE DE LICENCE";
        ask.lines.push_back({.icon = ui::Icon::Star, .text = "**" + requestflow::wanted(ws(), e) + "**"});
        if (!e.fingerprint.empty()) ask.lines.push_back({.icon = ui::Icon::Lock, .text = e.fingerprint, .mono = true});
        std::string pc = e.deviceName.empty() ? std::string("(sans nom)") : e.deviceName;
        if (!e.software.empty()) pc += " \xC2\xB7 " + e.software;
        ask.lines.push_back({.icon = ui::Icon::Station, .text = pc});
        if (!e.message.empty()) ask.lines.push_back({.icon = ui::Icon::Comment, .text = "\xC2\xAB " + oneLine(e.message) + " \xC2\xBB"});
        ask.lines.push_back({.icon = ui::Icon::History, .text = "re\xC3\xA7ue " + whenFr(now, e.createdAt ? e.createdAt : e.requestedAt)});
        card->addBox(std::move(ask));
        ui::UnfoldCard::Box ans;
        ans.title = "LA R\xC3\x89PONSE";
        if (e.state == online::RequestState::Accepted) {
            ans.lines.push_back({.text = "**livr\xC3\xA9" "e** " + whenFr(now, e.answeredAt), .tone = ui::Tone::Ok, .check = true});
        } else if (e.state == online::RequestState::Refused) {
            ans.lines.push_back({.icon = ui::Icon::Close, .text = "**refus\xC3\xA9" "e** " + whenFr(now, e.answeredAt), .tone = ui::Tone::Error});
            if (!e.answer.empty()) ans.lines.push_back({.icon = ui::Icon::Comment, .text = "\xC2\xAB " + oneLine(e.answer) + " \xC2\xBB"});
        } else {
            ans.lines.push_back({.icon = ui::Icon::Info, .text = "**" + requestState(e).second + "**", .tone = e.waiting() ? ui::Tone::Warning : ui::Tone::Muted});
        }
        const std::string lic = e.state == online::RequestState::Accepted && !e.answer.empty() ? e.answer : e.licenseId;
        if (!lic.empty()) {
            ui::UnfoldCard::Line l{.icon = ui::Icon::Document, .text = "licence **" + lic + "**"};
            if (ws().licence(lic))
                l.link = [this, lic] {
                    requestPageFocus("licences", lic);
                    host_.navigate("licences");
                };
            ans.lines.push_back(std::move(l));
        }
        card->addBox(std::move(ans));
        if (const auto d = who_.last({"demande_accepter", "demande_refuser"}, "demande " + std::to_string(e.number))) {
            card->addText("r\xC3\xA9pondue par");
            (void)card->addPerson(listkit::person(host_, d->key, d->who), "commandes.depliage.admin");
        }
        const std::string noRight = ws().can("demandes") ? std::string() : ws().whyNot("demandes");
        if (e.acceptedHere())
            (void)card->addButton("Renvoyer la r\xC3\xA9ponse", "commandes.depliage.renvoyer", ui::Icon::Refresh, [this] { resend(); }, noRight, true);
        else if (e.waiting())
            (void)card->addButton("D\xC3\xA9livrer\xE2\x80\xA6", "commandes.depliage.delivrer", ui::Icon::Ok, [this] { deliver(); }, noRight, true);
        return card;
    }

    void update() {
        const shop::Order* o = selected();
        const online::Entry* e = selectedRequest();
        const bool bz = busy();
        note_ = shopServiceNote(ws());
        const bool ready = ws().service().ready() && ws().can("demandes");
        reload_->setEnabled(ready && !fetching_);
        show(deliver_, o != nullptr);
        show(review_, o != nullptr);
        for (ui::Button* btn : {stripe_, receipt_, refund_}) show(btn, o && !e);
        show(refundAll_, o && !e && o->line > 0);
        show(acceptOnline_, e && !e->optionRequest());
        show(resend_, e && e->acceptedHere());
        show(delete_, o != nullptr);
        if (e) updateRequest(*e, bz);
        else if (o) updateOrder(*o, bz);
        if (o) {   // 1.48.1 : Supprimer... - les rangees choisies qui s'effacent (sans argent, closes)
            const DeletePlan plan = deletePlan();
            const bool any = !plan.orders.empty() || !plan.requests.empty();
            const std::string noRight = ws().can("demandes") ? std::string() : ws().whyNot("demandes");
            delete_->setEnabled(any && noRight.empty() && !bz);
            delete_->setTooltip(!noRight.empty() ? noRight
                                : bz         ? std::string(kBusy)
                                : any        ? "Effacer " + std::to_string(plan.gone.size()) + " \xC3\xA9l\xC3\xA9ment(s) sans argent (sur le service et ici) ; une question d'abord."
                                             : capitalFirst(plan.firstWhy) + ".");
        }
        invalidateLayout();
        invalidate();
    }

    // 1.48.1 : les boutons d'une demande de licence (comme l'ancienne page) ; sans le droit << demandes >> : grises, la raison.
    void updateRequest(const online::Entry& e, bool bz) {
        const std::string noRight = ws().can("demandes") ? std::string() : ws().whyNot("demandes");
        const bool here = e.acceptedHere(), opt = e.optionRequest();
        const std::string why = !noRight.empty() ? noRight
                                : bz             ? std::string(kBusy)
                                : here           ? std::string("D\xC3\xA9j\xC3\xA0 accept\xC3\xA9" "e ici : renvoie la r\xC3\xA9ponse.")
                                : !e.waiting()   ? "Cette demande n'attend pas de d\xC3\xA9" "cision (" + online::requestStateLabel(e.state) + ")."
                                                 : std::string();
        deliver_->setText("D\xC3\xA9livrer\xE2\x80\xA6");
        bool optOk = true;
        std::string optWhy;
        if (opt && why.empty()) {
            if (!ws().licence(e.licenseId)) optWhy = "La licence " + e.licenseId + " n'est pas dans l'archive de ce poste : r\xC3\xA9" "cup\xC3\xA8re-la d'abord.";
            else if (!ws().hasRegistry() || !ws().registry().option(e.option)) optWhy = "L'option " + e.option + " n'est pas dans le registre de ce poste.";
            optOk = optWhy.empty();
        }
        deliver_->setEnabled(why.empty() && optOk);
        deliver_->setTooltip(!why.empty() ? why
                             : !optOk     ? optWhy
                             : opt ? std::string("La fen\xC3\xAAtre \xC2\xAB Ajouter une option \xC2\xBB r\xC3\xA9gl\xC3\xA9" "e sur sa demande ; sign\xC3\xA9" "e, la nouvelle \xC3\xA9mission part sur le service et "
                                                 "son PC voit \xC2\xAB Accept\xC3\xA9" "e \xC2\xBB.")
                                   : std::string("L'assistant de licence, avec cette demande : une licence LI\xC3\x89" "E \xC3\xA0 ce PC ; sign\xC3\xA9" "e, elle part d'elle-m\xC3\xAAme \xC3\xA0 "
                                                 "chacun de ses PC."));
        acceptOnline_->setEnabled(why.empty() && !opt);
        acceptOnline_->setTooltip(!why.empty() ? why
                                               : std::string("Une licence EN LIGNE pour ce client (aucun PC li\xC3\xA9) : son code part scell\xC3\xA9 \xC3\xA0 son PC (il s'active tout seul) "
                                                             "et par mail ; gard\xC3\xA9 chiffr\xC3\xA9 ici."));
        review_->setEnabled(why.empty());
        review_->setTooltip(!why.empty() ? why
                                         : std::string("Le client voit \xC2\xAB Refus\xC3\xA9" "e \xC2\xBB et ta raison dans son logiciel (scell\xC3\xA9" "e : personne d'autre ne la lit)."));
        resend_->setEnabled(noRight.empty() && !bz && here);
        resend_->setTooltip(!noRight.empty() ? noRight
                                             : std::string("Cette demande est d\xC3\xA9j\xC3\xA0 accept\xC3\xA9" "e ici, mais sa r\xC3\xA9ponse n'est pas partie : la renvoyer au PC du "
                                                           "client (la m\xC3\xAA" "me licence, le m\xC3\xAA" "me code ; et le mail)."));
    }

    void updateOrder(const shop::Order& o, bool bz) {
        deliver_->setText("D\xC3\xA9livrer maintenant");
        const std::string why = ws().whyNotDeliver(o.kind);
        deliver_->setEnabled(o.deliverable() && why.empty() && !bz && note_.empty());
        deliver_->setTooltip(!o.deliverable() ? "Cette commande n'est pas \xC3\xA0 livrer (" + shop::stateLabel(o.state) + ")."
                             : !why.empty()   ? why
                                              : std::string("Signer la licence et la d\xC3\xA9poser tout de suite (le code part scell\xC3\xA9 au PC du client)."));
        stripe_->setEnabled(!o.stripePayment.empty() || !o.stripeSession.empty());
        receipt_->setEnabled(o.receiptUrl.rfind("https://", 0) == 0);
        review_->setEnabled(o.deliverable() && ws().can("demandes") && !bz);   // 1.48.0 : refuser une ligne a valider
        review_->setTooltip(!ws().can("demandes") ? ws().whyNot("demandes") : std::string());
        const bool plus = ws().role() == Role::Plus;
        const std::string notPlus = "R\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+.";
        const std::string whole = "Stripe rembourse toute la commande ; la licence livr\xC3\xA9" "e n'est pas r\xC3\xA9voqu\xC3\xA9" "e d'office.";
        if (o.line > 0) {   // 1.48.1 : cet article seulement ; toute la commande a part
            std::string lineWhy;
            if (o.state == "remboursee") lineWhy = "Cet article est d\xC3\xA9j\xC3\xA0 rembours\xC3\xA9.";
            else if (!o.payment) lineWhy = "Une demande sans paiement : rien \xC3\xA0 rembourser.";
            else if (!o.stripeRefund.empty()) lineWhy = "Le remboursement de toute la commande est d\xC3\xA9j\xC3\xA0 demand\xC3\xA9 \xC3\xA0 Stripe.";
            else if (!o.lineRefundable()) lineWhy = "Pas de paiement \xC3\xA0 rembourser pour cet article (" + shop::stateLabel(o.state) + ").";
            refund_->setEnabled(lineWhy.empty() && plus && !bz);
            refund_->setTooltip(!plus ? notPlus
                                : !lineWhy.empty() ? lineWhy
                                                   : "Stripe rembourse cet article seulement (" + shop::amountText(o.amount, o.currency) +
                                                         " TTC) ; les autres articles de la commande restent tels quels ; la licence livr\xC3\xA9" "e n'est pas r\xC3\xA9voqu\xC3\xA9" "e d'office.");
            refundAll_->setEnabled(o.refundable() && plus && !bz);
            refundAll_->setTooltip(!plus ? notPlus : !o.refundable() ? std::string("Pas de paiement \xC3\xA0 rembourser (" + shop::stateLabel(o.paidState()) + ").") : whole);
        } else {   // une commande sans lignes (d'avant la 1.48) : un seul article, toute la commande
            refund_->setEnabled(o.refundable() && plus && !bz);
            refund_->setTooltip(!plus ? notPlus : whole);
        }
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
            (void)drawWrapped(r, x, y + 30.f, w, "Choisis une commande ou une demande dans le tableau : son client, son PC, sa licence, son paiement chez Stripe.", kF13,
                              c.textMuted, 18.f, 4);
            return;
        }
        const online::Entry* e = selectedRequest();
        const std::string head = e ? std::string(kKindLicence) + " n\xC2\xB0 " + std::to_string(o->number)
                                 : (o->payment ? "Commande n\xC2\xB0 " : "Demande n\xC2\xB0 ") + std::to_string(o->number) +
                                       (o->lineCount > 1 && o->line > 0 ? " \xC2\xB7 article " + std::to_string(o->line) + " sur " + std::to_string(o->lineCount) : std::string());
        r.drawText({x, y}, fit(r, head, gfx::semibold(kF16), w), gfx::semibold(kF16), c.text);
        const Color ink = stateInk(o->state);
        const std::string stateWords = selectedRow() >= 0 ? model_->cellText(static_cast<RowIndex>(selectedRow()), 5) : shop::stateLabel(o->state);
        pill(r, x, y + 28.f, stateWords, kF12, ink, ink.withAlpha(26), 22.f, true);
        if (!e && o->mode == "test") pill(r, x + pillWidth(r, stateWords, kF12, true) + 8.f, y + 28.f, "paiement d'essai", kF12, rgb(0xB45309), rgb(0xFFF4E5));
        y += 62.f;
        const auto row = [&](const std::string& k, const std::string& v) {
            if (y + 18.f > buttonsTop_ - 8.f) return;
            r.drawText({x, y}, k, kF12, c.textMuted);
            r.drawText({x + 104.f, y}, fit(r, v.empty() ? std::string("\xE2\x80\x94") : v, kF13, w - 104.f), kF13, c.text);
            y += 21.f;
        };
        const auto note = [&](const std::string& text, Color ink2, int maxLines) {
            if (text.empty() || y + 20.f >= buttonsTop_ - 8.f) return;
            y += 4.f;
            y += drawWrapped(r, x, y, w, text, kF13, ink2, 18.f, std::max(1, std::min(maxLines, static_cast<int>((buttonsTop_ - 8.f - y) / 18.f))));
        };
        if (e) {   // 1.48.1 : une demande de licence (l'assistant du Studio)
            row("Type", std::string(kKindLicence) + " (Studio)");
            row("Demand\xC3\xA9", requestflow::wanted(ws(), *e));
            row("Soci\xC3\xA9t\xC3\xA9", e->customer);
            row("Client", e->contact);
            row("Courriel", e->email);
            row("PC", (e->deviceName.empty() ? std::string("(sans nom)") : e->deviceName) +
                          (e->fingerprint.empty() ? std::string() : " \xC2\xB7 " + e->fingerprint.substr(0, std::min<std::size_t>(e->fingerprint.size(), 14))));
            row("Logiciel", e->software);
            std::string lic = e->licenseId.empty() ? std::string("nouvelle") : e->licenseId;
            if (!e->licenseId.empty() && ws().licence(e->licenseId)) lic += e->optionRequest() ? " (une option)" : " (ajouter ce PC)";
            row("Licence", lic);
            row("Re\xC3\xA7ue", dateTimeFr(e->createdAt ? e->createdAt : e->requestedAt));
            if (e->state == online::RequestState::Accepted) row("Licence livr\xC3\xA9" "e", e->answer);
            if (!e->message.empty()) note("\xC2\xAB " + oneLine(e->message) + " \xC2\xBB", c.textMuted, 3);
            if (e->state == online::RequestState::Refused && !e->answer.empty()) note("Refus\xC3\xA9" "e : " + oneLine(e->answer), c.error, 3);
            if (e->acceptedHere())
                note("D\xC3\xA9j\xC3\xA0 accept\xC3\xA9" "e ici (licence " + e->answer + "), mais sa r\xC3\xA9ponse n'est pas partie : \xC2\xAB Renvoyer la r\xC3\xA9ponse \xC2\xBB. "
                     "Elle ne s'accepte pas une seconde fois.", c.warning, 4);
            else if (e->waiting() && e->joinRequest() && ws().licence(e->licenseId)) note(online::joinWarning(*e), c.warning, 5);   // n'importe qui peut demander
            return;
        }
        row("Type", o->payment ? std::string("Achat (pay\xC3\xA9 sur Stripe)") : std::string("Demande (le panier, sans paiement)"));
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
        if (!o->stripeRefund.empty()) row("Remboursement", o->stripeRefund + (o->paidState() == "remboursee" ? "" : " (demand\xC3\xA9)"));
        row("Re\xC3\xA7u", o->receiptUrl.empty() ? std::string() : std::string("la facture de Stripe (Ouvrir le re\xC3\xA7u)"));
        note(o->error, c.error, 8);
        if (o->state == "remboursee" && !o->licence.empty())
            note("Rembours\xC3\xA9" "e : la licence " + o->licence + " n'est pas r\xC3\xA9voqu\xC3\xA9" "e d'office (Licences \xE2\x80\xBA R\xC3\xA9voquer, si tu le d\xC3\xA9" "cides).", c.warning, 2);
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
                reloadAll();
            }))
            fetching_ = false;
        update();
    }

    // 1.48.1 : << Relever >> - les commandes ET les demandes de licence (rangees sur ce poste), sans bruit.
    void fetchAll() {
        if (fetching_) return;
        if (busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return;
        }
        Workspace& w = ws();
        if (!w.service().ready() || !w.signer()) return;
        const bool orders = shopServiceNote(w).empty();
        const Workspace* wp = &w;
        auto s = std::make_shared<shop::Settings>();
        auto v = std::make_shared<shop::OrdersView>();
        auto okS = std::make_shared<bool>(false);
        auto okO = std::make_shared<bool>(false);
        fetching_ = true;
        lastFetch_ = steadySeconds();
        auto alive = alive_;
        ShellHost h = host_;
        const bool started = runQuietly(
            host_, "Commandes et demandes : relev\xC3\xA9",
            [wp, s, v, okS, okO, orders] {
                OpResult r = OpResult::done("Commandes et demandes relev\xC3\xA9" "es.");
                if (orders) {
                    r = wp->shopSettings(*s);
                    *okS = r.ok;
                    if (r.ok) {
                        const OpResult o = wp->shopOrders(*v);
                        *okO = o.ok;
                        if (!o.ok) r = o;
                    }
                }
                const OpResult q = wp->serviceFetch();   // les demandes de licence (l'assistant du Studio)
                if (r.ok && !q.ok) r = q;
                return r;
            },
            [this, alive, h, s, v, okS, okO](const OpResult& r) {
                Workspace& here = h.workspace();
                if (*okS) here.adoptShop(*s);
                if (*okO) here.adoptOrders(*v);
                here.reload();   // les demandes relevees (le dossier)
                if (!*alive) return;
                fetching_ = false;
                problem_ = r.ok ? std::string() : r.message;
                reloadAll();
            });
        if (!started) fetching_ = false;
        update();
    }

    // Apres un geste sur une demande de licence : la page relue.
    std::function<void()> afterRequest() {
        auto alive = alive_;
        return [this, alive] {
            if (*alive) refresh();
        };
    }

    void deliver() {
        if (const online::Entry* e = selectedRequest()) {   // 1.48.1 : EXACTEMENT l'acceptation d'avant
            if (e->optionRequest()) requestflow::addOption(host_, *e);
            else requestflow::accept(host_, *e, false);
            return;
        }
        const shop::Order* o = selectedOrder();
        if (!o) return;
        if (busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return;
        }
        const int n = o->number, ln = o->line;
        const Workspace* wp = &ws();
        auto alive = alive_;
        (void)host_.runSlow("Boutique : livraison de la commande n\xC2\xB0 " + std::to_string(n), [wp, n, ln] { return wp->deliverOrders(n, ln, false); },
                            [this, alive](const OpResult&) {
                                if (!*alive) return;
                                lastFetch_ = -1;
                                fetch();
                            });
    }
    void acceptOnline() {
        if (const online::Entry* e = selectedRequest(); e && !e->optionRequest()) requestflow::accept(host_, *e, true);
    }
    void resend() {
        if (const online::Entry* e = selectedRequest()) requestflow::resend(host_, *e, afterRequest());
    }

    // 1.48.0 : refuser UNE ligne (une demande, ou une ligne payee : rembourse-la ensuite), la raison lue par le client.
    // 1.48.1 : une demande de licence : le refus d'avant (requestflow::askRefuse).
    void askReview() {
        if (const online::Entry* e = selectedRequest()) {
            requestflow::askRefuse(host_, *e, afterRequest());
            return;
        }
        const shop::Order* o = selectedOrder();
        if (!o || !host_.openDialog) return;
        const int n = o->number, ln = o->line > 0 ? o->line : 1;
        const bool linesKnown = o->linesKnown;
        auto alive = alive_;
        host_.openDialog(std::make_unique<AskDialog>(
            "boutique.revoir", "Refuser \xC2\xAB " + (o->label.empty() ? o->article : o->label) + " \xC2\xBB (commande n\xC2\xB0 " + std::to_string(n) + ")",
            std::string("Cet article ne sera pas livr\xC3\xA9 : dis pourquoi (le client le lit dans Mes achats).") +
                (o->payment ? " Il est pay\xC3\xA9 : rembourse-le ensuite si tu le d\xC3\xA9" "cides (Rembourser\xE2\x80\xA6)." : ""),
            true, "Refuser", true, [this, alive, n, ln, linesKnown](const std::string& reason) {
                if (!*alive) return;
                const Workspace* wp = &ws();
                (void)host_.runSlow("Boutique : article refus\xC3\xA9", [wp, n, ln, linesKnown, reason] { return linesKnown ? wp->refuseLine(n, ln, reason) : wp->reviewOrder(n, reason); },
                                    [this, alive](const OpResult&) {
                                        if (!*alive) return;
                                        lastFetch_ = -1;
                                        fetch();
                                    });
            }));
    }

    void refunded(const std::shared_ptr<bool>& alive) {
        changes::bump();   // 1.48.1 : toutes les pages se relisent (sans effet si rien n'a change)
        if (!*alive) return;
        lastFetch_ = -1;
        fetch();
    }

    // 1.48.1 : CE QUE SUPPRIMER EFFACE parmi les rangees choisies - chaque commande sans argent une fois (toutes ses lignes),
    // chaque demande de licence close ; le reste reste (la raison).
    struct DeletePlan {
        std::vector<int>                   orders, requests;
        std::vector<std::string>           gone, kept;
        std::map<std::string, std::string> what;   // "c12" / "d7" -> ce qui part (le journal)
        std::string                        firstWhy;
    };
    static std::string capitalFirst(std::string t) {
        if (!t.empty() && t[0] >= 'a' && t[0] <= 'z') t[0] = static_cast<char>(t[0] - 'a' + 'A');
        return t;
    }
    [[nodiscard]] DeletePlan deletePlan() const {
        DeletePlan p;
        std::vector<std::string> seen;
        for (const RowIndex r : table_->selectedModelRows()) {
            if (r >= model_->rows.size()) continue;
            const shop::Order& row = model_->rows[r];
            const bool request = model_->isRequest(r);
            const std::string key = (request ? "d" : "c") + std::to_string(row.number);
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            std::string why, name, what;
            bool ok = false;
            if (request) {
                const auto i = static_cast<std::size_t>(model_->req[r]);
                if (i >= entries_.size()) continue;
                const online::Entry& e = entries_[i];
                name = "la demande de licence n\xC2\xB0 " + std::to_string(e.number) + (e.customer.empty() ? std::string() : " (" + e.customer + ")");
                what = std::string(kKindLicence) + (e.customer.empty() ? std::string() : ", " + e.customer);
                ok = online::requestDeletable(e, &why);
            } else {
                const shop::Order* whole = ws().hasOrders() ? ws().orders().find(row.number) : nullptr;
                const shop::Order& w = whole ? *whole : row;
                const std::size_t n = w.lines.size();
                name = (w.payment ? "la commande n\xC2\xB0 " : "la demande n\xC2\xB0 ") + std::to_string(w.number) + " (" + (w.company.empty() ? std::string("?") : w.company) +
                       (n > 1 ? ", " + std::to_string(n) + " articles" : std::string()) + ")";
                what = (w.payment ? std::string(kKindPaid) : std::string(kKindCart)) + ", " + w.company + ", " + shop::stateLabel(w.state);
                ok = shop::orderDeletable(w, &why);
            }
            if (ok) {
                (request ? p.requests : p.orders).push_back(row.number);
                p.gone.push_back(name);
                p.what[key] = what;
            } else {
                p.kept.push_back(name + " : " + why);
                if (p.firstWhy.empty()) p.firstWhy = why;
            }
        }
        return p;
    }

    void askDelete() {
        if (!host_.openDialog) return;
        if (busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return;
        }
        DeletePlan plan = deletePlan();
        if (plan.orders.empty() && plan.requests.empty()) {
            if (host_.flash && !plan.firstWhy.empty()) host_.flash(capitalFirst(plan.firstWhy) + ".", Tone::Warning);
            return;
        }
        const std::size_t n = plan.gone.size();
        const auto list = [](const std::vector<std::string>& v) {
            std::string t;
            for (std::size_t i = 0; i < v.size(); ++i) t += (i ? (i + 1 == v.size() ? " et " : ", ") : "") + v[i];
            return t;
        };
        std::string text = "Dispara\xC3\xAEt du service et de ce poste : " + list(plan.gone) + ".";
        if (!plan.kept.empty()) text += " Reste : " + list(plan.kept) + ".";
        text += " Le journal des admins garde la trace.";
        const std::string title = n == 1 ? "Supprimer " + plan.gone.front() + " ?" : "Supprimer ces " + std::to_string(n) + " \xC3\xA9l\xC3\xA9ments ?";
        auto alive = alive_;
        auto shared = std::make_shared<DeletePlan>(std::move(plan));
        host_.openDialog(std::make_unique<AskDialog>("boutique.supprimer", title, text, false, "Supprimer", true, [this, alive, shared](const std::string&) {
            if (!*alive) return;
            const Workspace* wp = &ws();
            (void)host_.runSlow("Commandes et demandes : suppression", [wp, shared] { return wp->deleteOrdersAndRequests(shared->orders, shared->requests, shared->what); },
                                [this, alive](const OpResult& r) {
                                    if (r.ok) changes::bump();   // 1.48.1 : toutes les pages se relisent
                                    ws().reload();               // les demandes effacees de ce poste
                                    if (!*alive) return;
                                    lastFetch_ = -1;
                                    fetch();
                                    reloadAll();
                                });
        }));
    }

    // Toute la commande (comme avant la 1.48.1).
    void askRefund() {
        const shop::Order* o = selectedOrder();
        if (!o || !host_.openDialog) return;
        const int n = o->number;
        std::int64_t total = o->amount;   // une rangee sans lignes : la commande entiere
        if (const shop::Order* whole = ws().hasOrders() ? ws().orders().find(n) : nullptr) {
            total = whole->amount;
            for (const auto& l : whole->lines)   // 1.48.1 : un article deja rembourse ne l'est pas deux fois (Stripe rend le reste)
                if (l.state == "remboursee") total -= l.amount;
        }
        std::string text = "Stripe rembourse " + shop::amountText(std::max<std::int64_t>(0, total), o->currency) + " TTC \xC3\xA0 " +
                           (o->company.empty() ? std::string("ce client") : o->company) + " (la carte de son paiement). La commande passe \xC2\xAB rembours\xC3\xA9" "e \xC2\xBB quand Stripe le confirme.";
        if (!o->licence.empty()) text += " La licence " + o->licence + " reste valable : r\xC3\xA9voque-la toi-m\xC3\xAA" "me si tu le d\xC3\xA9" "cides (Licences).";
        auto alive = alive_;
        host_.openDialog(std::make_unique<AskDialog>("boutique.rembourser", "Rembourser la commande n\xC2\xB0 " + std::to_string(n) + " ?", text, false, "Rembourser", true,
                                                     [this, alive, n](const std::string&) {
                                                         if (!*alive) return;
                                                         const Workspace* wp = &ws();
                                                         (void)host_.runSlow("Boutique : remboursement", [wp, n] { return wp->refundOrder(n); },
                                                                             [this, alive](const OpResult&) { refunded(alive); });
                                                     }));
    }

    // 1.48.1 : UN article (la ligne choisie) - la question nomme l'article et son montant TTC.
    void askRefundLine() {
        const shop::Order* o = selectedOrder();
        if (!o || !host_.openDialog) return;
        if (o->line <= 0) {   // une commande sans lignes : un seul article, toute la commande
            askRefund();
            return;
        }
        const int n = o->number, ln = o->line;
        const std::string article = o->label.empty() ? o->article : o->label;
        const std::string amount = shop::amountText(o->amount, o->currency);
        std::string text = "Stripe rembourse " + amount + " TTC \xC3\xA0 " + (o->company.empty() ? std::string("ce client") : o->company) +
                           " pour cet article seulement (la carte de son paiement). L'article passe \xC2\xAB rembours\xC3\xA9 \xC2\xBB ; les autres articles de la commande n\xC2\xB0 " +
                           std::to_string(n) + " restent tels quels.";
        if (!o->licence.empty())
            text += o->kind == "option" ? " L'option reste sur la licence " + o->licence + " : retire-la toi-m\xC3\xAA" "me si tu le d\xC3\xA9" "cides (Licences)."
                                        : " La licence " + o->licence + " reste valable : r\xC3\xA9voque-la toi-m\xC3\xAA" "me si tu le d\xC3\xA9" "cides (Licences).";
        auto alive = alive_;
        const std::string what = article + ", " + amount;
        host_.openDialog(std::make_unique<AskDialog>("boutique.rembourser-article", "Rembourser \xC2\xAB " + article + " \xC2\xBB ?", text, false, "Rembourser " + amount, true,
                                                     [this, alive, n, ln, what](const std::string&) {
                                                         if (!*alive) return;
                                                         const Workspace* wp = &ws();
                                                         (void)host_.runSlow("Boutique : remboursement d'un article", [wp, n, ln, what] { return wp->refundLine(n, ln, what); },
                                                                             [this, alive](const OpResult&) { refunded(alive); });
                                                     }));
    }

    std::shared_ptr<bool>        alive_ = std::make_shared<bool>(true);
    ui::Button*                  reload_{nullptr};
    ui::Button*                  deploy_{nullptr};
    ui::FilterBar*               bar_{nullptr};                                    // 1.47.0
    std::shared_ptr<OrdersModel> all_ = std::make_shared<OrdersModel>();           // 1.47.0 : toutes (la barre filtre)
    std::vector<online::Entry>   entries_;                                         // 1.48.1 : les demandes de licence (rangees sur ce poste)
    listkit::WhoDid              who_{host_.workspace()};                          // 1.47.0 : qui a livre
    ui::TableView*               table_{nullptr};
    std::shared_ptr<OrdersModel> model_;
    ui::Button*                  deliver_{nullptr};
    ui::Button*                  acceptOnline_{nullptr};   // 1.48.1 : une demande de licence, en ligne (un code)
    ui::Button*                  resend_{nullptr};         // 1.48.1 : la reponse d'une demande acceptee ici
    ui::Button*                  stripe_{nullptr};
    ui::Button*                  receipt_{nullptr};
    ui::Button*                  review_{nullptr};
    ui::Button*                  refund_{nullptr};         // 1.48.1 : cet article
    ui::Button*                  refundAll_{nullptr};      // 1.48.1 : toute la commande
    ui::Button*                  delete_{nullptr};         // 1.48.1 : Supprimer... (sans argent)
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
