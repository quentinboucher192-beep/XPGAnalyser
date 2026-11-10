// =============================================================================
//  license_admin/Shop.cpp - 1.46.0 : la boutique et les commandes, cote editeur
//  (voir Shop.hpp, docs/BOUTIQUE.md). JAMAIS LIVRE A UN CLIENT.
// =============================================================================
#include "Shop.hpp"

#include "../app/ai/AiHttp.hpp"
#include "../deploy/Crypto.hpp"
#include "../licensing/Admins.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <mutex>

namespace license_admin::shop {

namespace {

using core::json::Value;
namespace crypto = deploy::crypto;

online::Result refused(std::string why) {
    online::Result r;
    r.ok = false;
    r.message = "Refus\xC3\xA9 : " + std::move(why) + ".";
    return r;
}
online::Result done(std::string message) {
    online::Result r;
    r.ok = true;
    r.message = std::move(message);
    return r;
}

void wipe(std::string& s) {
    if (!s.empty()) crypto::wipe(s.data(), s.size());
    s.clear();
}

// Une ligne propre (sans caractere de controle), `max` octets au plus (sans couper un caractere).
std::string clean(std::string_view s, std::size_t max = 300) {
    std::string out;
    for (char c : s) out += (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) ? ' ' : c;
    if (out.size() > max) {
        std::size_t n = max;
        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) --n;
        out.resize(n);
        out += "\xE2\x80\xA6";
    }
    return out;
}

bool isEditionId(const std::string& s) {
    if (s.empty() || s.size() > 32 || !(s[0] >= 'A' && s[0] <= 'Z')) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; });
}
bool isOptionId(const std::string& s) {
    if (s.empty() || s.size() > 40) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}
bool oneLine(const std::string& s, std::size_t minChars, std::size_t maxChars) {
    std::size_t n = 0;
    for (char c : s) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n >= minChars && n <= maxChars;
}

// Le corps application/x-www-form-urlencoded d'un appel a Stripe.
std::string formEncode(const std::vector<std::pair<std::string, std::string>>& params) {
    static const char* hexd = "0123456789ABCDEF";
    std::string out;
    const auto enc = [&](const std::string& s) {
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
            else {
                out += '%';
                out += hexd[c >> 4];
                out += hexd[c & 15];
            }
        }
    };
    for (const auto& [k, v] : params) {
        if (!out.empty()) out += '&';
        enc(k);
        out += '=';
        enc(v);
    }
    return out;
}

std::mutex  g_baseMutex;
std::string g_testBase;

bool loopbackBase(const std::string& b) {
    for (const char* p : {"http://127.0.0.1", "http://localhost"}) {
        const std::string pre = p;
        if (b.rfind(pre, 0) != 0) continue;
        const std::string rest = b.substr(pre.size());
        if (rest.empty()) return true;
        if (rest[0] != ':' || rest.size() < 2 || rest.size() > 6) return false;
        return std::all_of(rest.begin() + 1, rest.end(), [](char c) { return c >= '0' && c <= '9'; });
    }
    return false;
}

struct StripeReply {
    int         status{0};
    std::string error;   // une panne du reseau ; vide : une reponse
    Value       json;
    [[nodiscard]] std::string message() const {
        if (const Value* e = json.isObject() ? json.find("error") : nullptr; e && e->isObject()) return maskStripe(clean(e->str("message")));
        return {};
    }
};

// Un appel a l'API de Stripe : la cle en Bearer (jamais ecrite ailleurs), le corps form-urlencoded.
StripeReply stripeCall(const std::string& method, const std::string& path, const std::vector<std::pair<std::string, std::string>>& params,
                       const std::string& secretKey) {
    StripeReply out;
    app::ai::HttpRequest req;
    req.method = method;
    req.url = stripeApiBase() + path;
    std::string auth = "Bearer " + secretKey;
    // 1.46.1 : la version de l'API fixee (sans elle : celle du compte, qui change sous nos pieds).
    req.headers = {{"Authorization", auth}, {"Accept", "application/json"}, {"User-Agent", "XPG-License-Admin"}, {"Stripe-Version", kStripeVersion}};
    wipe(auth);
    if (method != "GET" && method != "DELETE") {
        req.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
        req.body = formEncode(params);
    }
    req.connectTimeoutSeconds = 20;
    req.idleTimeoutSeconds = 60;
    std::string body;
    app::ai::HttpSink sink;
    sink.onData = [&body](std::string_view chunk) {
        if (body.size() + chunk.size() <= 4u * 1024u * 1024u) body.append(chunk);
    };
    auto client = app::ai::makeHttpClient();
    const auto resp = client->perform(req, sink);
    for (auto& h : req.headers)
        if (h.first == "Authorization") wipe(h.second);
    wipe(req.body);
    if (!resp.ok()) {
        app::ai::Url u;
        (void)app::ai::parseUrl(req.url, u, nullptr);
        out.error = resp.error.empty() ? app::ai::failureText(resp.failure, u.host) : resp.error;
        if (out.error.empty()) out.error = "pas de connexion";
        out.error = maskStripe(clean(out.error));
        return out;
    }
    out.status = resp.status;
    (void)core::json::parse(body, out.json, nullptr);
    wipe(body);   // le secret du webhook y passe
    return out;
}

