// =============================================================================
//  app/LicenceShopWindows.cpp - 1.46.0 : LES FENETRES DE LA BOUTIQUE (docs/BOUTIQUE.md,
//  par. 7) - le recapitulatif d'un achat (<< Acheter >>) et la fenetre << Commande n. N >>
// -----------------------------------------------------------------------------
//  ACHETER (dialog.achat) : l'offre (ou l'option et sa duree), ce qu'elle donne,
//  le montant TTC, la societe, le nom et le courriel du profil (<< Modifier mon
//  profil... >> : sa fenetre, par-dessus ; relu a sa fermeture), le paiement d'essai,
//  la phrase de la carte. << Payer ... sur Stripe... >> : la commande part sur le fil
//  du service (placeOrder) ; la page de paiement s'ouvre dans le navigateur
//  (service::poll) ; cette fenetre laisse la place a << Commande n. N >>.
//
//  COMMANDE N. N (dialog.commande) : son etat, suivi tout seul (le fil du service
//  relit les commandes toutes les 10 secondes pendant 30 minutes, puis toutes les
//  5 minutes tant qu'elle est payee) - a payer : Rouvrir la page de paiement,
//  Annuler la commande ; payee : << ta licence arrive >> ; livree : Voir ma licence
//  (la licence s'est activee seule : l'etat << poste >>, comme une demande acceptee) ;
//  expiree, annulee, refusee, remboursee : ce qui s'est passe. Le recu de Stripe :
//  Ouvrir le recu (le navigateur).
// =============================================================================
#include "Licence.hpp"

#include "LicenceService.hpp"
#include "screens/Screens.hpp"

#include "../licensing/Gateway.hpp"   // plausibleEmail
#include "../licensing/Licensing.hpp"
#include "../menu/MenuManager.hpp"
#include "../ui/DialogFrame.hpp"
#include "../ui/Icons.hpp"
#include "../ui/Shapes.hpp"
#include "../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace {

namespace ls = licence::service;
namespace shop = licence::shop;

constexpr gfx::FontId kTitle{17};
constexpr gfx::FontId kSmall{14};
constexpr gfx::FontId kValue{17};
constexpr gfx::FontId kBig{22};

std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
        if (!t.empty()) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

// Des lignes coupees a la largeur (les mots entiers ; << \n >> : une nouvelle ligne).
std::vector<std::string> wrap(const std::string& text, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= text.size()) {
        const std::size_t nl = text.find('\n', from);
        const std::string para = text.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        std::string line, word;
        const auto flush = [&] {
            if (word.empty()) return;
            const std::string t = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui::measureWidth(t, f) > w) {
                out.push_back(line);
                line = word;
            } else {
                line = t;
            }
            word.clear();
        };
        for (const char ch : para) {
            if (ch == ' ') flush();
            else word += ch;
        }
        flush();
        out.push_back(line);
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

menu::MenuTraits dialogTraits() {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

gfx::Color toneColor(const ui::PaintContext& ctx, int tone, gfx::Color base) {
    switch (tone) {
        case 1: return ctx.theme.tone(ui::Tone::Ok, base);
        case 2: return ctx.theme.tone(ui::Tone::Warning, base);
        case 3: return ctx.theme.tone(ui::Tone::Error, base);
        default: return base;
    }
}

// << 1 an >>, << 1 mois >>, << jusqu'a la fin de ta licence >>.
std::string durationText(int days) {
    if (days == 0) return "jusqu'\xC3\xA0 la fin de ta licence";
    if (days == 7) return "1 semaine";
    if (days == 14) return "2 semaines";
    if (days == 30) return "1 mois";
    if (days == 365) return "1 an";
    if (days > 0 && days % 365 == 0) return std::to_string(days / 365) + " ans";
    return std::to_string(days) + " jours";
}

std::string capitalized(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    return s;
}

// La commande montree au premier plan (les essais, les scripts).
class PurchaseWindow;
class OrderWindow;
PurchaseWindow* g_purchase = nullptr;
OrderWindow*    g_order = nullptr;
// Le numero des resultats du service juste avant << Payer >> : la fenetre << Commande >> (0) prend le resultat de CE travail,
// meme s'il est releve avant qu'elle soit construite (un service qui ne repond pas : tout de suite).
unsigned long long g_orderSince = 0;
// 1.48.0 : les lignes du panier parties avec la commande en cours (retirees du panier quand le service l'a recue).
std::vector<std::pair<std::string, bool>> g_sentLines;

} // namespace

