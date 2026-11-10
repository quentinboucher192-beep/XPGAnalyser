// =============================================================================
//  license_admin/gui/WorkspaceShop.cpp - 1.46.0 : LA BOUTIQUE ET LES COMMANDES
//  DANS LE POSTE ADMIN (Workspace.hpp ; docs/BOUTIQUE.md, par. 5 et 6)
// -----------------------------------------------------------------------------
//  Le reglage de la boutique, les commandes, et LA LIVRAISON d'une commande
//  payee : le meme chemin qu'une demande en ligne acceptee - la demande scellee
//  de la commande ouverte par la cle de ce poste (online::openSealedRequest), la
//  licence en ligne de l'offre signee ici (prepareOnlineLicence : l'archive, le
//  registre des activations, le journal), deposee (serviceRunOnline), son code
//  scelle pour le PC (online::answerOrder : son etat << poste >>, le mail) ; une
//  option : la licence du client emise de nouveau avec elle (addOption, comme
//  une demande d'option acceptee en 1.42). Puis << livree >> sur le service.
//  Les livraisons commencees sont notees (boutique-livraisons.json) : une reprise
//  apres une panne du reseau depose la MEME licence (son code garde chiffre sur
//  ce poste), jamais une seconde.
// =============================================================================
#include "Workspace.hpp"

#include "../../deploy/Crypto.hpp"
#include "../../licensing/Service.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace {

namespace fs = std::filesystem;
namespace crypto = deploy::crypto;
using core::json::Value;

constexpr const char* kStartedFormat = "xpg-livraisons-boutique/1";

std::string joinFolder(const std::string& folder, const char* name) {
    const auto u = (fs::path(std::u8string(folder.begin(), folder.end())) / name).u8string();
    return std::string(u.begin(), u.end());
}

bool readFileText(const std::string& path, std::string& out, std::string* why) {
    crypto::Bytes raw;
    if (!crypto::readFile(path, raw, why, 16u * 1024u * 1024u)) return false;
    out.assign(raw.begin(), raw.end());
    return true;
}

OpResult fromResult(const online::Result& x) {
    OpResult r;
    r.ok = x.ok;
    r.message = x.message;
    r.details = x.details;
    return r;
}

void wipe(std::string& s) {
    if (!s.empty()) crypto::wipe(s.data(), s.size());
    s.clear();
}

// Les livraisons commencees : numero de la commande -> la licence signee pour elle.
struct Started {
    std::string licence, kind;
};
std::map<int, Started> readStarted(const std::string& folder) {
    std::map<int, Started> out;
    std::string text;
    Value v;
    const std::string path = joinFolder(folder, shop::kDeliveriesFile);
    std::error_code ec;
    if (!fs::exists(fs::path(std::u8string(path.begin(), path.end())), ec)) return out;
    if (!readFileText(path, text, nullptr) || !core::json::parse(text, v, nullptr) || !v.isObject() || v.str("format") != kStartedFormat) return out;
    if (const Value* c = v.find("commandes"); c && c->isObject())
        for (const auto& [k, x] : c->members()) {
            const int n = std::atoi(k.c_str());
            if (n > 0 && x.isObject()) out[n] = {x.str("licence"), x.str("genre")};
        }
    return out;
}
void writeStarted(const std::string& folder, const std::map<int, Started>& m) {
    Value v = Value::object();
    v.set("format", kStartedFormat);
    Value c = Value::object();
    for (const auto& [n, s] : m) {
        Value x = Value::object();
        x.set("licence", s.licence);
        x.set("genre", s.kind);
        c.set(std::to_string(n), std::move(x));
    }
    v.set("commandes", std::move(c));
    const std::string text = core::json::write(v, true) + "\n";
    (void)crypto::writePrivateFile(joinFolder(folder, shop::kDeliveriesFile), crypto::Bytes(text.begin(), text.end()), nullptr);
}

std::string articleText(const shop::Order& o) { return o.label.empty() ? o.article : o.label; }