std::string stripeExplain(const StripeReply& r, const std::string& what) {
    if (!r.error.empty()) return "Stripe ne r\xC3\xA9pond pas (" + what + ") : " + r.error;
    const std::string m = r.message();
    if (r.status == 401) return "Stripe refuse cette cl\xC3\xA9 (" + what + ") : v\xC3\xA9rifie que tu as coll\xC3\xA9 la cl\xC3\xA9 secr\xC3\xA8te enti\xC3\xA8re" + (m.empty() ? std::string() : " (" + m + ")");
    if (r.status == 403)
        return "cette cl\xC3\xA9 restreinte n'a pas le droit de " + what + " : donne-lui \xC2\xAB Webhook Endpoints : \xC3\xA9" "criture \xC2\xBB, \xC2\xAB Checkout Sessions : \xC3\xA9" "criture \xC2\xBB, \xC2\xAB Refunds : \xC3\xA9" "criture \xC2\xBB, \xC2\xAB Invoices : lecture \xC2\xBB, \xC2\xAB Products : \xC3\xA9" "criture \xC2\xBB et \xC2\xAB Prices : \xC3\xA9" "criture \xC2\xBB" +
               (m.empty() ? std::string() : " (" + m + ")");
    return "Stripe a r\xC3\xA9pondu " + std::to_string(r.status) + " (" + what + ")" + (m.empty() ? std::string() : " : " + m);
}

Seconds timeOf(const Value& v, std::string_view k) {
    const Value* x = v.find(k);
    return x && x->isNumber() ? static_cast<Seconds>(x->asInt(0)) : 0;
}

online::Result call(online::Client& c, const std::string& method, const std::string& path, const Value* body, const std::string& what, online::Reply* out = nullptr) {
    const online::Reply r = c.call(method, path, body ? core::json::write(*body, false) : std::string());
    if (out) *out = r;
    if (r.status < 200 || r.status >= 300 || !r.error.empty()) {
        if (r.error.empty() && r.status == 404 && r.code() == "route")
            return refused("le service en ligne ne conna\xC3\xAEt pas encore la boutique : d\xC3\xA9ploie la " + std::string(kServiceRequired) + " (Service en ligne \xE2\x80\xBA D\xC3\xA9ployer le service)");
        return refused(c.explain(r, what));
    }
    return done({});
}

} // namespace

// ============================================================================ le catalogue
const std::vector<std::string>& stripeEvents() {
    static const std::vector<std::string> v = {"checkout.session.completed", "checkout.session.async_payment_succeeded",
                                               "checkout.session.async_payment_failed", "checkout.session.expired", "charge.refunded"};
    return v;
}

std::string articleId(const std::string& kind, const std::string& id, int days) {
    if (kind == "option") return "opt:" + id + ":" + std::to_string(days);
    return "ed:" + id;
}

const Article* Settings::find(const std::string& id) const {
    for (const auto& a : articles)
        if (a.id == id) return &a;
    return nullptr;
}

bool parseSettings(const Value& v, Settings& out, std::string* why) {
    out = Settings{};
    if (!v.isObject()) {
        if (why) *why = "le r\xC3\xA9glage de la boutique est illisible";
        return false;
    }
    out.active = v.flag("active");
    out.autoDeliver = v.flag("livraison_auto", true);
    out.currency = v.str("devise", "eur");
    out.number = v.integer("numero");
    out.open = v.flag("ouverte");
    if (const Value* a = v.find("articles"); a && a->isArray())
        for (const auto& x : a->items()) {
            if (!x.isObject()) continue;
            Article art;
            art.kind = x.str("genre");
            art.edition = x.str("edition");
            art.option = x.str("option");
            art.days = static_cast<int>(x.integer("jours"));
            art.amount = x.integer("montant");
            art.label = x.str("libelle");
            art.id = x.str("id", articleId(art.kind, art.kind == "option" ? art.option : art.edition, art.days));
            // 1.46.1 : libre ou suivi (un reglage d'avant : ni l'un ni l'autre - followRegistry decide), la reduction
            if (const Value* l = x.find("libre"); l && l->isBool()) art.free = l->asBool();
            else art.legacy = true;
            art.reduced = x.integer("montant_reduit");
            art.reducedUntil = timeOf(x, "fin_reduction");
            if (art.reduced <= 0 || art.reducedUntil <= 0) art.reduced = 0, art.reducedUntil = 0;
            art.current = x.integer("montant_actuel");
            art.stripeProduct = clean(x.str("stripe_produit"), 120);
            out.articles.push_back(std::move(art));
        }
    if (const Value* s = v.find("stripe"); s && s->isObject()) {
        out.stripe.connected = s->flag("branche");
        out.stripe.mode = s->str("mode");
        out.stripe.webhook = s->flag("webhook");
        out.stripe.lastEvent = timeOf(*s, "dernier_evenement");
    }
    if (const Value* c = v.find("stripe_catalogue"); c && c->isObject()) (void)parseCatalogState(*c, out.catalog);   // 1.46.1
    return true;
}

bool parseCatalogState(const Value& v, CatalogState& out) {
    out = CatalogState{};
    if (!v.isObject()) return false;
    out.state = clean(v.str("etat"), 20);
    out.message = maskStripe(clean(v.str("message"), 600));
    out.when = timeOf(v, "quand");
    out.products = static_cast<int>(v.integer("produits"));
    return !out.state.empty();
}

