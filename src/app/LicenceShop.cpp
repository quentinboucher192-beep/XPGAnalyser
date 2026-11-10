// =============================================================================
//  app/LicenceShop.cpp - 1.46.0 : la boutique vue du Studio (voir LicenceShop.hpp)
// =============================================================================
#include "LicenceShop.hpp"

#include "../core/Json.hpp"
#include "../deploy/Crypto.hpp"
#include "../deploy/Paths.hpp"
#include "../licensing/Admins.hpp"   // parseSemver, compareSemver
#include "../licensing/Licensing.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>

namespace app::licence::shop {

namespace {
namespace json = core::json;
namespace lic = licensing;

bool idChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'; }

// Une ligne propre (pas de caractere de controle), 200 octets au plus.
std::string clean(std::string s, std::size_t max = 200) {
    std::string out;
    for (const char c : s)
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
    if (out.size() > max) {
        out.resize(max);
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0xC0) out.pop_back();
    }
    return out;
}

// Une adresse https (la page de paiement, le recu) ; sinon vide.
std::string webUrl(const std::string& s) {
    if (s.rfind("https://", 0) != 0 || s.size() > 4000) return {};
    for (const char c : s)
        if (static_cast<unsigned char>(c) <= 0x20 || static_cast<unsigned char>(c) >= 0x7F) return {};
    return s;
}

bool readArticle(const json::Value& a, Article& out) {
    if (!a.isObject()) return false;
    Article x;
    x.id = a.str("id");
    x.kind = a.str("genre");
    x.amount = a.integer("montant");
    x.label = clean(a.str("libelle"), 240);
    if (x.id.empty() || x.id.size() > 120 || !std::all_of(x.id.begin(), x.id.end(), [](char c) { return idChar(c) || c == ':'; })) return false;
    if (x.amount < 50 || x.amount > 100000000) return false;
    if (x.kind == "edition") {
        x.edition = a.str("edition");
        if (x.edition.empty() || x.id != "ed:" + x.edition) return false;
    } else if (x.kind == "option") {
        x.option = a.str("option");
        const json::Value* d = a.find("jours");
        if (!d || !d->isNumber()) return false;
        x.days = static_cast<int>(d->asInt(-1));
        if (x.option.empty() || x.days < 0 || x.days > 3660 || x.id != "opt:" + x.option + ":" + std::to_string(x.days)) return false;
    } else {
        return false;
    }
    if (x.label.empty()) x.label = x.id;
    out = std::move(x);
    return true;
}

lic::Seconds secondsOf(const json::Value& v, std::string_view key) {
    const json::Value* x = v.find(key);
    if (!x || !x->isNumber()) return 0;
    const double d = x->asNumber(0);
    return d > 0 && d < 1e11 ? static_cast<lic::Seconds>(d) : lic::Seconds{0};
}

struct Cache {
    std::mutex         mutex;
    bool               loaded{false};
    std::string        path;      // le fichier relu (le dossier des licences peut changer : les essais)
    Catalogue          value;
    unsigned long long revision{0};
};
Cache& cache() {
    static Cache c;
    return c;
}

} // namespace

bool serviceKnowsShop(std::string_view version) {
    int a = 0, b = 0, c = 0;
    return licensing::admins::parseSemver(version, a, b, c) && licensing::admins::compareSemver(version, kServiceRequired) >= 0;
}

// ============================================================ le catalogue ==
const Article* Catalogue::find(std::string_view id) const noexcept {
    for (const auto& a : articles)
        if (a.id == id) return &a;
    return nullptr;
}

const Article* Catalogue::edition(std::string_view editionId) const noexcept {
    if (!selling()) return nullptr;
    for (const auto& a : articles)
        if (a.kind == "edition" && a.edition == editionId) return &a;
    return nullptr;
}

const Article* Catalogue::option(std::string_view optionId, int d) const noexcept {
    if (!selling()) return nullptr;
    for (const auto& a : articles)
        if (a.kind == "option" && a.option == optionId && a.days == d) return &a;
    return nullptr;
}

std::vector<const Article*> Catalogue::options(std::string_view optionId) const {
    std::vector<const Article*> out;
    if (!selling()) return out;
    for (const auto& a : articles)
        if (a.kind == "option" && a.option == optionId) out.push_back(&a);
    return out;
}