// Une raison << a revoir >> lisible : une majuscule en tete (a-z, et a accentue en UTF-8 : \xC3\xA0..\xC3\xBE), un
// point a la fin (elle s'affiche telle quelle dans Commandes et dans le journal).
std::string sentence(std::string t) {
    while (!t.empty() && (t.back() == ' ' || t.back() == '\n' || t.back() == '\r')) t.pop_back();
    if (t.empty()) return t;
    const auto u = [&t](std::size_t i) { return static_cast<unsigned char>(t[i]); };
    if (u(0) >= 'a' && u(0) <= 'z') t[0] = static_cast<char>(u(0) - 'a' + 'A');
    else if (t.size() > 1 && u(0) == 0xC3 && u(1) >= 0xA0 && u(1) <= 0xBE && u(1) != 0xB7) t[1] = static_cast<char>(u(1) - 0x20);
    if (t.back() != '.' && t.back() != '!' && t.back() != '?') t += '.';
    return t;
}

} // namespace

// ============================================================================ ce que les pages lisent
bool Workspace::serviceKnowsShop() const {
    return service_.hasState && service_.state.reached && shop::serviceKnowsShop(service_.state.version);
}

std::string Workspace::whyNotDeliver(const std::string& kind) const {
    if (!signer()) return whyNot("demandes");
    if (!can("demandes")) return whyNot("demandes");
    const char* right = kind == "option" ? "licence_option" : "licence_creer";
    if (!can(right)) return whyNot(right);
    if (!hasRegistry_) return "Pas de registre sign\xC3\xA9 dans l'archive (Offres \xE2\x80\xBA Signer le registre).";
    if (!service_.ready()) return "Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne).";
    return {};
}

// ============================================================================ le reglage, les commandes (LENT)
OpResult Workspace::shopSettings(shop::Settings& out) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (caller.role != Role::Plus && caller.role != Role::Admin) return OpResult::fail("ce poste n'a pas de cl\xC3\xA9 qui signe (ni l'admin+, ni un admin de la liste)");
    return fromResult(shop::readSettings(folder_, *caller.key, detail::optionsFor(caller, now()), out));
}

OpResult Workspace::saveShop(const shop::Settings& in) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;   // l'admin+ seul
    const online::Options o = detail::optionsFor(caller, now());
    // 1.46.1 : les articles qui suivent le registre partent avec ses montants (de base, reduit, la fin) ; un article d'un
    // JSON d'avant (ni << libre >> ni suivi : la console) part tel qu'il est.
    shop::Settings settings = in;
    if (hasRegistry_) (void)shop::followRegistry(settings, registry_, now(), false);
    OpResult r = fromResult(shop::saveSettings(folder_, *caller.key, o, settings));
    if (!r.ok) return r;
    Value d = Value::object();
    d.set("vente", settings.active);
    d.set("livraison_auto", settings.autoDeliver);
    d.set("articles", static_cast<int>(settings.articles.size()));
    (void)detail::queueJournal(folder_, *caller.key, now(), "boutique", "boutique",
                               std::string("a r\xC3\xA9gl\xC3\xA9 la boutique (") + (settings.active ? "vente en ligne ouverte" : "vente en ligne arr\xC3\xAAt\xC3\xA9" "e") + ", " +
                                   std::to_string(settings.articles.size()) + " article(s))",
                               core::json::write(d, false), {}, {}, nullptr);
    (void)detail::flushJournalQuiet(folder_, caller, now());
    return r;
}

OpResult Workspace::shopOrders(shop::OrdersView& out) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, "demandes")) return *deny;
    return fromResult(shop::listOrders(folder_, *caller.key, detail::optionsFor(caller, now()), out));
}

OpResult Workspace::reviewOrder(int number, const std::string& reason) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, "demandes")) return *deny;
    const std::string text = sentence(reason);
    if (text.empty()) return OpResult::fail("dis pourquoi la commande est \xC3\xA0 revoir");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    OpResult r = fromResult(shop::markReview(*c, number, text));
    if (!r.ok) return r;
    Value d = Value::object();
    d.set("raison", text.substr(0, 300));
    (void)detail::queueJournal(folder_, *caller.key, now(), "commande_revoir", "commande " + std::to_string(number),
                               "a mis la commande n\xC2\xB0 " + std::to_string(number) + " \xC3\xA0 revoir", core::json::write(d, false), {}, {}, nullptr);
    (void)detail::flushJournalWith(folder_, *c);
    r.message = "Commande n\xC2\xB0 " + std::to_string(number) + " \xC3\xA0 revoir : " + text;
    return r;
}