std::string writeSettings(const Settings& s) {
    Value v = Value::object();
    v.set("active", s.active);
    v.set("livraison_auto", s.autoDeliver);
    v.set("devise", s.currency.empty() ? std::string("eur") : s.currency);
    Value arr = Value::array();
    for (const auto& a : s.articles) {
        Value x = Value::object();
        x.set("id", articleId(a.kind, a.kind == "option" ? a.option : a.edition, a.days));
        x.set("genre", a.kind);
        if (a.kind == "option") {
            x.set("option", a.option);
            x.set("jours", a.days);
        } else {
            x.set("edition", a.edition);
        }
        x.set("montant", a.amount);
        x.set("libelle", a.label);
        // 1.46.1 : libre ou suivi ; la reduction (suivi seulement). Un article d'avant (legacy) part tel qu'il est venu.
        if (!a.legacy) x.set("libre", a.free);
        if (!a.free && a.reduced > 0 && a.reducedUntil > 0) {
            x.set("montant_reduit", a.reduced);
            x.set("fin_reduction", static_cast<std::int64_t>(a.reducedUntil));
        }
        arr.push(std::move(x));
    }
    v.set("articles", std::move(arr));
    return core::json::write(v, false);
}

bool validSettings(const Settings& s, std::string* why) {
    const auto fail = [&](std::string t) {
        if (why) *why = std::move(t);
        return false;
    };
    if (s.articles.size() > kMaxArticles) return fail("le catalogue a plus de 200 articles");
    std::vector<std::string> seen;
    for (const auto& a : s.articles) {
        const std::string name = a.label.empty() ? a.id : a.label;
        if (a.kind == "edition" && !isEditionId(a.edition)) return fail("l'offre \xC2\xAB " + a.edition + " \xC2\xBB n'a pas un identifiant du registre");
        if (a.kind == "option" && (!isOptionId(a.option) || a.days < 0 || a.days > 3660)) return fail("l'option \xC2\xAB " + a.option + " \xC2\xBB (ou sa dur\xC3\xA9" "e) n'est pas lisible");
        if (a.kind != "edition" && a.kind != "option") return fail("un article n'est ni une offre ni une option");
        if (a.amount < kMinAmount || a.amount > kMaxAmount)
            return fail("le montant de \xC2\xAB " + name + " \xC2\xBB doit aller de 0,50 \xE2\x82\xAC \xC3\xA0 1 000 000 \xE2\x82\xAC TTC (" + amountText(a.amount) + ")");
        if (!oneLine(a.label, 1, 120)) return fail("le libell\xC3\xA9 de facture de \xC2\xAB " + a.id + " \xC2\xBB : une ligne, 1 \xC3\xA0 120 caract\xC3\xA8res");
        if (!a.free && a.reduced > 0 && (a.reduced < kMinAmount || a.reduced > kMaxAmount))   // 1.46.1
            return fail("le montant r\xC3\xA9" "duit de \xC2\xAB " + name + " \xC2\xBB doit aller de 0,50 \xE2\x82\xAC \xC3\xA0 1 000 000 \xE2\x82\xAC TTC (" + amountText(a.reduced) + ")");
        const std::string id = articleId(a.kind, a.kind == "option" ? a.option : a.edition, a.days);
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) return fail("\xC2\xAB " + id + " \xC2\xBB est deux fois dans le catalogue");
        seen.push_back(id);
    }
    return true;
}