bool parseCatalogue(std::string_view text, Catalogue& out, std::string* why) {
    json::Value v;
    std::string w;
    if (!json::parse(text, v, &w) || !v.isObject()) {
        if (why) *why = "le catalogue de la boutique est illisible";
        return false;
    }
    Catalogue c;
    c.known = true;
    c.open = v.flag("active");
    c.currency = v.str("devise", "eur");
    if (c.currency.size() != 3 || !std::all_of(c.currency.begin(), c.currency.end(), [](char ch) { return ch >= 'a' && ch <= 'z'; })) c.currency = "eur";
    c.mode = v.str("mode");
    if (c.mode != "test" && c.mode != "reel") c.mode.clear();
    if (const json::Value* list = v.find("articles"); list && list->isArray())
        for (const auto& a : list->items()) {
            Article x;
            if (readArticle(a, x) && !c.find(x.id)) c.articles.push_back(std::move(x));
            if (c.articles.size() >= 200) break;
        }
    if (!c.open) c.articles.clear();
    out = std::move(c);
    return true;
}

std::string writeCatalogue(const Catalogue& c) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("active", c.open);
    v.set("devise", c.currency);
    if (!c.mode.empty()) v.set("mode", c.mode);
    if (c.receivedAt) v.set("recu", static_cast<std::int64_t>(c.receivedAt));
    if (!c.serviceVersion.empty()) v.set("service", c.serviceVersion);
    json::Value list = json::Value::array();
    for (const auto& a : c.articles) {
        json::Value o = json::Value::object();
        o.set("id", a.id);
        o.set("genre", a.kind);
        if (a.kind == "edition") o.set("edition", a.edition);
        else {
            o.set("option", a.option);
            o.set("jours", a.days);
        }
        o.set("montant", a.amount);
        o.set("libelle", a.label);
        list.push(std::move(o));
    }
    v.set("articles", std::move(list));
    return json::write(v) + "\n";
}

bool readCatalogue(std::string_view text, Catalogue& out) {
    if (!parseCatalogue(text, out, nullptr)) return false;
    json::Value v;
    if (json::parse(text, v) && v.isObject()) {
        out.receivedAt = static_cast<lic::Seconds>(std::max<std::int64_t>(0, v.integer("recu")));
        out.serviceVersion = v.str("service");
    }
    return true;
}

std::string cachePath() { return deploy::joinPath(lic::getLicenseFolder(), "boutique.json"); }

Catalogue catalogue() {
    Cache& k = cache();
    const std::string path = cachePath();
    std::lock_guard<std::mutex> lock(k.mutex);
    if (!k.loaded || k.path != path) {
        k.loaded = true;
        k.path = path;
        k.value = Catalogue{};
        deploy::crypto::Bytes bytes;
        std::error_code ec;
        if (!path.empty() && std::filesystem::exists(std::filesystem::path(std::u8string(path.begin(), path.end())), ec) &&
            deploy::crypto::readFile(path, bytes, nullptr, 512u * 1024u)) {
            Catalogue c;
            if (readCatalogue(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), c)) k.value = std::move(c);
        }
        ++k.revision;
    }
    return k.value;
}

void setCatalogue(const Catalogue& c) {
    Cache& k = cache();
    const std::string path = cachePath();
    {
        std::lock_guard<std::mutex> lock(k.mutex);
        const bool same = k.loaded && k.path == path && writeCatalogue(k.value) == writeCatalogue(c);
        k.loaded = true;
        k.path = path;
        k.value = c;
        if (!same) ++k.revision;
    }
    std::string w;
    if (!path.empty()) (void)deploy::crypto::replaceFile(path, writeCatalogue(c), false, &w);   // un dossier ou ce compte n'ecrit pas : la memoire suffit
}

void resetCatalogueForTests() {
    Cache& k = cache();
    std::lock_guard<std::mutex> lock(k.mutex);
    k.loaded = false;
    k.value = Catalogue{};
    ++k.revision;
}

unsigned long long catalogueRevision() noexcept {
    Cache& k = cache();
    std::lock_guard<std::mutex> lock(k.mutex);
    return k.revision;
}