OpResult Workspace::refundOrder(int number) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;   // l'admin+ seul
    OpResult r = fromResult(shop::refund(folder_, *caller.key, detail::optionsFor(caller, now()), number));
    if (!r.ok) return r;
    (void)detail::queueJournal(folder_, *caller.key, now(), "commande_rembourser", "commande " + std::to_string(number),
                               "a demand\xC3\xA9 le remboursement de la commande n\xC2\xB0 " + std::to_string(number) + " \xC3\xA0 Stripe", {}, {}, {}, nullptr);
    (void)detail::flushJournalQuiet(folder_, caller, now());
    return r;
}

OpResult Workspace::connectStripe(std::string& secretKey, const deployer::Options& kit, const deployer::LineSink& onLine, const std::atomic<bool>* cancel) const {
    std::string why;
    detail::Caller caller;
    const auto wipeAndFail = [&](std::string w) {
        wipe(secretKey);
        return OpResult::fail(std::move(w));
    };
    if (!detail::loadCaller(folder_, caller, &why)) return wipeAndFail(why);
    if (const auto deny = detail::callerDenied(caller, {})) {
        wipe(secretKey);
        return *deny;
    }
    online::Config cfg;
    if (!online::readConfig(folder_, cfg, &why)) return wipeAndFail("le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne) : " + why);
    const shop::ConnectReport rep = shop::connectStripe(secretKey, cfg.url, kit, onLine, cancel);
    OpResult r = rep.ok ? OpResult::done(rep.reason) : OpResult::fail(rep.reason);
    if (!rep.next.empty()) r.details.push_back(rep.next);
    if (!rep.link.empty()) r.details.push_back(rep.link);
    for (const auto& l : rep.lines) r.details.push_back(l);
    if (rep.ok) {
        // 1.46.1 : la cle neuve sur le service - il cree (ou met a jour) les produits et les prix chez Stripe tout de suite. Un
        // echec ne defait rien : ils se creent au prochain Enregistrer de la boutique.
        shop::CatalogState cs;
        const online::Result k = shop::syncStripeCatalog(folder_, *caller.key, detail::optionsFor(caller, now()), cs);
        const std::string line = k.ok ? k.message : "Produits chez Stripe : pas encore (" + k.message + ") ; ils se cr\xC3\xA9" "eront \xC3\xA0 l'enregistrement de la boutique.";
        r.details.push_back(line);
        if (onLine) onLine(line);
    }
    if (rep.ok) {
        Value d = Value::object();
        d.set("mode", rep.mode);
        d.set("webhook", rep.webhookId);
        (void)detail::queueJournal(folder_, *caller.key, now(), "stripe_brancher", "stripe",
                                   "a branch\xC3\xA9 Stripe (" + std::string(rep.mode == "reel" ? "mode r\xC3\xA9" "el" : "mode test") + ")", core::json::write(d, false), {}, {},
                                   nullptr);
        (void)detail::flushJournalQuiet(folder_, caller, now());
    }
    return r;
}

// ============================================================================ livrer (LENT)
OpResult Workspace::deliverOrders(int number, bool automatic, DeliveryReport* report) const { return deliverOrders(number, 0, automatic, report); }

OpResult Workspace::deliverOrders(int number, int line, bool automatic, DeliveryReport* report) const {
    // Un Workspace neuf, relu du dossier : celui de l'interface n'est jamais touche par un autre fil.
    Workspace w(folder_);
    if (clock_) {
        w.setClock(clock_);
        w.reload();
    }
    DeliveryReport local;
    return w.deliverHere(number, line, automatic, report ? report : &local);
}

// 1.48.0 : une ligne refusee (la raison) ; la commande finie quand toutes ses lignes le sont.
OpResult Workspace::refuseLine(int number, int line, const std::string& reason) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, "demandes")) return *deny;
    const std::string text = sentence(reason);
    if (text.empty()) return OpResult::fail("dis pourquoi la ligne est refus\xC3\xA9" "e");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    OpResult r = fromResult(shop::markLine(*c, number, line, "refusee", text));
    if (!r.ok) return r;
    Value d = Value::object();
    d.set("raison", text.substr(0, 300));
    d.set("ligne", line);
    (void)detail::queueJournal(folder_, *caller.key, now(), "commande_revoir", "commande " + std::to_string(number),
                               "a refus\xC3\xA9 la ligne " + std::to_string(line) + " de la commande n\xC2\xB0 " + std::to_string(number), core::json::write(d, false), {}, {},
                               nullptr);
    (void)detail::flushJournalWith(folder_, *c);
    r.message = "Commande n\xC2\xB0 " + std::to_string(number) + ", ligne " + std::to_string(line) + " refus\xC3\xA9" "e : " + text;
    return r;
}