// ============================================================================ 1.46.1 : le montant suit le registre
bool shownPriceTtc(const std::string& price, std::int64_t& cents, const std::string& base) {
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
    if (num.empty() || !parseAmount(num, v)) return false;
    const auto low = [](std::string t) {
        for (auto& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return t;
    };
    const std::string p = low(price), b = low(base);
    // HT : le texte le dit ; ou il ne dit rien (ni HT ni TTC) et le prix normal dont il est la reduction est HT.
    const bool ht = p.find("ht") != std::string::npos || (p.find("ttc") == std::string::npos && !b.empty() && b.find("ht") != std::string::npos && b.find("ttc") == std::string::npos);
    if (ht) v = static_cast<std::int64_t>(std::llround(static_cast<double>(v) * 1.2));
    cents = v;
    return true;
}

RegistryAmounts registryAmounts(const licensing::Registry& reg, const Article& a) {
    RegistryAmounts r;
    if (a.kind == "edition") {
        const licensing::EditionDef* e = reg.edition(a.edition);
        if (!e) return r;
        r.price = e->price;
        r.ok = !e->price.empty() && shownPriceTtc(e->price, r.amount) && r.amount >= kMinAmount && r.amount <= kMaxAmount;
        std::int64_t p = 0;
        if (r.ok && !e->promoPrice.empty() && e->promoUntil > 0 && shownPriceTtc(e->promoPrice, p, e->price) && p >= kMinAmount && p <= kMaxAmount) {
            r.reduced = p;
            r.until = e->promoUntil;
            r.promo = e->promoPrice;
        }
    } else if (a.kind == "option") {
        const licensing::OptionDef* o = reg.option(a.option);
        if (!o) return r;
        for (const auto& d : o->durations)
            if (d.days == a.days) {
                r.price = d.price;
                r.ok = !d.price.empty() && shownPriceTtc(d.price, r.amount) && r.amount >= kMinAmount && r.amount <= kMaxAmount;
            }
    }
    if (!r.ok) r.amount = 0;
    return r;
}

int followRegistry(Settings& s, const licensing::Registry& reg, Seconds now, bool resolveLegacy) {
    int changed = 0;
    for (auto& a : s.articles) {
        const RegistryAmounts r = registryAmounts(reg, a);
        if (a.legacy) {
            if (!resolveLegacy) continue;
            // Un reglage d'avant la 1.46.1 : le montant etait celui du registre (de base, ou reduit en cours) -> il le suit ;
            // un autre montant, tape par l'editeur -> libre (rien ne change pour lui).
            const bool same = r.ok && (std::llabs(a.amount - r.amount) <= 1 || (r.reduced > 0 && now < r.until && std::llabs(a.amount - r.reduced) <= 1));
            a.legacy = false;
            a.free = !same;
            ++changed;
        }
        if (a.free || !r.ok) {
            if (!a.free) {   // plus rien a suivre (le prix affiche ne se lit plus) : libre, avec son montant
                a.free = true;
                ++changed;
            }
            if (a.reduced || a.reducedUntil) {
                a.reduced = 0;
                a.reducedUntil = 0;
                ++changed;
            }
            continue;
        }
        // La reduction telle que le registre la dit (finie ou non : le service ne prend que celle qui court) - le brouillon
        // ne change pas d'un jour a l'autre.
        const std::int64_t red = r.reduced;
        const Seconds until = red ? r.until : 0;
        if (a.amount != r.amount || a.reduced != red || a.reducedUntil != until) {
            a.amount = r.amount;
            a.reduced = red;
            a.reducedUntil = until;
            ++changed;
        }
    }
    return changed;
}

std::string articleAmountText(const Article& a, Seconds now, const std::string& currency) {
    if (a.free) return amountText(a.amount, currency) + " TTC (montant libre)";
    if (!a.reducedAt(now)) return amountText(a.amount, currency) + " TTC";
    const std::string d = licensing::isoDate(a.reducedUntil);   // "2026-12-22"
    const std::string day = d.size() >= 10 ? d.substr(8, 2) + "/" + d.substr(5, 2) : d;
    return amountText(a.reduced, currency) + " TTC (r\xC3\xA9" "duit jusqu'au " + day + ", puis " + amountText(a.amount, currency) + ")";
}

// ============================================================================ les commandes
const OrderLine* Order::findLine(int n) const {
    for (const auto& l : lines)
        if (l.line == n) return &l;
    return nullptr;
}

const Order* OrdersView::find(int number) const {
    for (const auto& o : orders)
        if (o.number == number) return &o;
    return nullptr;
}

bool parseOrders(const Value& v, OrdersView& out, std::string* why) {
    out = OrdersView{};
    const Value* list = v.isObject() ? v.find("commandes") : nullptr;
    if (!list || !list->isArray()) {
        if (why) *why = "le service a rendu une liste de commandes illisible";
        return false;
    }
    out.counter = v.integer("numero", -1);
    out.toDeliver = static_cast<int>(v.integer("a_livrer"));
    out.toValidate = static_cast<int>(v.integer("a_valider"));   // 1.48.0
    for (const auto& x : list->items()) {
        if (!x.isObject() || x.integer("numero") <= 0) continue;
        Order o;
        o.number = static_cast<int>(x.integer("numero"));
        o.device = clean(x.str("poste"), 64);
        o.deviceKey = clean(x.str("cle_pc"), 64);
        o.article = clean(x.str("article"), 80);
        o.kind = clean(x.str("genre"), 20);
        o.edition = clean(x.str("edition"), 40);
        o.option = clean(x.str("option"), 40);
        o.days = static_cast<int>(x.integer("jours"));
        o.amount = x.integer("montant");
        o.currency = clean(x.str("devise", "eur"), 3);
        o.label = clean(x.str("libelle"), 200);
        o.company = clean(x.str("societe"), 200);
        o.name = clean(x.str("nom"), 200);
        o.email = clean(x.str("courriel"), 254);
        o.existingLicence = clean(x.str("licence_existante"), 64);
        o.keyId = clean(x.str("cle"), 40);
        o.sealed = x.str("scelle");
        o.forOther = x.flag("pour_un_autre");
        o.state = clean(x.str("etat"), 20);
        o.mode = clean(x.str("mode"), 10);
        o.created = timeOf(x, "cree");
        o.updated = timeOf(x, "maj");
        o.expires = timeOf(x, "expire");
        o.paid = timeOf(x, "paye");
        o.takenAt = timeOf(x, "prise");
        o.stripeSession = clean(x.str("stripe_session"), 255);
        o.stripePayment = clean(x.str("stripe_paiement"), 255);
        o.stripeRefund = clean(x.str("stripe_remboursement"), 255);
        o.receiptUrl = clean(x.str("recu_url"), 2000);
        o.licence = clean(x.str("licence"), 64);
        o.error = clean(x.str("erreur"), 500);
        // 1.48.0 : les lignes (un service d'avant : une ligne, la commande elle-meme)
        o.payment = x.flag("paiement", true);
        if (const Value* ls = x.find("lignes"); ls && ls->isArray()) {
            o.linesKnown = true;
            for (const auto& y : ls->items()) {
                if (!y.isObject() || y.integer("ligne") <= 0) continue;
                OrderLine l;
                l.line = static_cast<int>(y.integer("ligne"));
                l.article = clean(y.str("article"), 80);
                l.kind = clean(y.str("genre"), 20);
                l.edition = clean(y.str("edition"), 40);
                l.option = clean(y.str("option"), 40);
                l.days = static_cast<int>(y.integer("jours"));
                l.amount = y.integer("montant");
                l.label = clean(y.str("libelle"), 200);
                l.existingLicence = clean(y.str("licence_existante"), 64);
                l.withLine = static_cast<int>(y.integer("avec_ligne"));
                l.state = clean(y.str("etat"), 20);
                l.licence = clean(y.str("licence"), 64);
                l.error = clean(y.str("erreur"), 500);
                o.lines.push_back(std::move(l));
            }
        }
        if (o.lines.empty()) {
            OrderLine l;
            l.line = 1;
            l.article = o.article;
            l.kind = o.kind;
            l.edition = o.edition;
            l.option = o.option;
            l.days = o.days;
            l.amount = o.amount;
            l.label = o.label;
            l.existingLicence = o.existingLicence;
            l.state = o.state;
            l.licence = o.licence;
            l.error = o.error;
            o.lines.push_back(std::move(l));
        }
        o.lineCount = static_cast<int>(o.lines.size());
        out.orders.push_back(std::move(o));
    }
    std::sort(out.orders.begin(), out.orders.end(), [](const Order& a, const Order& b) { return a.number > b.number; });
    return true;
}

// ============================================================================ les textes
std::string amountText(std::int64_t cents, const std::string& currency) {
    const bool neg = cents < 0;
    const std::uint64_t c = static_cast<std::uint64_t>(neg ? -cents : cents);
    std::string units = std::to_string(c / 100);
    std::string grouped;
    for (std::size_t i = 0; i < units.size(); ++i) {
        if (i > 0 && (units.size() - i) % 3 == 0) grouped += "\xC2\xA0";
        grouped += units[i];
    }
    const unsigned rest = static_cast<unsigned>(c % 100);
    std::string out = (neg ? "-" : "") + grouped + "," + (rest < 10 ? "0" : "") + std::to_string(rest);
    if (currency.empty() || currency == "eur") return out + "\xC2\xA0\xE2\x82\xAC";
    std::string up = currency;
    for (auto& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return out + " " + up;
}

bool parseAmount(std::string_view text, std::int64_t& cents) {
    std::string digits;
    int decimals = -1;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            if (decimals >= 0) {
                if (decimals >= 2) return false;
                ++decimals;
            }
            digits += c;
            if (digits.size() > 12) return false;
        } else if (c == ',' || c == '.') {
            if (decimals >= 0) return false;
            decimals = 0;
        } else if (c == ' ') {
            continue;
        } else if (static_cast<unsigned char>(c) == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xA0) {
            ++i;   // une espace insecable
        } else if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x82 &&
                   static_cast<unsigned char>(text[i + 2]) == 0xAC) {
            i += 2;   // le signe de l'euro
        } else {
            std::string rest(text.substr(i));
            for (auto& ch : rest) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            while (!rest.empty() && rest.back() == ' ') rest.pop_back();
            if (rest == "eur" || rest == "ttc" || rest == "eur ttc" || rest == "ttc eur") break;
            return false;
        }
    }
    if (digits.empty()) return false;
    std::int64_t v = std::strtoll(digits.c_str(), nullptr, 10);
    const int d = decimals < 0 ? 0 : decimals;
    for (int k = d; k < 2; ++k) v *= 10;
    cents = v;
    return true;
}