namespace licence {

// ============================================================ ce que disent les fenetres ==
PurchaseView purchaseView(const shop::Catalogue& cat, const std::string& article, const Profile& p, const licensing::LicenseState& st) {
    PurchaseView v;
    v.article = article;
    v.card = shop::cardSentence();
    v.company = !p.company.empty() ? p.company : p.name;   // une personne seule : son nom
    v.name = !p.company.empty() ? p.name : std::string();
    v.email = p.email;
    v.title = "Acheter";
    const shop::Article* a = cat.find(article);
    if (!cat.selling() || !a) {
        v.missing = "Cet article n'est pas (ou plus) en vente en ligne : demande ta licence sans paiement (Demander).";
        return v;
    }
    v.ok = true;
    v.label = a->label;
    v.amount = shop::amountTtc(a->amount, cat.currency);
    if (cat.test()) v.testMode = shop::testModeText();
    const auto reg = licensing::getRegistry();
    if (a->kind == "edition") {
        const licensing::RegistryEditionView* e = nullptr;
        if (reg)
            for (const auto& x : reg->editions)
                if (x.id == a->edition) e = &x;
        v.what = e && !e->label.empty() ? e->label : a->edition;
        v.title = "Acheter " + v.what;
        if (e) {
            std::size_t all = 0;
            for (const auto& f : reg->features)
                if (!f.retired) ++all;
            const int pcs = std::max(1, e->maxDevices);
            std::string facts = e->type == "floating" ? std::to_string(pcs) + " postes en m\xC3\xAAme temps" : std::to_string(pcs) + " PC";
            facts += " \xC2\xB7 " + (e->type == "perpetual" || e->durationDays <= 0 ? std::string("sans fin") : durationText(e->durationDays));
            facts += " \xC2\xB7 " + std::to_string(e->features.size()) + (all ? "/" + std::to_string(all) : std::string()) +
                     (e->features.size() > 1 ? " fonctionnalit\xC3\xA9s" : " fonctionnalit\xC3\xA9");
            v.gives.push_back(facts);
        }
        v.gives.push_back("Une licence en ligne \xC3\xA0 ton nom : elle s'active seule sur ce PC d\xC3\xA8s que l'\xC3\xA9" "diteur l'a d\xC3\xA9livr\xC3\xA9" "e (aucun code \xC3\xA0 taper).");
    } else {
        const licensing::RegistryOptionView* o = reg ? reg->option(a->option) : nullptr;
        v.what = o && !o->label.empty() ? o->label : a->option;
        v.title = "Acheter une option";
        v.optionText = v.what + ", " + durationText(a->days);
        v.licenseId = st.licenseId;
        if (o && reg && !o->features.empty()) {
            std::string names;
            for (const auto& f : o->features) {
                std::string n = f;
                for (const auto& x : reg->features)
                    if (x.id == f && !x.label.empty()) n = x.label;
                names += (names.empty() ? "" : ", ") + n;
            }
            v.gives.push_back("Ouvre : " + names);
        }
        v.gives.push_back("Pour " + durationText(a->days) + (st.licenseId.empty() ? std::string() : ", ajout\xC3\xA9" "e \xC3\xA0 ta licence " + st.licenseId) +
                          " : elle arrive seule sur ce PC d\xC3\xA8s que l'\xC3\xA9" "diteur l'a d\xC3\xA9livr\xC3\xA9" "e, puis s'arr\xC3\xAAte \xC3\xA0 sa date.");
        if (st.licenseId.empty()) v.missing = "Une option s'ajoute \xC3\xA0 ta licence : active d'abord ta licence sur ce PC.";
    }
    if (v.missing.empty()) {
        if (v.company.empty()) v.missing = "Ta soci\xC3\xA9t\xC3\xA9 manque : la licence achet\xC3\xA9" "e est \xC3\xA0 son nom (Modifier mon profil\xE2\x80\xA6).";
        else if (v.email.empty()) v.missing = "Ton courriel manque : Stripe y envoie ta facture (Modifier mon profil\xE2\x80\xA6).";
        else if (!licensing::gateway::plausibleEmail(v.email))
            v.missing = "Ton courriel \xC2\xAB " + v.email + " \xC2\xBB ne ressemble pas \xC3\xA0 une adresse : corrige-le (Modifier mon profil\xE2\x80\xA6).";
    }
    v.confirm = "Payer " + v.amount + " sur Stripe\xE2\x80\xA6";
    return v;
}

PurchaseView cartView(const shop::Catalogue& cat, const shop::Cart& cart, const Profile& p) {
    PurchaseView v;
    v.card = shop::cardSentence();
    v.company = !p.company.empty() ? p.company : p.name;
    v.name = !p.company.empty() ? p.name : std::string();
    v.email = p.email;
    v.title = "Payer le panier";
    std::int64_t total = 0;
    std::size_t n = 0;
    for (const auto& l : cart.lines) {
        if (l.request) continue;
        const shop::Article* a = cat.find(l.article);
        if (!cat.selling() || !a) {
            v.missing = "\xC2\xAB " + l.label + " \xC2\xBB n'est plus en vente en ligne : retire-le du panier, ou demande-le sans paiement.";
            continue;
        }
        total += a->amount;
        ++n;
        v.gives.push_back(l.label + " \xE2\x80\x94 " + shop::amountTtc(a->amount, cat.currency) + (l.gives.empty() ? std::string() : " \xC2\xB7 " + l.gives));
    }
    v.ok = n > 0;
    v.what = std::to_string(n) + (n > 1 ? " articles" : " article");
    v.label = "le panier";
    v.amount = shop::amountTtc(total, cat.currency);
    if (cat.test()) v.testMode = shop::testModeText();
    if (n == 0 && v.missing.empty()) v.missing = "Aucune ligne \xC3\xA0 payer dans le panier.";
    if (v.missing.empty()) {
        if (v.company.empty()) v.missing = "Ta soci\xC3\xA9t\xC3\xA9 manque : la licence achet\xC3\xA9" "e est \xC3\xA0 son nom (Modifier mon profil\xE2\x80\xA6).";
        else if (v.email.empty()) v.missing = "Ton courriel manque : Stripe y envoie ta facture (Modifier mon profil\xE2\x80\xA6).";
    }
    v.confirm = "Payer " + v.amount + " sur Stripe\xE2\x80\xA6";
    return v;
}

OrderView orderView(const shop::Order& o, licensing::Seconds now) {
    OrderView v;
    v.title = o.number ? "Commande n\xC2\xB0 " + std::to_string(o.number) : std::string("Ta commande");
    v.state = capitalized(shop::stateLabel(o.state));
    v.text = shop::stateText(o, now);
    v.tone = shop::stateTone(o.state);
    if (!o.label.empty()) v.facts.emplace_back("Article", o.label);
    if (o.amount > 0) v.facts.emplace_back("Montant", shop::amountTtc(o.amount, o.currency));
    if (o.created) v.facts.emplace_back("Pass\xC3\xA9" "e le", ls::dateText(o.created));
    if (o.mode == "test") v.facts.emplace_back("Paiement", "d'essai : aucune carte r\xC3\xA9" "elle n'est d\xC3\xA9" "bit\xC3\xA9" "e");
    else if (o.mode == "reel") v.facts.emplace_back("Paiement", "par carte, sur la page s\xC3\xA9" "curis\xC3\xA9" "e de Stripe");
    if (o.state == shop::kToPay && o.expires) v.facts.emplace_back("Page de paiement", "ouverte jusqu'au " + ls::dateText(o.expires));
    if (!o.licence.empty()) v.facts.emplace_back("Licence", o.licence);
    if (o.updated && o.updated != o.created) v.facts.emplace_back("Dernier changement", ls::dateText(o.updated));
    for (const auto& l : o.lines)   // 1.48.0 : chaque ligne et son etat
        v.facts.emplace_back("Ligne " + std::to_string(l.index), l.label + " \xE2\x80\x94 " + shop::stateLabel(l.state) +
                                                                     (l.licence.empty() ? std::string() : " (" + l.licence + ")") +
                                                                     (l.error.empty() ? std::string() : " : " + l.error));
    if (!o.payment) v.facts.emplace_back("Paiement", "aucun (une demande \xC3\xA0 valider par l'\xC3\xA9" "diteur)");
    v.pay = o.state == shop::kToPay && !o.payUrl.empty();
    v.cancel = o.state == shop::kToPay || (o.state == shop::kToValidate &&
                                           std::none_of(o.lines.begin(), o.lines.end(), [](const shop::OrderLine& l) { return l.state == shop::kDelivered; }));
    v.receipt = !o.receiptUrl.empty();
    v.licence = o.state == shop::kDelivered;
    return v;
}

std::string orderHint(const std::vector<shop::Order>& orders, int* tone) {
    const shop::Order* o = shop::waitingOrder(orders);
    if (tone) *tone = 0;
    if (!o) return {};
    if (tone) *tone = 2;
    const std::string n = "commande n\xC2\xB0 " + std::to_string(o->number);
    if (o->state == shop::kToPay) return n + " : paiement en attente";
    if (o->state == shop::kPaid) return n + " pay\xC3\xA9" "e : licence en route";
    return n + " : en revue chez l'\xC3\xA9" "diteur";
}

} // namespace licence