OpResult Workspace::deliverHere(int number, int line, bool automatic, DeliveryReport* rep) {
    *rep = DeliveryReport{};
    if (!signer()) return OpResult::fail(key_.problem.empty() ? std::string("ce poste n'a pas de cl\xC3\xA9 qui signe") : key_.problem);
    if (const auto deny = denied("demandes")) return *deny;
    if (!service_.ready()) return OpResult::fail("le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne)");
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    const online::Options o = detail::optionsFor(caller, now());
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, o, c, &why)) return OpResult::fail(why);
    const std::string version = c->state().str("version");
    if (!shop::serviceKnowsShop(version))
        return OpResult::fail("ton service en ligne est en " + (version.empty() ? std::string("?") : version) + " : d\xC3\xA9ploie la " + shop::kServiceRequired +
                              " pour la boutique (Service en ligne \xE2\x80\xBA D\xC3\xA9ployer le service)");
    shop::Settings s;
    if (const online::Result rs = shop::readSettings(*c, s); !rs.ok) return fromResult(rs);
    if (automatic && !s.autoDeliver) {
        rep->autoOff = true;
        return OpResult::done("Livraison automatique arr\xC3\xAAt\xC3\xA9" "e (Boutique) : les commandes pay\xC3\xA9" "es attendent \xC2\xAB D\xC3\xA9livrer maintenant \xC2\xBB (Commandes).");
    }
    shop::OrdersView v;
    if (const online::Result rl = shop::listOrders(*c, v); !rl.ok) return fromResult(rl);
    std::vector<const shop::Order*> todo;
    for (const auto& x : v.orders)
        if (number > 0 ? x.number == number : x.toDeliver()) todo.push_back(&x);
    if (number > 0) {
        if (todo.empty()) return OpResult::fail("la commande n\xC2\xB0 " + std::to_string(number) + " n'existe pas sur le service");
        const shop::OrderLine* one = line > 0 ? todo.front()->findLine(line) : nullptr;
        const bool any = std::any_of(todo.front()->lines.begin(), todo.front()->lines.end(), [](const shop::OrderLine& l) { return l.toValidate(); });
        if (line > 0 ? (!one || !one->toValidate()) : (!todo.front()->deliverable() && !any))
            return OpResult::fail("la commande n\xC2\xB0 " + std::to_string(number) + (line > 0 ? ", ligne " + std::to_string(line) + "," : std::string()) +
                                  " n'est pas \xC3\xA0 livrer (" + shop::stateLabel(one ? one->state : todo.front()->state) + ")");
    }
    std::sort(todo.begin(), todo.end(), [](const shop::Order* a, const shop::Order* b) { return a->number < b->number; });
    int notMine = 0;   // automatique : celles qu'un autre Admin livrera (un droit qui manque ici)
    for (const shop::Order* x : todo) {
        if (const std::string w = whyNotDeliver(x->kind); !w.empty()) {
            if (automatic) {   // un autre Admin (l'admin+) la livrera : rien n'est change
                ++rep->skipped;
                ++notMine;
                rep->lines.push_back({x->number, false, false, {}, x->company, articleText(*x), w});
                continue;
            }
            return OpResult::fail(w);
        }
        (void)deliverOne(*c, *x, s, automatic, number > 0 ? line : 0, *rep);
    }
    (void)detail::flushJournalWith(folder_, *c);
    OpResult r;
    r.ok = rep->review == 0 && rep->skipped == notMine && (number == 0 || rep->delivered >= 1);
    if (todo.empty()) {
        r.ok = true;
        r.message = "Boutique : aucune commande pay\xC3\xA9" "e \xC3\xA0 livrer.";
        return r;
    }
    std::string head = "Boutique : " + std::to_string(rep->delivered) + (rep->delivered > 1 ? " commandes livr\xC3\xA9" "es" : " commande livr\xC3\xA9" "e");
    if (rep->review) head += ", " + std::to_string(rep->review) + " \xC3\xA0 revoir";
    if (rep->skipped) head += ", " + std::to_string(rep->skipped) + " pas encore";
    r.message = head + ".";
    for (const auto& l : rep->lines) {
        if (l.delivered) r.details.push_back("Commande n\xC2\xB0 " + std::to_string(l.number) + " livr\xC3\xA9" "e : licence " + l.licence + " (" + l.customer + ", " + l.article + ").");
        else if (l.review) r.details.push_back("Commande n\xC2\xB0 " + std::to_string(l.number) + " \xC3\xA0 revoir : " + l.problem);
        else r.details.push_back("Commande n\xC2\xB0 " + std::to_string(l.number) + " pas encore livr\xC3\xA9" "e : " + l.problem);
    }
    return r;
}