// ============================================================ les textes ==
std::string amountText(std::int64_t cents, const std::string& currency) {
    const bool neg = cents < 0;
    const std::uint64_t c = static_cast<std::uint64_t>(neg ? -cents : cents);
    const std::string units = std::to_string(c / 100);
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

std::string amountTtc(std::int64_t cents, const std::string& currency) { return amountText(cents, currency) + " TTC"; }

std::string buyLabel(const Article& a, const std::string& currency) { return "Acheter \xE2\x80\x94 " + amountTtc(a.amount, currency); }

std::string testModeText() { return "Paiement d'essai : aucune carte r\xC3\xA9" "elle n'est d\xC3\xA9" "bit\xC3\xA9" "e"; }

std::string cardSentence() {
    return "Tu paies sur la page s\xC3\xA9" "curis\xC3\xA9" "e de Stripe : ta carte n'est ni vue ni gard\xC3\xA9" "e par XPG ; elle est redemand\xC3\xA9" "e \xC3\xA0 chaque achat.";
}

// ============================================================ les commandes ==
bool parseOrders(std::string_view text, std::vector<Order>& out, std::string* why) {
    json::Value v;
    if (!json::parse(text, v) || !v.isObject()) {
        if (why) *why = "la liste des commandes est illisible";
        return false;
    }
    const json::Value* list = v.find("commandes");
    if (!list || !list->isArray()) {
        if (why) *why = "la liste des commandes est illisible (pas de \xC2\xAB commandes \xC2\xBB)";
        return false;
    }
    std::vector<Order> got;
    for (const auto& c : list->items()) {
        if (!c.isObject()) continue;
        Order o;
        const double n = c.num("numero");
        if (n <= 0 || n > 9e15) continue;
        o.number = static_cast<std::uint64_t>(n);
        o.article = clean(c.str("article"), 120);
        o.label = clean(c.str("libelle"), 240);
        o.amount = c.integer("montant");
        o.currency = c.str("devise", "eur");
        if (o.currency.size() != 3) o.currency = "eur";
        o.state = clean(c.str("etat"), 40);
        o.mode = c.str("mode");
        if (o.mode != "test" && o.mode != "reel") o.mode.clear();
        o.created = secondsOf(c, "cree");
        o.updated = secondsOf(c, "maj");
        o.expires = secondsOf(c, "expire");
        o.licence = clean(c.str("licence"), 64);
        o.receiptUrl = webUrl(c.str("recu_url"));
        o.payUrl = webUrl(c.str("url"));
        if (o.label.empty()) o.label = o.article;
        got.push_back(std::move(o));
        if (got.size() >= kMaxOrders) break;
    }
    out = std::move(got);
    return true;
}

const Order* findOrder(const std::vector<Order>& list, std::uint64_t number) noexcept {
    for (const auto& o : list)
        if (o.number == number) return &o;
    return nullptr;
}

Order* findOrder(std::vector<Order>& list, std::uint64_t number) noexcept {
    for (auto& o : list)
        if (o.number == number) return &o;
    return nullptr;
}

bool mergeOrders(std::vector<Order>& mine, const std::vector<Order>& fresh) {
    bool changed = false;
    for (const auto& f : fresh) {
        Order* m = findOrder(mine, f.number);
        if (!m) {
            mine.push_back(f);
            changed = true;
            continue;
        }
        // ce que le service dit remplace ; ce que seul ce PC sait reste
        Order next = f;
        next.nonce = m->nonce;
        next.option = m->option;
        next.optionText = m->optionText;
        next.applied = m->applied;
        if (next.label.empty()) next.label = m->label;
        const bool differs = next.state != m->state || next.updated != m->updated || next.receiptUrl != m->receiptUrl || next.licence != m->licence ||
                             next.payUrl != m->payUrl || next.expires != m->expires || next.label != m->label || next.amount != m->amount;
        if (differs) {
            *m = std::move(next);
            changed = true;
        }
    }
    std::stable_sort(mine.begin(), mine.end(), [](const Order& a, const Order& b) { return a.number > b.number; });
    if (mine.size() > kMaxOrders) {
        mine.resize(kMaxOrders);
        changed = true;
    }
    return changed;
}

bool closed(const std::string& s) noexcept {
    return s == kDelivered || s == kExpired || s == kCancelled || s == kFailed || s == kRefunded;
}

bool open(const std::string& s) noexcept { return s == kToPay || s == kPaid || s == kReview; }

std::string stateLabel(const std::string& s) {
    if (s == kToPay) return "en attente du paiement";
    if (s == kPaid) return "pay\xC3\xA9" "e";
    if (s == kDelivered) return "livr\xC3\xA9" "e";
    if (s == kExpired) return "expir\xC3\xA9" "e";
    if (s == kCancelled) return "annul\xC3\xA9" "e";
    if (s == kFailed) return "paiement refus\xC3\xA9";
    if (s == kRefunded) return "rembours\xC3\xA9" "e";
    if (s == kReview) return "en revue chez l'\xC3\xA9" "diteur";
    return s.empty() ? std::string("inconnu") : s;
}

int stateTone(const std::string& s) noexcept {
    if (s == kDelivered) return 1;
    if (s == kToPay || s == kPaid || s == kReview) return 2;
    if (s == kFailed) return 3;
    return 0;
}

std::string stateText(const Order& o, lic::Seconds now) {
    const bool option = o.article.rfind("opt:", 0) == 0;
    if (o.state == kToPay) {
        std::string t = "Paiement dans ton navigateur\xE2\x80\xA6 La page s\xC3\xA9" "curis\xC3\xA9" "e de Stripe est ouverte : paie-y ta commande (ta carte n'est ni vue ni gard\xC3\xA9" "e "
                        "par XPG).";
        if (o.expires > now && now > 0) {
            const long long min = (o.expires - now + 59) / 60;
            t += " Elle reste ouverte encore " + std::to_string(min) + " min.";
        }
        t += " D\xC3\xA8s que tu as pay\xC3\xA9, cette fen\xC3\xAAtre le voit toute seule. Ferm\xC3\xA9" "e par erreur ? Rouvrir la page de paiement.";
        return t;
    }
    if (o.state == kPaid)
        return option ? std::string("Paiement re\xC3\xA7u : ton option arrive d\xC3\xA8s que l'\xC3\xA9" "diteur la d\xC3\xA9livre. Le Studio regarde tout seul (toutes les 5 minutes) : "
                                    "tu peux fermer cette fen\xC3\xAAtre, et m\xC3\xAAme le Studio.")
                      : std::string("Paiement re\xC3\xA7u : ta licence arrive d\xC3\xA8s que l'\xC3\xA9" "diteur la d\xC3\xA9livre. Le Studio regarde tout seul (toutes les 5 minutes) "
                                    "et l'active d\xC3\xA8s qu'elle arrive : tu peux fermer cette fen\xC3\xAAtre, et m\xC3\xAAme le Studio.");
    if (o.state == kDelivered) {
        if (option)
            return "Commande livr\xC3\xA9" "e : " + (o.optionText.empty() ? std::string("ton option") : "\xC2\xAB " + o.optionText + " \xC2\xBB") + " est sur ce PC" +
                   (o.licence.empty() ? std::string(".") : " (licence " + o.licence + ").");
        return "Commande livr\xC3\xA9" "e : ta licence" + (o.licence.empty() ? std::string() : " " + o.licence) + " est activ\xC3\xA9" "e sur ce PC, tu peux travailler.";
    }
    if (o.state == kExpired) return "La page de paiement a expir\xC3\xA9 sans paiement : rien n'a \xC3\xA9t\xC3\xA9 d\xC3\xA9" "bit\xC3\xA9. Tu peux acheter de nouveau.";
    if (o.state == kCancelled) return "Commande annul\xC3\xA9" "e : rien n'a \xC3\xA9t\xC3\xA9 d\xC3\xA9" "bit\xC3\xA9.";
    if (o.state == kFailed) return "Ta banque a refus\xC3\xA9 le paiement : rien n'a \xC3\xA9t\xC3\xA9 d\xC3\xA9" "bit\xC3\xA9. Tu peux acheter de nouveau (avec une autre carte).";
    if (o.state == kRefunded) return "Commande rembours\xC3\xA9" "e : Stripe a rendu le montant sur ta carte.";
    if (o.state == kReview) return "Paiement re\xC3\xA7u, mais l'\xC3\xA9" "diteur doit revoir la livraison avant de te d\xC3\xA9livrer ta licence : il te contacte si besoin.";
    return "\xC3\x89tat de la commande : " + stateLabel(o.state) + ".";
}

const Order* waitingOrder(const std::vector<Order>& list) noexcept {
    const Order* best = nullptr;
    const auto rank = [](const std::string& s) { return s == kToPay ? 3 : s == kPaid ? 2 : s == kReview ? 1 : 0; };
    for (const auto& o : list) {
        const int r = rank(o.state);
        if (r == 0) continue;
        if (!best || r > rank(best->state) || (r == rank(best->state) && o.number > best->number)) best = &o;
    }
    return best;
}

// ============================================================ 1.48.0 : le panier ==
namespace {
std::atomic<unsigned long long> g_cartRev{1};
}

std::int64_t Cart::total() const noexcept {
    std::int64_t t = 0;
    for (const auto& l : lines)
        if (!l.request) t += l.amount;
    return t;
}
std::size_t Cart::toPay() const noexcept {
    std::size_t n = 0;
    for (const auto& l : lines) n += l.request ? 0 : 1;
    return n;
}
std::size_t Cart::requests() const noexcept { return lines.size() - toPay(); }
bool Cart::hasOffer() const noexcept {
    for (const auto& l : lines)
        if (l.kind == "edition") return true;
    return false;
}

std::string cartPath() { return deploy::joinPath(lic::getLicenseFolder(), "panier.json"); }

Cart loadCart() {
    Cart c;
    const std::string path = cartPath();
    deploy::crypto::Bytes bytes;
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(std::filesystem::path(std::u8string(path.begin(), path.end())), ec) ||
        !deploy::crypto::readFile(path, bytes, nullptr, 256u * 1024u))
        return c;
    json::Value v;
    if (!json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), v) || !v.isObject()) return c;
    c.currency = v.str("devise", "eur");
    if (const json::Value* list = v.find("lignes"); list && list->isArray())
        for (const auto& x : list->items()) {
            if (!x.isObject()) continue;
            CartLine l;
            l.article = clean(x.str("article"), 120);
            l.kind = x.str("genre");
            l.edition = clean(x.str("edition"), 60);
            l.option = clean(x.str("option"), 60);
            l.days = static_cast<int>(x.integer("jours"));
            l.label = clean(x.str("libelle"), 200);
            l.gives = clean(x.str("donne"), 300);
            l.amount = std::max<std::int64_t>(0, x.integer("montant"));
            l.request = x.flag("demande");
            l.licence = clean(x.str("licence"), 64);
            if (l.article.empty() || (l.kind != "edition" && l.kind != "option")) continue;
            c.lines.push_back(std::move(l));
            if (c.lines.size() >= 50) break;
        }
    return c;
}