std::string stateLabel(const std::string& s) {
    if (s == "a_payer") return "en attente du paiement";
    if (s == "payee") return "pay\xC3\xA9" "e, \xC3\xA0 livrer";
    if (s == "livree") return "livr\xC3\xA9" "e";
    if (s == "expiree") return "expir\xC3\xA9" "e";
    if (s == "annulee") return "annul\xC3\xA9" "e";
    if (s == "echec_paiement") return "paiement refus\xC3\xA9";
    if (s == "remboursee") return "rembours\xC3\xA9" "e";
    if (s == "a_revoir") return "\xC3\xA0 revoir";
    if (s == "a_valider") return "\xC3\xA0 valider";   // 1.48.0 : une demande
    if (s == "refusee") return "refus\xC3\xA9" "e";
    return s;
}

std::string modeOfKey(std::string_view k) {
    const auto starts = [&](const char* p) { return k.substr(0, std::char_traits<char>::length(p)) == p; };
    if (k.size() < 12 || k.size() > 400) return {};
    if (std::any_of(k.begin(), k.end(), [](char c) { return !(std::isalnum(static_cast<unsigned char>(c)) || c == '_'); })) return {};
    if (starts("sk_live_") || starts("rk_live_")) return "reel";
    if (starts("sk_test_") || starts("rk_test_") || starts("essai_jeton_")) return "test";
    return {};
}

std::string modeLabel(const std::string& mode) {
    if (mode == "reel") return "Stripe en mode r\xC3\xA9" "el";
    if (mode == "test") return "Stripe en mode test";
    return "Stripe pas branch\xC3\xA9";
}