// 1.48.0 : une commande - prise une fois, son scelle ouvert une fois, puis chacune de ses lignes a valider (les licences
// d'abord : une option << avec_ligne >> s'ajoute a la licence que sa ligne vient de livrer).
bool Workspace::deliverOne(online::Client& c, const shop::Order& ord, const shop::Settings& settings, bool automatic, int only, DeliveryReport& rep) {
    (void)settings;
    const auto skipAll = [&](const std::string& why) {
        OrderDelivery d;
        d.number = ord.number;
        d.customer = ord.company;
        d.article = articleText(ord);
        d.problem = sentence(why);
        ++rep.skipped;
        rep.lines.push_back(d);
        return false;
    };
    // 1. la prendre (un seul Admin la livre)
    if (const online::Result t = shop::takeOrder(c, ord.number); !t.ok) return skipAll(t.message);
    // 2. la demande scellee du PC (une pour tout le panier)
    if (ord.forOther || ord.sealed.empty()) return skipAll("scell\xC3\xA9" "e pour un autre admin : ce poste ne peut pas l'ouvrir");
    online::Entry req;
    std::string why;
    const bool opened = online::openSealedRequest(*signer(), ord.device, ord.keyId, ord.sealed, now(), req, &why);
    if (opened) {
        if (req.customer.empty()) req.customer = ord.company;
        if (req.email.empty()) req.email = ord.email;
    }
    std::vector<const shop::OrderLine*> todo;
    for (const auto& l : ord.lines) {
        if (only > 0 ? l.line != only : !(automatic ? l.state == "payee" : l.toValidate())) continue;
        if (only > 0 && !l.toValidate()) continue;
        todo.push_back(&l);
    }
    // L'ordre des reponses au PC (son etat << poste >> n'en garde qu'une, la derniere) : les options sur une licence qu'il a
    // deja (sans code) d'abord, puis la licence (son code), puis ses options << avec_ligne >> (qui redisent ce code) - la
    // derniere reponse porte toujours le code s'il y en a un (une seule licence par commande : le service le veut).
    const auto rank = [](const shop::OrderLine* l) { return l->kind == "edition" ? 1 : l->withLine > 0 ? 2 : 0; };
    std::stable_sort(todo.begin(), todo.end(), [&](const shop::OrderLine* a, const shop::OrderLine* b) { return rank(a) < rank(b); });
    std::map<int, std::string> licOf;
    for (const auto& l : ord.lines)
        if (l.state == "livree" && !l.licence.empty()) licOf[l.line] = l.licence;
    bool all = true;
    for (const shop::OrderLine* l : todo) {
        shop::Order lo = ord;
        lo.article = l->article;
        lo.kind = l->kind;
        lo.edition = l->edition;
        lo.option = l->option;
        lo.days = l->days;
        lo.amount = l->amount;
        lo.label = l->label;
        lo.existingLicence = l->existingLicence;
        std::string cart;
        if (l->withLine > 0) {
            const auto it = licOf.find(l->withLine);
            if (it == licOf.end()) {
                OrderDelivery d;
                d.number = ord.number;
                d.line = l->line;
                d.customer = ord.company;
                d.article = articleText(lo);
                d.problem = "L'option attend la licence de la ligne " + std::to_string(l->withLine) + " (pas encore livr\xC3\xA9" "e).";
                ++rep.skipped;
                rep.lines.push_back(d);
                all = false;
                continue;
            }
            cart = it->second;
            lo.existingLicence = cart;
        }
        if (!opened) {   // le scelle ne s'ouvre pas : chaque ligne a revoir, la raison dite
            OrderDelivery d;
            d.number = ord.number;
            d.line = l->line;
            d.customer = ord.company;
            d.article = articleText(lo);
            d.review = true;
            d.problem = sentence("la demande scell\xC3\xA9" "e de la commande ne s'ouvre pas : " + why);
            if (ord.linesKnown) (void)shop::markLine(c, ord.number, l->line, "a_revoir", d.problem);
            else (void)shop::markReview(c, ord.number, d.problem);
            ++rep.review;
            rep.lines.push_back(d);
            all = false;
            continue;
        }
        if (req.deviceKey != ord.deviceKey) {
            OrderDelivery d;
            d.number = ord.number;
            d.line = l->line;
            d.customer = ord.company;
            d.article = articleText(lo);
            d.review = true;
            d.problem = "La demande scell\xC3\xA9" "e est celle d'un autre PC que celui de la commande.";
            if (ord.linesKnown) (void)shop::markLine(c, ord.number, l->line, "a_revoir", d.problem);
            else (void)shop::markReview(c, ord.number, d.problem);
            ++rep.review;
            rep.lines.push_back(d);
            all = false;
            continue;
        }
        std::string lic;
        if (deliverLine(c, lo, l->line, req, cart, rep, lic)) licOf[l->line] = lic;
        else all = false;
    }
    return all;
}