bool saveCart(const Cart& c, std::string* why) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("devise", c.currency);
    json::Value list = json::Value::array();
    for (const auto& l : c.lines) {
        json::Value x = json::Value::object();
        x.set("article", l.article);
        x.set("genre", l.kind);
        if (!l.edition.empty()) x.set("edition", l.edition);
        if (!l.option.empty()) {
            x.set("option", l.option);
            x.set("jours", l.days);
        }
        x.set("libelle", l.label);
        if (!l.gives.empty()) x.set("donne", l.gives);
        x.set("montant", l.amount);
        if (l.request) x.set("demande", true);
        if (!l.licence.empty()) x.set("licence", l.licence);
        list.push(std::move(x));
    }
    v.set("lignes", std::move(list));
    std::string w;
    if (!deploy::crypto::replaceFile(cartPath(), json::write(v) + "\n", false, &w)) {
        if (why) *why = "Le panier n'a pas pu \xC3\xAAtre \xC3\xA9" "crit : " + w;
        return false;
    }
    ++g_cartRev;
    return true;
}

bool addToCart(Cart& c, CartLine line, const std::string& currentLicence, std::string* why) {
    for (const auto& l : c.lines)
        if (l.article == line.article && l.request == line.request) {
            if (why) *why = "\xC2\xAB " + line.label + " \xC2\xBB est d\xC3\xA9j\xC3\xA0 dans le panier.";
            return false;
        }
    if (line.kind == "option" && line.licence.empty()) {
        if (!currentLicence.empty()) line.licence = currentLicence;
        else if (!c.hasOffer()) {
            if (why) *why = "Une option s'ajoute \xC3\xA0 une licence : active ta licence, ou mets d'abord une offre dans le panier.";
            return false;
        }
    }
    if (c.lines.size() >= 50) {
        if (why) *why = "Le panier est plein (50 lignes).";
        return false;
    }
    c.lines.push_back(std::move(line));
    return true;
}

unsigned long long cartRevision() noexcept { return g_cartRev.load(); }

} // namespace app::licence::shop