std::string dashboardUrl(const std::string& mode, const std::string& payment) {
    std::string u = "https://dashboard.stripe.com";
    if (mode == "test") u += "/test";
    bool ok = !payment.empty() && payment.size() < 120 &&
              std::all_of(payment.begin(), payment.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
    return ok ? u + "/payments/" + payment : u + "/payments";
}

std::string maskStripe(std::string t) {
    static const char* const prefixes[] = {"sk_live_", "sk_test_", "rk_live_", "rk_test_", "pk_live_", "pk_test_", "whsec_", "essai_jeton_"};
    for (const char* p : prefixes) {
        const std::string pre = p;
        std::size_t at = 0;
        while ((at = t.find(pre, at)) != std::string::npos) {
            std::size_t end = at + pre.size();
            while (end < t.size() && (std::isalnum(static_cast<unsigned char>(t[end])) || t[end] == '_')) ++end;
            t.replace(at, end - at, "[masqu\xC3\xA9]");
            at += 9;
        }
    }
    return t;
}

bool serviceKnowsShop(const std::string& version) {
    int a = 0, b = 0, c = 0;
    return licensing::admins::parseSemver(version, a, b, c) && licensing::admins::compareSemver(version, kServiceRequired) >= 0;
}

// ============================================================================ le service
online::Result readSettings(online::Client& c, Settings& out) {
    online::Reply r;
    online::Result res = call(c, "GET", "/admin/boutique", nullptr, "lire la boutique", &r);
    if (!res.ok) return res;
    std::string why;
    if (!parseSettings(r.json, out, &why)) return refused(why);
    return done("Boutique lue : " + std::string(out.active ? "ouverte" : "ferm\xC3\xA9" "e") + ", " + std::to_string(out.articles.size()) + " article(s), " +
                modeLabel(out.stripe.mode) + ".");
}

online::Result readSettings(const std::string& folder, const licensing::SigningKey& key, const online::Options& o, Settings& out) {
    std::unique_ptr<online::Client> c;
    std::string why;
    if (!online::openClient(folder, key, o, c, &why)) return refused(why);
    return readSettings(*c, out);
}

online::Result saveSettings(const std::string& folder, const licensing::SigningKey& key, const online::Options& o, const Settings& in) {
    std::string why;
    if (!validSettings(in, &why)) return refused(why);
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder, key, o, c, &why)) return refused(why);
    Value body;
    (void)core::json::parse(writeSettings(in), body, nullptr);
    online::Reply r;
    online::Result res = call(*c, "PUT", "/admin/boutique", &body, "enregistrer la boutique", &r);
    if (!res.ok) return res;
    online::Result out = done("Boutique enregistr\xC3\xA9" "e sur le service (r\xC3\xA9glage n\xC2\xB0 " + std::to_string(r.json.integer("numero")) + ") : " +
                              std::string(in.active ? "vente en ligne ouverte" : "vente en ligne arr\xC3\xAAt\xC3\xA9" "e") + ", " + std::to_string(in.articles.size()) + " article(s).");
    // 1.46.1 : les produits chez Stripe (le service les cree ; un refus n'empeche pas de vendre)
    CatalogState cs;
    if (const Value* x = r.json.find("stripe_catalogue"); x && parseCatalogState(*x, cs) && !cs.message.empty())
        out.details.push_back((cs.ok() ? "Stripe : " : "Stripe (produits) : ") + cs.message);
    return out;
}

online::Result syncStripeCatalog(const std::string& folder, const licensing::SigningKey& key, const online::Options& o, CatalogState& out) {
    out = CatalogState{};
    std::unique_ptr<online::Client> c;
    std::string why;
    if (!online::openClient(folder, key, o, c, &why)) return refused(why);
    Value b = Value::object();
    online::Reply r;
    online::Result res = call(*c, "POST", "/admin/boutique/stripe", &b, "mettre \xC3\xA0 jour les produits chez Stripe", &r);
    if (!res.ok) return res;
    const Value* x = r.json.find("stripe_catalogue");
    if (!x || !parseCatalogState(*x, out)) return refused("le service a rendu un \xC3\xA9tat des produits illisible");
    online::Result d = done("Stripe : " + (out.message.empty() ? out.state : out.message));
    d.ok = out.ok() || out.state == "pas_branche" || out.state == "partiel";
    if (!d.ok) d.message = "Stripe (produits) : " + out.message;
    return d;
}

online::Result listOrders(online::Client& c, OrdersView& out) {
    online::Reply r;
    online::Result res = call(c, "GET", "/admin/commandes", nullptr, "lire les commandes", &r);
    if (!res.ok) return res;
    std::string why;
    if (!parseOrders(r.json, out, &why)) return refused(why);
    return done(std::to_string(out.orders.size()) + " commande(s) relev\xC3\xA9" "e(s), " + std::to_string(out.toDeliver) + " \xC3\xA0 livrer.");
}

online::Result listOrders(const std::string& folder, const licensing::SigningKey& key, const online::Options& o, OrdersView& out) {
    std::unique_ptr<online::Client> c;
    std::string why;
    if (!online::openClient(folder, key, o, c, &why)) return refused(why);
    return listOrders(*c, out);
}

online::Result takeOrder(online::Client& c, int number, int* status) {
    Value b = Value::object();
    b.set("numero", number);
    b.set("lignes", true);   // 1.48.0 : cet Admin valide ligne par ligne
    online::Reply r;
    online::Result res = call(c, "POST", "/admin/commandes/prendre", &b, "prendre la commande n\xC2\xB0 " + std::to_string(number), &r);
    if (status) *status = r.error.empty() ? r.status : 0;
    if (!res.ok && r.error.empty() && r.status == 409)
        return refused(r.code() == "prise" ? "un autre Admin livre la commande n\xC2\xB0 " + std::to_string(number) + " en ce moment"
                                           : "la commande n\xC2\xB0 " + std::to_string(number) + " n'est plus \xC3\xA0 livrer (" + stateLabel(r.json.str("etat")) + ")");
    return res;
}