bool Workspace::deliverLine(online::Client& c, const shop::Order& ord, int lineNo, const online::Entry& reqIn, const std::string& cartLicence, DeliveryReport& rep,
                            std::string& licenseOut) {
    const online::Options& o = c.options();
    online::Entry req = reqIn;
    OrderDelivery line;
    line.number = ord.number;
    line.line = lineNo;
    line.customer = ord.company;
    line.article = articleText(ord);
    const std::string object = "commande " + std::to_string(ord.number);
    const int startKey = ord.linesKnown ? ord.number * 100 + lineNo : ord.number;   // les livraisons commencees : par ligne
    const auto finish = [&](bool ok) {
        if (ok) ++rep.delivered;
        rep.lines.push_back(line);
        return ok;
    };
    const auto review = [&](const std::string& reasonIn) {
        const std::string reason = sentence(reasonIn);
        line.review = true;
        line.problem = reason;
        const online::Result m = ord.linesKnown ? shop::markLine(c, ord.number, lineNo, "a_revoir", reason) : shop::markReview(c, ord.number, reason);
        if (!m.ok) line.problem += " (et la commande n'est pas marqu\xC3\xA9" "e \xC2\xAB \xC3\xA0 revoir \xC2\xBB : " + m.message + ")";
        Value d = Value::object();
        d.set("raison", reason.substr(0, 300));
        d.set("client", ord.company);
        (void)journal("commande_revoir", object, "n'a pas pu livrer la commande n\xC2\xB0 " + std::to_string(ord.number) + " : \xC3\xA0 revoir", core::json::write(d, false));
        ++rep.review;
        return finish(false);
    };
    const auto later = [&](const std::string& why) {   // une panne qui passera : la commande reste payee
        line.problem = sentence(why);
        ++rep.skipped;
        return finish(false);
    };
    std::string why;
    auto started = readStarted(folder_);
    const auto was = started.find(startKey);
    std::string licenseId;
    if (ord.kind == "edition") {
        const licensing::EditionDef* ed = hasRegistry_ ? registry_.edition(ord.edition) : nullptr;
        if (!ed) return review("l'offre \xC2\xAB " + ord.edition + " \xC2\xBB n'est pas dans le registre sign\xC3\xA9 de ce poste");
        const std::string edLabel = ed->label.empty() ? ed->id : ed->label;   // (la signature relit le registre)
        std::string code;
        ServiceJob job;
        if (was != started.end() && licence(was->second.licence)) {
            // une reprise : la MEME licence (signee pour cette commande), son code garde chiffre sur ce poste
            licenseId = was->second.licence;
            if (!online::readCode(folder_, licenseId, code, &why))
                return review("la licence " + licenseId + " sign\xC3\xA9" "e pour cette commande n'a plus son code sur ce poste (" + why + ")");
            if (!readFileText(licence(licenseId)->path, job.licenceText, &why)) return later(why);
            std::string normalized;
            (void)licensing::service::normalizeCode(code, normalized);
            job.kind = ServiceJob::Kind::OnlineLicence;
            job.licenseId = licenseId;
            job.codeHash = licensing::service::codeHash(normalized);
            job.leaseDays = online::kDefaultLeaseDays;
            wipe(normalized);
        } else {
            LicenceDraft d;
            d.online = true;
            d.licenseId = suggestLicenseId();
            d.customer = ord.company;
            d.edition = ord.edition;
            d.leaseDays = online::kDefaultLeaseDays;
            const OpResult p = prepareOnlineLicence(d, onlineLicencePath(d.licenseId), job, code, 0);
            if (!p.ok) {
                wipe(code);
                return review("la licence ne se signe pas : " + p.message);
            }
            licenseId = d.licenseId;
            std::string w;
            (void)online::storeCode(folder_, licenseId, code, &w);   // avant le depot : une reprise la retrouve
            started[startKey] = {licenseId, "edition"};
            writeStarted(folder_, started);
        }
        const OpResult dep = serviceRunOnline(job, code);
        if (!dep.ok) {
            wipe(code);
            return later("la licence " + licenseId + " n'est pas d\xC3\xA9pos\xC3\xA9" "e : " + dep.message);
        }
        const online::Result a = online::answerOrder(folder_, *signer(), req, licenseId, code, job.licenceText,
                                                     "Ta licence " + licenseId + " (" + edLabel + ", commande n\xC2\xB0 " + std::to_string(ord.number) +
                                                         ") est pr\xC3\xAAte : ton Studio l'active tout seul.",
                                                     mailer(), o);
        wipe(code);
        if (!a.ok) return later("la licence " + licenseId + " est d\xC3\xA9pos\xC3\xA9" "e, mais son code n'est pas parti au PC : " + a.message);
    } else {
        const LicenceEntry* L = licence(ord.existingLicence);
        if (!L) return review("la licence \xC2\xAB " + ord.existingLicence + " \xC2\xBB de la commande n'est pas dans l'archive de ce poste");
        licenseId = L->licence.licenseId;
        const licensing::OptionDef* opt = hasRegistry_ ? registry_.option(ord.option) : nullptr;
        if (!opt) return review("l'option \xC2\xAB " + ord.option + " \xC2\xBB n'est pas dans le registre sign\xC3\xA9 de ce poste");
        const std::string optLabel = opt->label.empty() ? opt->id : opt->label;   // (la signature relit le registre)
        const bool onlineLic = isOnlineLicence(L->licence);
        const bool inForceNow = inForce(*L);
        const std::string stateNow = L->stateLabel();
        const Seconds issued = L->licence.issuedAt;
        // la licence est-elle bien celle de ce PC ? (1.48.0 : la licence d'une ligne de la meme commande : oui, elle vient d'etre
        // livree pour lui - il ne l'a peut-etre pas encore activee)
        if (!cartLicence.empty()) {
        } else if (onlineLic) {
            online::Snapshot snap;
            if (const OpResult ls = serviceListLicences(snap); !ls.ok) return later(ls.message);
            const online::OnlineLicence* ol = snap.licence(licenseId);
            if (!ol || !ol->activation(ord.deviceKey)) return review("la licence " + licenseId + " n'est pas activ\xC3\xA9" "e sur le PC de la commande");
        } else if (std::none_of(L->licence.devices.begin(), L->licence.devices.end(), [&](const licensing::BoundDevice& b) { return b.key == ord.deviceKey; })) {
            return review("la licence " + licenseId + " n'est pas li\xC3\xA9" "e au PC de la commande");
        }
        if (was == started.end()) {
            if (!inForceNow) return review("la licence " + licenseId + " est " + stateNow + " : une option s'ajoute \xC3\xA0 une licence en cours");
            // Une emission dans la meme seconde que la precedente n'entre pas dans l'archive : attendre la seconde suivante.
            for (int i = 0; i < 25 && now() <= issued; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            OptionDraft od;
            od.optionId = ord.option;
            od.days = ord.days;
            od.price = shop::amountText(ord.amount, ord.currency) + " TTC";
            const OpResult a = addOption(licenseId, od, onlineLicencePath(licenseId));
            if (!a.ok) return review("l'option ne s'ajoute pas : " + a.message);
            started[startKey] = {licenseId, "option"};
            writeStarted(folder_, started);
        }
        std::optional<ServiceJob> job = takeServiceJob();
        if (!job) {   // une reprise : la derniere emission de l'archive, de nouveau
            const LicenceEntry* now2 = licence(licenseId);
            ServiceJob j;
            j.kind = onlineLic ? ServiceJob::Kind::OnlineLicence : ServiceJob::Kind::Licence;
            j.licenseId = licenseId;
            if (!now2 || !readFileText(now2->path, j.licenceText, &why)) return later("la licence " + licenseId + " ne se relit pas");
            job = std::move(j);
        }
        const OpResult dep = serviceRun(*job);
        if (!dep.ok) return later("l'option de la licence " + licenseId + " n'est pas d\xC3\xA9pos\xC3\xA9" "e : " + dep.message);
        if (onlineLic && !cartLicence.empty()) {
            // 1.48.0 : la licence de la meme commande - son code de nouveau (garde chiffre ici) et la licence avec l'option
            std::string code, text;
            if (!online::readCode(folder_, licenseId, code, &why)) return review("la licence " + licenseId + " n'a plus son code sur ce poste (" + why + ")");
            const LicenceEntry* now3 = licence(licenseId);
            if (!now3 || !readFileText(now3->path, text, &why)) {
                wipe(code);
                return later("la licence " + licenseId + " ne se relit pas");
            }
            const online::Result a = online::answerOrder(folder_, *signer(), req, licenseId, code, text,
                                                         "Option \xC2\xAB " + optLabel + " \xC2\xBB ajout\xC3\xA9" "e \xC3\xA0 ta licence " + licenseId +
                                                             " (commande n\xC2\xB0 " + std::to_string(ord.number) + ") : ton Studio l'active tout seul.",
                                                         {}, o);
            wipe(code);
            if (!a.ok) return later("l'option est d\xC3\xA9pos\xC3\xA9" "e, mais la r\xC3\xA9ponse n'est pas partie au PC : " + a.message);
        } else if (onlineLic) {
            const online::Result a = online::answerOrder(folder_, *signer(), req, licenseId, {}, {},
                                                         "Option \xC2\xAB " + optLabel + " \xC2\xBB ajout\xC3\xA9" "e \xC3\xA0 ta licence " +
                                                             licenseId + " (commande n\xC2\xB0 " + std::to_string(ord.number) + ") : ton Studio la re\xC3\xA7oit tout de suite.",
                                                         {}, o);
            if (!a.ok) return later("l'option est d\xC3\xA9pos\xC3\xA9" "e, mais la r\xC3\xA9ponse n'est pas partie au PC : " + a.message);
        }
    }
    // 3. le journal, puis << livree >> (une seule fois, meme si deux Admin sont ouverts)
    {
        Value d = Value::object();
        d.set("client", ord.company);
        d.set("article", articleText(ord));
        d.set("montant", shop::amountText(ord.amount, ord.currency));
        d.set("licence", licenseId);
        d.set("mode", ord.mode);
        (void)journal("commande_livrer", object, "commande n\xC2\xB0 " + std::to_string(ord.number) + " livr\xC3\xA9" "e (licence " + licenseId + ", " + ord.company + ")",
                      core::json::write(d, false));
    }
    std::string already;
    const online::Result d = ord.linesKnown ? shop::markLine(c, ord.number, lineNo, "livree", licenseId, &already) : shop::markDelivered(c, ord.number, licenseId, &already);
    if (!d.ok && already != licenseId) {
        line.problem = "ATTENTION : la licence " + licenseId + " est partie, mais " + d.message;
        ++rep.skipped;
        return finish(false);
    }
    started = readStarted(folder_);
    started.erase(startKey);
    writeStarted(folder_, started);
    line.delivered = true;
    line.licence = licenseId;
    licenseOut = licenseId;
    return finish(true);
}

} // namespace license_admin::gui