namespace {

// ============================================================ Acheter ==
class PurchaseBody final : public ui::Widget {
public:
    PurchaseBody(menu::MenuManager& menus, std::string article, std::string optionText, std::function<void(bool)> close)
        : ui::Widget("dialog.achat.corps"), menus_(menus), article_(std::move(article)), optionText_(std::move(optionText)), close_(std::move(close)) {
        pay_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Payer", "dialog.achat.payer")));
        pay_->setStyle(ui::Button::Style::Primary);
        pay_->setTooltip("La page s\xC3\xA9" "curis\xC3\xA9" "e de Stripe s'ouvre dans ton navigateur : tu y tapes ta carte (ni vue ni gard\xC3\xA9" "e par XPG).");
        cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", "dialog.achat.annuler")));
        profile_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Modifier mon profil\xE2\x80\xA6", "dialog.achat.profil")));
        profile_->setStyle(ui::Button::Style::Flat);
        profile_->setIcon(ui::Icon::User);
        links_ += pay_->clicked->connect([this] { (void)pay(); });
        links_ += cancel_->clicked->connect([this] {
            if (close_) close_(false);
        });
        links_ += profile_->clicked->connect([this] {
            profileWasOpen_ = true;
            showProfileWindow(menus_);
        });
        reload();
    }
    void reload() {
        view_ = article_.empty() ? licence::cartView(shop::catalogue(), shop::loadCart(), licence::loadProfile())   // 1.48.0
                                 : licence::purchaseView(shop::catalogue(), article_, licence::loadProfile(), licensing::getLicenseState());
        if (!optionText_.empty() && !view_.optionText.empty()) view_.optionText = optionText_;
        pay_->setText(view_.ok ? view_.confirm : std::string("Payer"));
        pay_->setEnabled(view_.ok && view_.missing.empty());
        invalidateLayout();
        invalidate();
    }
    // << Payer >> : la commande part (le fil du service) ; la fenetre << Commande >> la suit.
    bool pay() {
        reload();
        if (!view_.ok || !view_.missing.empty()) {
            say(view_.missing.empty() ? std::string("Cet article n'est pas en vente en ligne.") : view_.missing, 3);
            return false;
        }
        if (article_.empty()) {   // 1.48.0 : le panier, ses lignes a payer
            std::string w;
            if (!licence::startCartOrder(menus_, true, false, &w, false)) {
                say(w, 2);
                return false;
            }
            if (close_) close_(true);   // la fenetre << Commande >> prend la suite
            return true;
        }
        g_orderSince = ls::revision();
        g_sentLines.clear();
        ls::OrderForm f;
        f.article = view_.article;
        f.customer = view_.company;
        f.name = view_.name;
        f.email = view_.email;
        f.licenseId = view_.licenseId;
        f.optionText = view_.optionText;
        if (!ls::startOrder(f)) {
            say("Un travail du service des licences est en cours : r\xC3\xA9" "essaie dans un instant.", 2);
            return false;
        }
        if (close_) close_(true);
        return true;
    }
    void poll() {
        // Mon profil, ouvert d'ici, s'est ferme : la societe, le nom et le courriel relus
        if (profileWasOpen_ && !profileWindowOpen()) {
            profileWasOpen_ = false;
            reload();
        }
    }
    [[nodiscard]] const licence::PurchaseView& view() const noexcept { return view_; }
    [[nodiscard]] const std::string& flash() const noexcept { return flash_; }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(660.f, std::max(480.f, r.w - 40.f));
        const float iw = w - 40.f;
        // les lignes, a la largeur
        givesL_.clear();
        for (const auto& g : view_.gives)
            for (auto& l : wrap("\xC2\xB7 " + g, kSmall, iw)) givesL_.push_back(std::move(l));
        missingL_ = view_.missing.empty() ? std::vector<std::string>{} : wrap(view_.missing, kSmall, iw - 20.f);
        cardL_ = wrap(view_.card, kSmall, iw - 34.f);
        const float lh = 20.f;
        float h = 42.f + 16.f;                       // l'en-tete
        h += 30.f + 22.f;                            // l'article et le montant ; le libelle de la facture
        h += static_cast<float>(givesL_.size()) * lh + 14.f;
        h += 1.f + 14.f + 26.f + 3.f * 24.f + 10.f;  // Facture a : la societe, le nom, le courriel
        if (!missingL_.empty()) h += static_cast<float>(missingL_.size()) * lh + 16.f + 10.f;
        if (!view_.testMode.empty()) h += 34.f + 8.f;
        h += static_cast<float>(cardL_.size()) * lh + 14.f + 10.f;
        if (!flash_.empty()) h += 30.f;
        h += 56.f;                                   // les boutons
        h = std::min(h, std::max(300.f, r.h - 20.f));
        frame_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, std::ceil(h)});
        closeBox_ = {frame_.right() - 36.f, frame_.y + 9.f, 24.f, 24.f};
        ui::dialogframe::keep(*this, closeBox_);
        // << Modifier mon profil... >> : a droite de << Facture a >>
        float y = frame_.y + 42.f + 16.f + 30.f + 22.f + static_cast<float>(givesL_.size()) * lh + 14.f + 1.f + 14.f;
        billY_ = y;
        const float pw = std::ceil(profile_->sizeHint().preferred.w + 10.f);
        profile_->setBounds({frame_.right() - 20.f - pw, y - 6.f, pw, 30.f});
        const float pb = std::max(220.f, pay_->sizeHint().preferred.w + 16.f), cb = std::max(110.f, cancel_->sizeHint().preferred.w + 10.f);
        pay_->setBounds({frame_.right() - 20.f - pb, frame_.bottom() - 46.f, pb, 32.f});
        cancel_->setBounds({pay_->bounds().x - 8.f - cb, frame_.bottom() - 46.f, cb, 32.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(frame_, c.panelBg);
        r.strokeRect(frame_, c.borderStrong, 1.f);
        r.fillRect({frame_.x, frame_.y, frame_.w, 42.f}, c.headerBg);
        ui::drawIcon(r, ui::Icon::Lock, {frame_.x + 14.f, frame_.y + 13.f, 16.f, 16.f}, c.text);
        r.drawText({frame_.x + 38.f, frame_.y + (42.f - r.lineHeight(kTitle)) * 0.5f}, fit(r, view_.title, kTitle, frame_.w - 90.f), kTitle, c.text);
        r.drawText({closeBox_.x + 7.f, closeBox_.y + 2.f}, "\xC3\x97", kTitle, c.textMuted);
        r.fillRect({frame_.x, frame_.y + 42.f, frame_.w, 1.f}, c.border);
        const float x = frame_.x + 20.f, iw = frame_.w - 40.f, lh = 20.f;
        float y = frame_.y + 42.f + 16.f;
        // l'article et son montant
        const float aw = view_.amount.empty() ? 0.f : r.measure(view_.amount, kBig).width;
        const std::string what = fit(r, view_.what.empty() ? view_.label : view_.what, kValue, iw - aw - 16.f);
        r.drawText({x, y + 3.f}, what, kValue, c.text);
        r.drawText({x + 1.f, y + 3.f}, what, kValue, c.text);
        if (!view_.amount.empty()) {
            r.drawText({x + iw - aw, y}, view_.amount, kBig, c.accent);
            r.drawText({x + iw - aw + 1.f, y}, view_.amount, kBig, c.accent);
        }
        y += 30.f;
        if (!view_.label.empty()) r.drawText({x, y}, fit(r, "Sur la facture : " + view_.label, kSmall, iw), kSmall, c.textMuted);
        y += 22.f;
        for (const auto& l : givesL_) {
            r.drawText({x, y}, l, kSmall, c.text);
            y += lh;
        }
        y += 14.f;
        r.fillRect({x, y - 7.f, iw, 1.f}, c.border);
        y += 1.f + 14.f - 7.f + 7.f;
        // Facture a
        r.drawText({x, billY_}, "Factur\xC3\xA9 \xC3\xA0 (ton profil)", kSmall, c.textMuted);
        float by = billY_ + 26.f;
        const struct {
            const char* label;
            std::string value;
        } rows[3] = {{"Soci\xC3\xA9t\xC3\xA9", view_.company}, {"Nom", view_.name}, {"Courriel", view_.email}};
        for (const auto& row : rows) {
            r.drawText({x, by}, row.label, kSmall, c.textMuted);
            const bool none = row.value.empty();
            r.drawText({x + 110.f, by}, fit(r, none ? std::string("\xE2\x80\x94") : row.value, kSmall, iw - 110.f), kSmall, none ? c.textMuted : c.text);
            by += 24.f;
        }
        y = by + 10.f;
        if (!missingL_.empty()) {
            const gfx::Color warn = ctx.theme.tone(ui::Tone::Warning, c.text);
            const gfx::Rect band{x, y, iw, static_cast<float>(missingL_.size()) * lh + 16.f};
            r.fillRect(band, c.headerBg);
            r.fillRect({band.x, band.y, 3.f, band.h}, warn);
            float my = band.y + 8.f;
            for (const auto& l : missingL_) {
                r.drawText({band.x + 12.f, my}, l, kSmall, warn);
                my += lh;
            }
            y += band.h + 10.f;
        }
        if (!view_.testMode.empty()) {
            const gfx::Color warn = ctx.theme.tone(ui::Tone::Warning, c.accent);
            const gfx::Rect band{x, y, iw, 34.f};
            r.fillRoundedRect(band, warn.withAlpha(36), 6.f);
            ui::shapes::strokeRoundedRect(r, band, warn, 6.f, 1.f);
            const std::string t = "ESSAI \xC2\xB7 " + view_.testMode;
            r.drawText({band.x + 12.f, band.y + (band.h - r.lineHeight(kSmall)) * 0.5f}, fit(r, t, kSmall, band.w - 24.f), kSmall, c.text);
            y += band.h + 8.f;
        }
        {   // la carte : la phrase, avec un cadenas
            const gfx::Rect box{x, y, iw, static_cast<float>(cardL_.size()) * lh + 14.f};
            r.fillRoundedRect(box, c.windowBg, 6.f);
            ui::shapes::strokeRoundedRect(r, box, c.border, 6.f, 1.f);
            ui::drawIcon(r, ui::Icon::Lock, {box.x + 10.f, box.y + 9.f, 14.f, 14.f}, ctx.theme.tone(ui::Tone::Ok, c.text));
            float cy = box.y + 7.f;
            for (const auto& l : cardL_) {
                r.drawText({box.x + 32.f, cy}, l, kSmall, c.text);
                cy += lh;
            }
            y += box.h + 10.f;
        }
        if (!flash_.empty()) {
            const gfx::Color ink = toneColor(ctx, tone_, c.text);
            r.drawText({x, y + 4.f}, fit(r, flash_, kSmall, iw), kSmall, ink);
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && closeBox_.contains(d->pos)) {
            if (close_) close_(false);
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    void say(std::string text, int tone) {
        flash_ = std::move(text);
        tone_ = tone;
        invalidateLayout();
        invalidate();
    }
    menu::MenuManager&       menus_;
    std::string              article_, optionText_;
    std::function<void(bool)> close_;
    licence::PurchaseView    view_;
    ui::Button*              pay_{nullptr};
    ui::Button*              cancel_{nullptr};
    ui::Button*              profile_{nullptr};
    std::vector<std::string> givesL_, missingL_, cardL_;
    std::string              flash_;
    int                      tone_{0};
    bool                     profileWasOpen_{false};
    float                    billY_{0};
    gfx::Rect                frame_{}, closeBox_{};
    core::ConnectionScope    links_;
};

class PurchaseWindow final : public menu::WidgetMenu {
public:
    PurchaseWindow(menu::MenuManager& menus, std::string article, std::string optionText)
        : menu::WidgetMenu("dialog.achat"), menus_(menus), article_(std::move(article)), optionText_(std::move(optionText)) {
        g_purchase = this;
    }
    ~PurchaseWindow() override {
        if (g_purchase == this) g_purchase = nullptr;
    }
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return "Acheter"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
            fermer(false);
            return ui::EventResult::Consumed;
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }
    void Update(const menu::FrameContext& f) override {
        menu::WidgetMenu::Update(f);
        if (body_) body_->poll();
    }
    // `paid` : la commande est partie - la fenetre << Commande >> prend la suite.
    void fermer(bool paid) {
        if (done_) return;
        done_ = true;
        if (g_purchase == this) g_purchase = nullptr;
        menu::MenuManager& menus = menus_;
        manager().CloseDialog(menu::DialogResult{paid ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, {}});
        if (paid) licence::showOrder(menus, 0);
    }
    [[nodiscard]] PurchaseBody* body() const noexcept { return body_; }

protected:
    core::Status buildUi() override {
        auto b = std::make_unique<PurchaseBody>(menus_, article_, optionText_, [this](bool paid) { fermer(paid); });
        body_ = b.get();
        setRoot(std::move(b));
        return core::ok();
    }

private:
    menu::MenuManager& menus_;
    std::string        article_, optionText_;
    PurchaseBody*      body_{nullptr};
    bool               done_{false};
};

// ============================================================ Commande n. N ==
class OrderBody final : public ui::Widget {
public:
    OrderBody(menu::MenuManager& menus, std::uint64_t number, std::function<void()> close)
        : ui::Widget("dialog.commande.corps"), menus_(menus), number_(number), close_(std::move(close)) {
        const auto mk = [this](const char* text, const char* id, ui::Button::Style style) {
            auto* b = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>(text, id)));
            b->setStyle(style);
            return b;
        };
        pay_ = mk("Rouvrir la page de paiement", "dialog.commande.payer", ui::Button::Style::Primary);
        pay_->setIcon(ui::Icon::Export);
        cancel_ = mk("Annuler la commande", "dialog.commande.annuler", ui::Button::Style::Danger);
        receipt_ = mk("Ouvrir le re\xC3\xA7u", "dialog.commande.recu", ui::Button::Style::Default);
        receipt_->setIcon(ui::Icon::Document);
        licence_ = mk("Voir ma licence", "dialog.commande.licence", ui::Button::Style::Primary);
        check_ = mk("V\xC3\xA9rifier maintenant", "dialog.commande.verifier", ui::Button::Style::Flat);
        check_->setIcon(ui::Icon::Refresh);
        closeButton_ = mk("Fermer", "dialog.commande.fermer", ui::Button::Style::Default);
        links_ += pay_->clicked->connect([this] { reopen(); });
        links_ += cancel_->clicked->connect([this] { askCancel(); });
        links_ += receipt_->clicked->connect([this] {
            if (const auto* o = order(); o && !o->receiptUrl.empty()) {
                const std::string w = ls::openPage(o->receiptUrl);
                say(w.empty() ? std::string("Le re\xC3\xA7u s'ouvre dans ton navigateur.") : "Le re\xC3\xA7u ne s'ouvre pas : " + w, w.empty() ? 1 : 3);
            }
        });
        links_ += licence_->clicked->connect([this] {
            menu::MenuManager& m = menus_;
            if (close_) close_();
            showLicenceWindow(m, licence::kTabMine);
        });
        links_ += check_->clicked->connect([this] {
            if (ls::startOrders()) say("Lecture de ta commande aupr\xC3\xA8s du service\xE2\x80\xA6", 0);
            else say("Un travail du service des licences est en cours : la commande est relue juste apr\xC3\xA8s.", 0);
        });
        links_ += closeButton_->clicked->connect([this] {
            if (close_) close_();
        });
        seenResult_ = number_ == 0 ? g_orderSince : ls::revision();
        refresh();
    }
    [[nodiscard]] std::uint64_t number() const noexcept { return number_; }
    [[nodiscard]] const licence::OrderView& view() const noexcept { return view_; }
    [[nodiscard]] const std::string& flash() const noexcept { return flash_; }
    [[nodiscard]] bool creating() const noexcept { return number_ == 0 && failed_.empty(); }
    void poll() {
        ls::poll();
        // la commande en train d'etre passee : son numero (ou pourquoi elle n'est pas passee) a la reponse du service
        if (const auto rev = ls::revision(); rev != seenResult_) {
            seenResult_ = rev;
            const auto res = ls::lastResult();
            if (number_ == 0 && res.job == ls::Job::Order) {
                if (res.orderNumber && res.ok && !g_sentLines.empty()) {   // 1.48.0 : recue - ces lignes quittent le panier
                    auto cart = shop::loadCart();
                    std::erase_if(cart.lines, [](const shop::CartLine& l) {
                        return std::any_of(g_sentLines.begin(), g_sentLines.end(), [&l](const auto& s) { return s.first == l.article && s.second == l.request; });
                    });
                    (void)shop::saveCart(cart, nullptr);
                    g_sentLines.clear();
                }
                if (res.orderNumber) {
                    number_ = res.orderNumber;
                    say(res.orderReopened ? "Une commande de ce PC attendait d\xC3\xA9j\xC3\xA0 son paiement : sa page est rouverte dans ton navigateur."
                                          : res.orderUrl.empty() ? "Ta demande est envoy\xC3\xA9" "e \xC3\xA0 l'\xC3\xA9" "diteur (sans paiement)."   // 1.48.0
                                                                 : "La page de paiement de Stripe s'ouvre dans ton navigateur.",
                        res.orderReopened ? 2 : 1);
                } else {
                    failed_ = res.message.empty() ? std::string("La commande n'a pas pu \xC3\xAAtre pass\xC3\xA9" "e.") : res.message;
                }
                refresh();
            } else if (res.job == ls::Job::CancelOrder || res.job == ls::Job::Orders) {
                if (!res.message.empty() && (res.job == ls::Job::CancelOrder || res.changed)) say(res.message, res.ok ? 1 : 2);
                refresh();
            }
        }
        if (const auto rev = ls::ordersRevision(); rev != seenOrders_) {
            seenOrders_ = rev;
            checkedAt_ = licensing::systemNow();
            refresh();
        }
        if (busy_ != ls::busyJob()) {
            busy_ = ls::busyJob();
            refresh();
        }
    }
    void refresh() {
        orders_ = ls::orders();
        const licensing::Seconds now = licensing::systemNow();
        if (const shop::Order* o = order()) {
            view_ = licence::orderView(*o, now);
        } else {
            view_ = licence::OrderView{};
            view_.title = number_ ? "Commande n\xC2\xB0 " + std::to_string(number_) : std::string("Ta commande");
            if (!failed_.empty()) {
                view_.state = "Commande non pass\xC3\xA9" "e";
                view_.text = failed_;
                view_.tone = 3;
            } else if (number_ == 0) {
                view_.state = "Cr\xC3\xA9" "ation de la commande\xE2\x80\xA6";
                view_.text = "Le service des licences cr\xC3\xA9" "e ta commande et demande sa page de paiement \xC3\xA0 Stripe. Elle s'ouvrira dans ton navigateur.";
                view_.tone = 2;
            } else {
                view_.state = "Inconnue";
                view_.text = "Cette commande n'est pas (encore) dans la liste de ce PC.";
            }
        }
        const bool idle = ls::busyJob() == ls::Job::None;
        const auto show = [](ui::Button* b, bool on) { b->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed); };
        show(pay_, view_.pay);
        show(cancel_, view_.cancel);
        show(receipt_, view_.receipt);
        show(licence_, view_.licence);
        // Verifier maintenant : une commande payee (regardee toutes les 5 minutes) ; a payer, le Studio regarde deja toutes les 10 s
        show(check_, number_ != 0 && order() && (order()->state == shop::kPaid || order()->state == shop::kReview));
        for (auto* b : {cancel_, check_}) b->setEnabled(idle);
        cancel_->setText(ls::busyJob() == ls::Job::CancelOrder ? std::string("Annulation\xE2\x80\xA6") : std::string("Annuler la commande"));
        invalidateLayout();
        invalidate();
    }
    // Les essais, les scripts.
    bool clickReopen() {
        if (pay_->visibility() != ui::Visibility::Visible) return false;
        reopen();
        return true;
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(640.f, std::max(480.f, r.w - 40.f));
        const float iw = w - 40.f;
        textL_ = wrap(view_.text, kSmall, iw);
        if (textL_.size() > 8) textL_.resize(8);
        flashL_ = flash_.empty() ? std::vector<std::string>{} : wrap(flash_, kSmall, iw);   // entiere (deux lignes au plus)
        if (flashL_.size() > 2) {
            flashL_.resize(2);
            flashL_.back() += " \xE2\x80\xA6";
        }
        const float lh = 20.f;
        float h = 42.f + 16.f + 34.f + 10.f + static_cast<float>(textL_.size()) * lh + 14.f + static_cast<float>(view_.facts.size()) * 24.f + 10.f;
        if (!flashL_.empty()) h += 20.f * static_cast<float>(flashL_.size()) + 8.f;
        h += 22.f + 56.f;   // la derniere verification ; les boutons
        frame_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, std::ceil(std::min(h, r.h - 20.f))});
        closeBox_ = {frame_.right() - 36.f, frame_.y + 9.f, 24.f, 24.f};
        ui::dialogframe::keep(*this, closeBox_);
        // les boutons : a droite Fermer ; a gauche ce que l'etat permet
        const float by = frame_.bottom() - 46.f;
        float right = frame_.right() - 20.f;
        for (ui::Button* b : {closeButton_, licence_, receipt_}) {
            if (b->visibility() != ui::Visibility::Visible) continue;
            const float bw = std::max(100.f, b->sizeHint().preferred.w + 14.f);
            right -= bw;
            b->setBounds({right, by, bw, 32.f});
            right -= 8.f;
        }
        float left = frame_.x + 20.f;
        for (ui::Button* b : {pay_, cancel_, check_}) {
            if (b->visibility() != ui::Visibility::Visible) continue;
            const float bw = b->sizeHint().preferred.w + 14.f;
            if (left + bw > right) {   // pas la place : au-dessus
                b->setBounds({frame_.x + 20.f, by - 40.f, bw, 32.f});
                continue;
            }
            b->setBounds({left, by, bw, 32.f});
            left += bw + 8.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(frame_, c.panelBg);
        r.strokeRect(frame_, c.borderStrong, 1.f);
        r.fillRect({frame_.x, frame_.y, frame_.w, 42.f}, c.headerBg);
        ui::drawIcon(r, ui::Icon::Document, {frame_.x + 14.f, frame_.y + 13.f, 16.f, 16.f}, c.text);
        r.drawText({frame_.x + 38.f, frame_.y + (42.f - r.lineHeight(kTitle)) * 0.5f}, view_.title, kTitle, c.text);
        r.drawText({closeBox_.x + 7.f, closeBox_.y + 2.f}, "\xC3\x97", kTitle, c.textMuted);
        r.fillRect({frame_.x, frame_.y + 42.f, frame_.w, 1.f}, c.border);
        const float x = frame_.x + 20.f, iw = frame_.w - 40.f, lh = 20.f;
        float y = frame_.y + 42.f + 16.f;
        // l'etat : une pastille
        {
            const gfx::Color ink = toneColor(ctx, view_.tone, c.text);
            const float tw = r.measure(view_.state, kValue).width;
            const gfx::Rect pill{x, y, tw + 28.f, 34.f};
            r.fillRoundedRect(pill, ink.withAlpha(36), 17.f);
            r.fillRoundedRect({pill.x + 10.f, pill.y + 13.f, 8.f, 8.f}, ink, 4.f);
            r.drawText({pill.x + 24.f, pill.y + (pill.h - r.lineHeight(kValue)) * 0.5f}, view_.state, kValue, c.text);
            if (const shop::Order* o = order(); o && o->mode == "test") {
                const std::string t = "ESSAI";
                const float ew = r.measure(t, kSmall).width + 16.f;
                const gfx::Color warn = ctx.theme.tone(ui::Tone::Warning, c.accent);
                const gfx::Rect tag{x + iw - ew, y + 6.f, ew, 22.f};
                r.fillRoundedRect(tag, warn.withAlpha(40), 4.f);
                r.drawText({tag.x + 8.f, tag.y + (tag.h - r.lineHeight(kSmall)) * 0.5f}, t, kSmall, c.text);
            }
            y += 34.f + 10.f;
        }
        for (const auto& l : textL_) {
            r.drawText({x, y}, l, kSmall, view_.tone == 3 ? toneColor(ctx, 3, c.text) : c.text);
            y += lh;
        }
        y += 14.f;
        for (const auto& [label, value] : view_.facts) {
            r.drawText({x, y}, label, kSmall, c.textMuted);
            r.drawText({x + 150.f, y}, fit(r, value, kSmall, iw - 150.f), kSmall, c.text);
            y += 24.f;
        }
        y += 10.f;
        for (const auto& l : flashL_) {
            r.drawText({x, y}, fit(r, l, kSmall, iw), kSmall, toneColor(ctx, tone_, c.text));
            y += 20.f;
        }
        if (!flashL_.empty()) y += 8.f;
        // la derniere verification (le fil du service la refait seul)
        std::string foot;
        if (ls::busyJob() == ls::Job::Order) foot = ls::jobText(ls::Job::Order);
        else if (const shop::Order* o = order(); o && shop::open(o->state))
            foot = (checkedAt_ ? "V\xC3\xA9rifi\xC3\xA9" "e \xC3\xA0 " + ls::dateText(checkedAt_).substr(11) + " \xC2\xB7 " : std::string()) +
                   (o->state == shop::kToPay ? "le Studio regarde toutes les 10 secondes" : "le Studio regarde toutes les 5 minutes");
        if (!foot.empty()) r.drawText({x, y}, fit(r, foot, kSmall, iw), kSmall, c.textMuted);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && closeBox_.contains(d->pos)) {
            if (close_) close_();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    [[nodiscard]] const shop::Order* order() const noexcept { return number_ ? shop::findOrder(orders_, number_) : nullptr; }
    void say(std::string text, int tone) {
        flash_ = std::move(text);
        tone_ = tone;
        invalidateLayout();
        invalidate();
    }
    void reopen() {
        const shop::Order* o = order();
        if (!o) return;
        const std::string w = ls::reopenPayment(*o);
        say(w.empty() ? std::string("La page de paiement de Stripe est rouverte dans ton navigateur.") : "La page de paiement ne s'ouvre pas : " + w, w.empty() ? 1 : 3);
    }
    void askCancel() {
        const shop::Order* o = order();
        if (!o) return;
        const std::uint64_t n = o->number;
        auto dialog = std::make_unique<MessageDialog>("Annuler la commande",
                                                      "La commande n\xC2\xB0 " + std::to_string(n) + " (" + o->label + ") est annul\xC3\xA9" "e et sa page de paiement ferm\xC3\xA9" "e : "
                                                      "rien n'est d\xC3\xA9" "bit\xC3\xA9. Tu pourras en passer une autre.",
                                                      MessageDialog::Icon::Question, "Annuler la commande");
        dialog->setCancelLabel("Garder la commande");
        menus_.ShowDialog(std::move(dialog), [this, n](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            if (!ls::startCancelOrder(n)) say("Un travail du service des licences est en cours : r\xC3\xA9" "essaie dans un instant.", 2);
            else say("Annulation de la commande\xE2\x80\xA6", 0);
            refresh();
        });
    }

    menu::MenuManager&       menus_;
    std::uint64_t            number_{0};
    std::function<void()>    close_;
    std::vector<shop::Order> orders_;
    licence::OrderView       view_;
    std::string              failed_;
    ui::Button*              pay_{nullptr};
    ui::Button*              cancel_{nullptr};
    ui::Button*              receipt_{nullptr};
    ui::Button*              licence_{nullptr};
    ui::Button*              check_{nullptr};
    ui::Button*              closeButton_{nullptr};
    std::vector<std::string> textL_, flashL_;
    std::string              flash_;
    int                      tone_{0};
    unsigned long long       seenResult_{0}, seenOrders_{0};
    ls::Job                  busy_{ls::Job::None};
    licensing::Seconds       checkedAt_{0};
    gfx::Rect                frame_{}, closeBox_{};
    core::ConnectionScope    links_;
};