online::Result markDelivered(online::Client& c, int number, const std::string& licenseId, std::string* alreadyLicence) {
    Value b = Value::object();
    b.set("numero", number);
    b.set("licence", licenseId);
    online::Reply r;
    online::Result res = call(c, "POST", "/admin/commandes/livree", &b, "marquer la commande n\xC2\xB0 " + std::to_string(number) + " livr\xC3\xA9" "e", &r);
    if (!res.ok && r.error.empty() && r.status == 409 && alreadyLicence) *alreadyLicence = r.json.str("licence");
    return res;
}

online::Result markLine(online::Client& c, int number, int line, const std::string& action, const std::string& value, std::string* alreadyLicence) {
    Value b = Value::object();
    b.set("numero", number);
    b.set("ligne", line);
    b.set("action", action);
    if (action == "livree") b.set("licence", value);
    else b.set("raison", clean(value, 480));
    online::Reply r;
    online::Result res = call(c, "POST", "/admin/commandes/ligne", &b, "la ligne " + std::to_string(line) + " de la commande n\xC2\xB0 " + std::to_string(number), &r);
    if (!res.ok && r.error.empty() && r.status == 409 && alreadyLicence) *alreadyLicence = r.json.str("licence");
    return res;
}

online::Result markReview(online::Client& c, int number, const std::string& reason) {
    Value b = Value::object();
    b.set("numero", number);
    b.set("erreur", clean(reason, 480));
    return call(c, "POST", "/admin/commandes/a-revoir", &b, "mettre la commande n\xC2\xB0 " + std::to_string(number) + " \xC3\xA0 revoir");
}

online::Result refund(const std::string& folder, const licensing::SigningKey& key, const online::Options& o, int number) {
    std::unique_ptr<online::Client> c;
    std::string why;
    if (!online::openClient(folder, key, o, c, &why)) return refused(why);
    Value b = Value::object();
    b.set("numero", number);
    online::Reply r;
    online::Result res = call(*c, "POST", "/admin/commandes/rembourser", &b, "rembourser la commande n\xC2\xB0 " + std::to_string(number), &r);
    if (!res.ok) return res;
    return done("Remboursement de la commande n\xC2\xB0 " + std::to_string(number) + " demand\xC3\xA9 \xC3\xA0 Stripe (" + clean(r.json.str("remboursement"), 80) +
                ") : elle passe \xC2\xAB rembours\xC3\xA9" "e \xC2\xBB quand Stripe le confirme. La licence livr\xC3\xA9" "e n'est pas r\xC3\xA9voqu\xC3\xA9" "e d'office.");
}

// ============================================================================ Brancher Stripe
std::string stripeApiBase() {
    {
        std::lock_guard<std::mutex> lk(g_baseMutex);
        if (!g_testBase.empty()) return g_testBase;
    }
    if (const char* e = std::getenv("XPG_STRIPE_API_ESSAI"); e && *e && loopbackBase(e)) return e;
    return "https://api.stripe.com";
}

void setStripeApiBaseForTests(std::string base) {
    std::lock_guard<std::mutex> lk(g_baseMutex);
    g_testBase = loopbackBase(base) ? std::move(base) : std::string();
}

std::string ConnectReport::text() const {
    std::string t = reason;
    if (!next.empty()) t += "\n" + next;
    if (!link.empty()) t += "\n" + link;
    return t;
}