class OrderWindow final : public menu::WidgetMenu {
public:
    OrderWindow(menu::MenuManager& menus, std::uint64_t number) : menu::WidgetMenu("dialog.commande"), menus_(menus), number_(number) { g_order = this; }
    ~OrderWindow() override {
        if (g_order == this) g_order = nullptr;
    }
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return "Commande"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
            fermer();
            return ui::EventResult::Consumed;
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }
    void Update(const menu::FrameContext& f) override {
        menu::WidgetMenu::Update(f);
        if (body_) body_->poll();
    }
    void fermer() {
        if (done_) return;
        done_ = true;
        if (g_order == this) g_order = nullptr;
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
    }
    [[nodiscard]] std::uint64_t number() const noexcept { return body_ ? body_->number() : number_; }
    [[nodiscard]] OrderBody* body() const noexcept { return body_; }

protected:
    core::Status buildUi() override {
        auto b = std::make_unique<OrderBody>(menus_, number_, [this] { fermer(); });
        body_ = b.get();
        setRoot(std::move(b));
        return core::ok();
    }

private:
    menu::MenuManager& menus_;
    std::uint64_t      number_{0};
    OrderBody*         body_{nullptr};
    bool               done_{false};
};

} // namespace

namespace licence {

void showPurchase(menu::MenuManager& menus, const std::string& article, const std::string& optionText) {
    if (g_purchase) return;   // une seule a la fois
    menus.ShowDialog(std::make_unique<PurchaseWindow>(menus, article, optionText), nullptr);
}

void showOrder(menu::MenuManager& menus, std::uint64_t number) {
    if (g_order) g_order->fermer();   // une seule a la fois : la nouvelle remplace
    menus.ShowDialog(std::make_unique<OrderWindow>(menus, number), nullptr);
}

bool purchaseWindowOpen() noexcept { return g_purchase != nullptr; }
bool orderWindowOpen() noexcept { return g_order != nullptr; }
std::uint64_t orderWindowNumber() noexcept { return g_order ? g_order->number() : 0; }

bool startCartOrder(menu::MenuManager& menus, bool payment, bool all, std::string* why, bool showWindow) {
    const auto cart = shop::loadCart();
    const auto p = loadProfile();
    ls::OrderForm f;
    f.customer = !p.company.empty() ? p.company : p.name;
    f.name = !p.company.empty() ? p.name : std::string();
    f.email = p.email;
    f.payment = payment;
    std::vector<std::pair<std::string, bool>> sent;
    for (const auto& l : cart.lines) {
        if (payment ? l.request : (!all && !l.request)) continue;
        shop::CartLine x = l;
        x.request = !payment;
        f.lines.push_back(std::move(x));
        sent.emplace_back(l.article, l.request);
    }
    if (f.lines.empty()) {
        if (why) *why = payment ? "Aucune ligne \xC3\xA0 payer dans le panier." : "Aucune ligne sans paiement dans le panier.";
        return false;
    }
    if (f.customer.empty() || f.email.empty()) {
        if (why) *why = "Ton profil n'a pas ta soci\xC3\xA9t\xC3\xA9 ou ton courriel (Modifier mon profil\xE2\x80\xA6) : l'\xC3\xA9" "diteur en a besoin.";
        return false;
    }
    g_orderSince = ls::revision();
    if (!ls::startOrder(f)) {
        if (why) *why = "Un travail du service des licences est en cours : r\xC3\xA9" "essaie dans un instant.";
        return false;
    }
    g_sentLines = std::move(sent);
    if (showWindow) showOrder(menus, 0);
    return true;
}

bool confirmPurchaseForTests() {
    if (!g_purchase || !g_purchase->body()) return false;
    return g_purchase->body()->pay();
}

} // namespace licence

} // namespace app