ConnectReport connectStripe(std::string& secretKey, const std::string& serviceUrlIn, const deployer::Options& kit, const deployer::LineSink& onLine,
                            const std::atomic<bool>* cancel) {
    ConnectReport rep;
    const auto say = [&](const std::string& l) {
        const std::string c = maskStripe(l);
        rep.lines.push_back(c);
        if (onLine) onLine(c);
    };
    const auto fail = [&](std::string reason, std::string next = {}) {
        wipe(secretKey);
        rep.ok = false;
        rep.reason = std::move(reason);
        rep.next = std::move(next);
        return rep;
    };
    // La cle : trimee (un copier-coller garde parfois un blanc), puis son mode.
    while (!secretKey.empty() && std::isspace(static_cast<unsigned char>(secretKey.back()))) secretKey.pop_back();
    std::size_t lead = 0;
    while (lead < secretKey.size() && std::isspace(static_cast<unsigned char>(secretKey[lead]))) ++lead;
    if (lead) secretKey.erase(0, lead);
    if (secretKey.rfind("pk_", 0) == 0)
        return fail("C'est la cl\xC3\xA9 PUBLIABLE (pk_\xE2\x80\xA6) : il faut la cl\xC3\xA9 SECR\xC3\x88TE (sk_\xE2\x80\xA6 ou une cl\xC3\xA9 restreinte rk_\xE2\x80\xA6).",
                    "Sur dashboard.stripe.com : D\xC3\xA9veloppeurs \xE2\x80\xBA Cl\xC3\xA9s API \xE2\x80\xBA Cl\xC3\xA9 secr\xC3\xA8te \xE2\x80\xBA R\xC3\xA9v\xC3\xA9ler, puis colle-la ici.");
    rep.mode = modeOfKey(secretKey);
    if (rep.mode.empty() || (secretKey.rfind("essai_jeton_", 0) == 0 && stripeApiBase().rfind("https://", 0) == 0))
        return fail("Ce n'est pas une cl\xC3\xA9 secr\xC3\xA8te de Stripe (elle commence par sk_live_, sk_test_, rk_live_ ou rk_test_).",
                    "Sur dashboard.stripe.com : D\xC3\xA9veloppeurs \xE2\x80\xBA Cl\xC3\xA9s API, copie la cl\xC3\xA9 secr\xC3\xA8te enti\xC3\xA8re.");
    const std::string serviceUrl = online::normalizeUrl(serviceUrlIn);
    if (serviceUrl.rfind("https://", 0) != 0 && !loopbackBase(serviceUrl))
        return fail("L'adresse du service (" + serviceUrl + ") n'est pas en https : Stripe n'envoie ses \xC3\xA9v\xC3\xA9nements qu'\xC3\xA0 une adresse https.",
                    "R\xC3\xA8gle l'adresse de ton service (Service en ligne), puis recommence.");
    const std::string hook = serviceUrl + "/v1/boutique/stripe";
    say(modeLabel(rep.mode) + " : le point d'arriv\xC3\xA9" "e du webhook \xC2\xAB " + hook + " \xC2\xBB.");
    // 1. les anciens points d'arrivee de cette adresse (un branchement refait) : effaces, sinon Stripe enverrait chaque
    //    evenement deux fois, l'un signe par un secret que le service n'a plus.
    {
        const StripeReply l = stripeCall("GET", "/v1/webhook_endpoints?limit=100", {}, secretKey);
        if (l.status != 200) return fail(stripeExplain(l, "lire tes points d'arriv\xC3\xA9" "e de webhook"));
        if (const Value* d = l.json.find("data"); d && d->isArray())
            for (const auto& e : d->items()) {
                if (online::normalizeUrl(e.str("url")) != hook) continue;
                const std::string id = clean(e.str("id"), 80);
                const StripeReply x = stripeCall("DELETE", "/v1/webhook_endpoints/" + id, {}, secretKey);
                if (x.status != 200) return fail(stripeExplain(x, "effacer l'ancien point d'arriv\xC3\xA9" "e " + id));
                ++rep.removed;
                say("Ancien point d'arriv\xC3\xA9" "e effac\xC3\xA9 : " + id + ".");
            }
    }
    if (cancel && cancel->load()) return fail("Annul\xC3\xA9 : rien n'a \xC3\xA9t\xC3\xA9 pos\xC3\xA9 sur le service.");
    // 2. le point d'arrivee neuf, et son secret (whsec_...)
    std::string webhookSecret;
    {
        std::vector<std::pair<std::string, std::string>> p = {{"url", hook}};
        for (std::size_t i = 0; i < stripeEvents().size(); ++i) p.emplace_back("enabled_events[" + std::to_string(i) + "]", stripeEvents()[i]);
        p.emplace_back("description", "XPG - service des licences (boutique)");
        p.emplace_back("api_version", kStripeVersion);   // 1.46.1 : les evenements dans la version que le service lit
        StripeReply w = stripeCall("POST", "/v1/webhook_endpoints", p, secretKey);
        if (w.status != 200) return fail(stripeExplain(w, "cr\xC3\xA9" "er le point d'arriv\xC3\xA9" "e du webhook"));
        rep.webhookId = clean(w.json.str("id"), 80);
        webhookSecret = w.json.str("secret");
        if (Value* s = w.json.find("secret")) *s = Value();
        if (webhookSecret.rfind("whsec_", 0) != 0 && webhookSecret.rfind("essai_jeton_", 0) != 0) {
            wipe(webhookSecret);
            return fail("Stripe n'a pas rendu le secret du webhook (" + rep.webhookId + ").");
        }
        say("Point d'arriv\xC3\xA9" "e du webhook cr\xC3\xA9\xC3\xA9 chez Stripe : " + rep.webhookId + " (5 \xC3\xA9v\xC3\xA9nements).");
    }
    // 3. les deux secrets poses sur le Worker (le kit du service : wrangler secret put, la valeur sur l'entree standard)
    for (const auto& [name, value] : {std::pair<const char*, std::string*>{"STRIPE_SECRET_KEY", &secretKey}, {"STRIPE_WEBHOOK_SECRET", &webhookSecret}}) {
        const deployer::Report r = deployer::putSecret(kit, name, *value, [&](const std::string& l) { say(l); }, cancel);
        if (!r.ok()) {
            wipe(webhookSecret);
            rep.outcome = r.outcome;
            rep.link = r.link;
            return fail(std::string("Le secret ") + name + " n'est pas pos\xC3\xA9 sur ton service : " + r.reason, r.next);
        }
        say(std::string("Secret ") + name + " pos\xC3\xA9 sur ton service.");
    }
    wipe(webhookSecret);
    wipe(secretKey);
    rep.ok = true;
    rep.outcome = deployer::Outcome::Ok;
    rep.reason = "Stripe est branch\xC3\xA9 (" + std::string(rep.mode == "reel" ? "mode r\xC3\xA9" "el : de vrais paiements" : "mode test : aucune carte r\xC3\xA9" "elle n'est d\xC3\xA9" "bit\xC3\xA9" "e") +
                 ") : la cl\xC3\xA9 secr\xC3\xA8te et le secret du webhook sont des secrets de ton service, gard\xC3\xA9s nulle part ailleurs.";
    rep.next = "Ouvre la vente en ligne et r\xC3\xA8gle les montants (Boutique), puis Enregistrer.";
    return rep;
}

} // namespace license_admin::shop
