// =============================================================================
//  license_admin/gui/WorkspaceDerogation.cpp - 1.47.0 : GERER LE SERVICE
//  (l'admin+ seul ; la derogation courte d'un admin) ET LA CONNEXION AU SERVICE
//  QUI VOYAGE AVEC L'INSTALLATEUR (docs/ADMINS.md, par. 11)
// -----------------------------------------------------------------------------
//  La derogation : Derogation.hpp (ce que voit l'Admin), service/src/derogations.js
//  (l'arbitre, a son heure). Le document, signe par la cle de l'editeur :
//  "XPG-DEROGATION-1\n" + {"format":1,"id","droit":"service","admin","nom","debut",
//  "fin","emise"} - debut : l'heure du service, fin : debut + 1, 4, 24 ou 72 h.
//  La connexion : ServiceConnection.hpp ; ici, la recherche au chargement (aucune
//  adresse reglee, pas l'admin+), << Utiliser une connexion... >>, << Exporter la
//  connexion... >>.
// =============================================================================
#include "Workspace.hpp"

#include "../../deploy/Crypto.hpp"
#include "../../deploy/Paths.hpp"
#include "../../licensing/Service.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace {

namespace fs = std::filesystem;
namespace crypto = deploy::crypto;
namespace adm = licensing::admins;
using core::json::Value;

constexpr const char* kPrefix = "XPG-DEROGATION-1\n";
constexpr const char* kServicePlusOnly = "L'admin+ seul g\xC3\xA8re le service (d\xC3\xA9rogation possible).";

std::string two(long v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02ld", v);
    return b;
}

std::string hourMinute(Seconds t) {
    const std::string s = dateTimeFr(t);   // "10/11/2026 15:42"
    const auto sp = s.find(' ');
    return sp == std::string::npos ? s : s.substr(sp + 1);
}

// Un service qui ne connait pas encore la derogation (404 route) : la phrase << deploie la 1.47.0 >>.
std::string serviceTooOld(const online::Reply& r) {
    if (r.error.empty() && r.status == 404 && r.code() == "route")
        return "ton service en ligne ne conna\xC3\xAEt pas encore la d\xC3\xA9rogation : l'admin+ doit d\xC3\xA9ployer la 1.47.0 (Service en ligne \xE2\x80\xBA D\xC3\xA9ployer le service)";
    return {};
}

// La vue JSON du service -> Derogation (sans la verification du document).
Derogation fromJson(const Value& d, Seconds serverNow, Seconds localNow) {
    Derogation out;
    out.known = true;
    out.serverNow = serverNow;
    out.readAt = localNow;
    if (!d.isObject()) return out;
    out.id = d.str("id").substr(0, 32);
    out.adminKey = d.str("admin");
    out.adminName = d.str("nom").substr(0, 120);
    out.by = d.str("par");
    out.start = static_cast<Seconds>(d.integer("debut"));
    out.end = static_cast<Seconds>(d.integer("fin"));
    out.revoked = static_cast<Seconds>(d.integer("revoquee"));
    out.state = d.str("etat").substr(0, 20);
    out.uses = static_cast<int>(d.integer("usages"));
    return out;
}

// Le document signe par une cle de l'editeur que ce poste croit, et le meme que la vue.
bool documentOk(const Value& d, const std::vector<licensing::crypto::PublicKey>& editors, const Derogation& view) {
    const std::string text = d.str("document"), sig = d.str("signature");
    crypto::Bytes raw;
    if (text.empty() || !crypto::unhex(sig, raw, 64)) return false;
    licensing::crypto::Signature s{};
    std::copy(raw.begin(), raw.end(), s.begin());
    bool signedOk = false;
    for (const auto& k : editors)
        if (crypto::hex(k) == view.by && crypto::verify(k, std::string(kPrefix) + text, s)) signedOk = true;
    if (!signedOk) return false;
    Value doc;
    if (!core::json::parse(text, doc, nullptr) || !doc.isObject()) return false;
    return doc.str("id") == view.id && doc.str("admin") == view.adminKey && doc.str("droit") == "service" &&
           static_cast<Seconds>(doc.integer("fin")) == view.end && static_cast<Seconds>(doc.integer("debut")) == view.start;
}

std::string P8(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

} // namespace

// ============================================================================================ Derogation.hpp
std::string remainingText(Seconds s) {
    if (s <= 0) return "0 min";
    if (s < 60) return std::to_string(s) + " s";
    if (s < 3600) return std::to_string((s + 59) / 60) + " min";
    const long h = static_cast<long>(s / 3600), m = static_cast<long>((s % 3600) / 60);
    return std::to_string(h) + " h " + two(m);
}

std::string derogationText(const Derogation& d, Seconds localNow) {
    if (!d.exists()) return "aucune";
    if (d.revoked || d.state == "revoquee") return "r\xC3\xA9voqu\xC3\xA9" "e par l'admin+" + (d.revoked ? " le " + dateTimeFr(d.revoked) : std::string());
    if (d.activeAt(localNow)) {
        const Seconds left = d.remaining(localNow);
        // l'heure de la fin, en heure de ce PC : le service l'a donnee a son heure (l'ecart corrige)
        const Seconds endLocal = d.end - (d.serviceNow(localNow) - localNow);
        return "il reste " + remainingText(left) + " (jusqu'\xC3\xA0 " + (left > 20 * 3600 ? dateTimeFr(endLocal) : hourMinute(endLocal)) + ")";
    }
    if (d.serviceNow(localNow) < d.start) return "pas encore commenc\xC3\xA9" "e";
    return "termin\xC3\xA9" "e le " + dateTimeFr(d.end - (d.serviceNow(localNow) - localNow));
}

// ============================================================================================ gerer le service
bool Workspace::mayManageService() const {
    if (role_ == Role::Plus) return true;
    if (role_ != Role::Admin) return false;
    return derogation_.forKey(adminKey_.keyHex) && derogation_.activeAt(now());
}

std::string Workspace::whyNotService() const {
    if (mayManageService()) return {};
    if (role_ == Role::Admin && derogation_.forKey(adminKey_.keyHex) && derogation_.exists()) {
        if (derogation_.revoked || derogation_.state == "revoquee")
            return "Ta d\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB a \xC3\xA9t\xC3\xA9 r\xC3\xA9voqu\xC3\xA9" "e : l'admin+ seul g\xC3\xA8re le service.";
        return "Ta d\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB est termin\xC3\xA9" "e : l'admin+ seul g\xC3\xA8re le service (demande-lui une nouvelle d\xC3\xA9rogation).";
    }
    return kServicePlusOnly;
}

void Workspace::adoptDerogation(const Derogation& d) { derogation_ = d; }

void Workspace::adoptDerogation(const online::AdminWatch& w) {
    if (!w.derogationSaid) return;   // un service d'avant la 1.47.0 : rien a dire
    Derogation d;
    d.known = true;
    d.serverNow = static_cast<Seconds>(w.serverTime);
    d.readAt = now();
    d.id = w.derogationId;
    d.adminKey = w.derogationAdmin;
    d.adminName = w.derogationName;
    d.by = w.derogationBy;
    d.start = static_cast<Seconds>(w.derogationStart);
    d.end = static_cast<Seconds>(w.derogationEnd);
    d.revoked = static_cast<Seconds>(w.derogationRevoked);
    d.state = w.derogationState;
    d.uses = static_cast<int>(w.derogationUses);
    // la verification du document se garde d'une lecture complete de la meme derogation
    d.signatureOk = derogation_.id == d.id && derogation_.signatureOk;
    derogation_ = d;
}

OpResult Workspace::grantDerogation(const std::string& adminKeyHex, int hours, Derogation* out) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (detail::callerDenied(caller, {})) return OpResult::fail("seul l'admin+ accorde une d\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB");
    if (std::find(std::begin(kDerogationHours), std::end(kDerogationHours), hours) == std::end(kDerogationHours))
        return OpResult::fail("une d\xC3\xA9rogation dure 1 h, 4 h, 24 h ou 72 h : jamais plus, jamais \xC2\xAB permanente \xC2\xBB");
    const adm::Admin* a = caller.admins ? caller.admins->byKey(adminKeyHex) : nullptr;
    if (!a) return OpResult::fail("cet admin n'est pas dans la liste des admins en place");
    if (!a->active()) return OpResult::fail(a->name + " est retir\xC3\xA9 : il ne peut rien recevoir");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    // L'heure du service (le debut doit etre la sienne, a 10 minutes pres).
    const Seconds start = c->serviceNow();
    unsigned char rnd[16];
    crypto::randomBytes(rnd, sizeof rnd);
    const std::string id = crypto::hex(rnd, sizeof rnd);
    Value doc = Value::object();
    doc.set("format", 1);
    doc.set("id", id);
    doc.set("droit", "service");
    doc.set("admin", a->key);
    doc.set("nom", a->name.substr(0, 60));
    doc.set("debut", static_cast<std::int64_t>(start));
    doc.set("fin", static_cast<std::int64_t>(start + static_cast<Seconds>(hours) * 3600));
    doc.set("emise", static_cast<std::int64_t>(start));
    const std::string text = core::json::write(doc, false);
    const auto sig = caller.key->sign(std::string(kPrefix) + text);
    Value body = Value::object();
    body.set("derogation", text);
    body.set("signature", crypto::hex(sig));
    const online::Reply r = c->call("PUT", "/admin/derogation", core::json::write(body, false));
    if (const std::string old = serviceTooOld(r); !old.empty()) return OpResult::fail(old);
    if (r.status != 201 || !r.error.empty() || !r.json.isObject()) return OpResult::fail(c->explain(r, "accorder la d\xC3\xA9rogation"));
    Derogation d = fromJson(r.json.at("derogation"), static_cast<Seconds>(r.json.integer("heure", start)), now());
    d.signatureOk = true;
    if (out) *out = d;
    const std::string until = dateTimeFr(d.end - (d.serverNow - d.readAt));
    Value det = Value::object();
    det.set("admin", a->key);
    det.set("nom", a->name);
    det.set("heures", hours);
    det.set("derogation", id);
    det.set("fin", static_cast<std::int64_t>(d.end));
    (void)detail::queueJournal(folder_, *caller.key, now(), "derogation_accorder", a->id,
                               "a accord\xC3\xA9 \xC3\xA0 " + a->name + " la d\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB pour " + std::to_string(hours) + " h",
                               core::json::write(det, false), {}, {}, nullptr);
    (void)detail::flushJournalWith(folder_, *c);
    OpResult res = OpResult::done("D\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB accord\xC3\xA9" "e \xC3\xA0 " + a->name + " pour " + std::to_string(hours) +
                                  " h : jusqu'au " + until + ".");
    if (!r.json.str("remplacee").empty()) res.details.push_back("Elle remplace la d\xC3\xA9rogation qui courait encore.");
    res.details.push_back("Le service la tient \xC3\xA0 son heure : \xC3\xA0 la fin, elle s'arr\xC3\xAAte d'elle-m\xC3\xAAme ; R\xC3\xA9voquer l'arr\xC3\xAAte avant.");
    res.details.push_back("Pour mettre le service en ligne, " + a->name + " se connecte \xC3\xA0 Cloudflare avec SON compte (invite-le comme membre de ton compte "
                          "Cloudflare) : jamais un jeton partag\xC3\xA9.");
    res.details.push_back("Chaque fois qu'il s'en sert, le journal le dit et tu es pr\xC3\xA9venu.");
    res.output = id;
    return res;
}

OpResult Workspace::revokeDerogation(Derogation* out) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (detail::callerDenied(caller, {})) return OpResult::fail("seul l'admin+ r\xC3\xA9voque une d\xC3\xA9rogation");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    const online::Reply g = c->call("GET", "/admin/derogation");
    if (const std::string old = serviceTooOld(g); !old.empty()) return OpResult::fail(old);
    if (g.status != 200 || !g.error.empty() || !g.json.isObject()) return OpResult::fail(c->explain(g, "lire la d\xC3\xA9rogation en place"));
    const Value& cur = g.json.at("derogation");
    if (!cur.isObject() || cur.str("id").empty()) return OpResult::fail("aucune d\xC3\xA9rogation en place : rien \xC3\xA0 r\xC3\xA9voquer");
    if (cur.str("etat") != "active" && cur.str("etat") != "a_venir") {
        if (out) *out = fromJson(cur, static_cast<Seconds>(g.json.integer("heure")), now());
        return OpResult::fail("la d\xC3\xA9rogation de " + cur.str("nom") + " ne court plus (" + (cur.str("etat") == "revoquee" ? std::string("d\xC3\xA9j\xC3\xA0 r\xC3\xA9voqu\xC3\xA9" "e") : std::string("termin\xC3\xA9" "e")) + ")");
    }
    Value body = Value::object();
    body.set("id", cur.str("id"));
    const online::Reply r = c->call("POST", "/admin/derogation/revoquer", core::json::write(body, false));
    if (r.status != 200 || !r.error.empty() || !r.json.isObject()) return OpResult::fail(c->explain(r, "r\xC3\xA9voquer la d\xC3\xA9rogation"));
    Derogation d = fromJson(r.json.at("derogation"), static_cast<Seconds>(r.json.integer("heure")), now());
    if (out) *out = d;
    Value det = Value::object();
    det.set("admin", d.adminKey);
    det.set("nom", d.adminName);
    det.set("derogation", d.id);
    det.set("restait", static_cast<std::int64_t>(std::max<Seconds>(0, d.end - d.serverNow)));
    std::string adminId = d.adminKey;
    if (caller.admins)
        if (const adm::Admin* a = caller.admins->byKey(d.adminKey)) adminId = a->id;
    (void)detail::queueJournal(folder_, *caller.key, now(), "derogation_revoquer", adminId,
                               "a r\xC3\xA9voqu\xC3\xA9 la d\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB de " + d.adminName, core::json::write(det, false), {}, {}, nullptr);
    (void)detail::flushJournalWith(folder_, *c);
    OpResult res = OpResult::done("D\xC3\xA9rogation de " + d.adminName + " r\xC3\xA9voqu\xC3\xA9" "e : l'admin+ seul g\xC3\xA8re de nouveau le service.");
    res.details.push_back("Son Admin repasse en lecture seule au prochain regard (15 s au plus) ; le service refuse d\xC3\xA9j\xC3\xA0 ses gestes.");
    return res;
}

OpResult Workspace::readDerogation(Derogation& out) const {
    out = Derogation{};
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (caller.role != Role::Plus && caller.role != Role::Admin) return OpResult::fail(kServicePlusOnly);
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    const online::Reply r = c->call("GET", "/admin/derogation");
    if (const std::string old = serviceTooOld(r); !old.empty()) return OpResult::fail(old);
    if (r.status != 200 || !r.error.empty() || !r.json.isObject()) return OpResult::fail(c->explain(r, "lire la d\xC3\xA9rogation"));
    const Value& d = r.json.at("derogation");
    out = fromJson(d, static_cast<Seconds>(r.json.integer("heure")), now());
    if (!d.isObject() || out.id.empty()) return OpResult::done("Aucune d\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB en cours.");
    std::vector<licensing::crypto::PublicKey> editors = caller.editorKeys;
    if (caller.role == Role::Plus) editors.push_back(caller.key->publicKey());
    out.signatureOk = documentOk(d, editors, out);
    if (!out.signatureOk)
        return OpResult::fail("la d\xC3\xA9rogation rendue par le service n'est pas sign\xC3\xA9" "e par une cl\xC3\xA9 de l'\xC3\xA9" "diteur que ce poste conna\xC3\xAEt : elle est ignor\xC3\xA9" "e");
    OpResult res = OpResult::done("D\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB de " + out.adminName + " : " + derogationText(out, now()) + ".");
    res.details.push_back("Sign\xC3\xA9" "e par la cl\xC3\xA9 de l'\xC3\xA9" "diteur ; utilis\xC3\xA9" "e " + std::to_string(out.uses) + " fois.");
    return res;
}

OpResult Workspace::useDerogation(const std::string& action, const std::string& detailText) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (caller.role == Role::Plus) return OpResult::done("L'admin+ g\xC3\xA8re le service.");
    if (caller.role != Role::Admin) return OpResult::fail(kServicePlusOnly);
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    Value body = Value::object();
    body.set("action", action);
    if (!detailText.empty()) body.set("detail", detailText.substr(0, 200));
    const online::Reply r = c->call("POST", "/admin/derogation/utiliser", core::json::write(body, false));
    if (const std::string old = serviceTooOld(r); !old.empty()) return OpResult::fail(old);
    if (r.error.empty() && r.status == 403) return OpResult::fail(r.message().empty() ? std::string(kServicePlusOnly) : r.message());
    if (r.status != 200 || !r.error.empty() || !r.json.isObject()) return OpResult::fail(c->explain(r, "v\xC3\xA9rifier ta d\xC3\xA9rogation aupr\xC3\xA8s du service"));
    const Derogation d = fromJson(r.json.at("derogation"), static_cast<Seconds>(r.json.integer("heure")), now());
    OpResult res = OpResult::done("D\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB : " + derogationText(d, now()) + " ; le geste est \xC3\xA9" "crit au journal (l'admin+ est pr\xC3\xA9venu).");
    return res;
}

OpResult Workspace::serviceEditorKeys(std::vector<std::string>& out) const {
    out.clear();
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    const online::Reply r = c->call("GET", "/admin/admins");
    if (r.status != 200 || !r.error.empty() || !r.json.isObject()) return OpResult::fail(c->explain(r, "lire les cl\xC3\xA9s de l'\xC3\xA9" "diteur du service"));
    if (const Value* e = r.json.find("editeurs"); e && e->isArray())
        for (const auto& k : e->items())
            if (k.isObject() && licensing::service::isKeyHex(k.str("cle")) && std::find(out.begin(), out.end(), k.str("cle")) == out.end()) out.push_back(k.str("cle"));
    if (out.empty()) return OpResult::fail("le service ne dit aucune cl\xC3\xA9 de l'\xC3\xA9" "diteur (PUBLISHER_KEYS) : demande \xC3\xA0 l'admin+ de d\xC3\xA9ployer lui-m\xC3\xAAme");
    // Chacune doit etre une cle que CE poste croit (compilee, retenue) : jamais une cle inconnue dans le Worker.
    for (const auto& h : out) {
        bool known = false;
        for (const auto& k : caller.editorKeys) known = known || crypto::hex(k) == h;
        if (caller.role == Role::Plus) known = known || caller.key->publicKeyHex() == h;
        if (!known) return OpResult::fail("le service nomme une cl\xC3\xA9 de l'\xC3\xA9" "diteur que ce poste ne conna\xC3\xAEt pas (" + h.substr(0, 16) + "\xE2\x80\xA6) : rien n'est \xC3\xA9" "crit");
    }
    OpResult res = OpResult::done(std::to_string(out.size()) + (out.size() > 1 ? " cl\xC3\xA9s" : " cl\xC3\xA9") + " de l'\xC3\xA9" "diteur (PUBLISHER_KEYS du service).");
    return res;
}

OpResult Workspace::writeEditorKeysFile(const std::string& kitFolder) const {
    std::vector<std::string> keys;
    OpResult r = serviceEditorKeys(keys);
    if (!r.ok) return r;
    std::string text = "# Les cles PUBLIQUES de l'editeur des licences XPG (PUBLISHER_KEYS du service), lues sur le service en place. Rien de secret.\n";
    for (const auto& k : keys) text += k + "\n";
    std::string why;
    const std::string path = P8(fs::path(std::u8string(kitFolder.begin(), kitFolder.end())) / "cle-publique.txt");
    if (!crypto::replaceFile(path, text, false, &why)) return OpResult::fail(why);
    OpResult res = OpResult::done("cle-publique.txt \xC3\xA9" "crite dans le kit : les cl\xC3\xA9s PUBLIQUES de l'\xC3\xA9" "diteur (celles que le service croit d\xC3\xA9j\xC3\xA0), jamais la tienne.");
    res.output = path;
    return res;
}

// ============================================================================================ la connexion
void Workspace::discoverConnection() {
    const connexion::Places* p = connexion::places();
    if (!p || role_ == Role::Plus || online::configured(folder_)) return;
    const connexion::Discovery d = connexion::discover(*p, editorKeys_, now());
    if (d.found) {
        std::string why;
        if (connexion::adopt(folder_, d.connection, false, now(), &why)) return;
        connexion::noteRefused(folder_, {{d.connection.path, d.connection.source, why}}, now());
        return;
    }
    if (!d.refused.empty() || !d.looked.empty()) connexion::noteRefused(folder_, d.refused, now());
}

OpResult Workspace::useConnectionFile(const std::string& path) {
    if (online::configured(folder_) && !mayManageService() && role_ != Role::None && role_ != Role::Pending) return OpResult::fail(whyNotService());
    connexion::Places p;
    p.explicitFile = path;
    const connexion::Discovery d = connexion::discover(p, editorKeys_, now());
    if (!d.found) {
        connexion::noteRefused(folder_, d.refused, now());
        reload();
        return OpResult::fail(path + " est refus\xC3\xA9 : " + (d.refused.empty() ? std::string("illisible") : d.refused.front().why));
    }
    std::string why;
    if (!connexion::adopt(folder_, d.connection, true, now(), &why)) return OpResult::fail(why);
    service_.hasState = false;
    reload();
    OpResult r = OpResult::done("Connect\xC3\xA9 au service de l'\xC3\xA9" "diteur : " + d.connection.url + ".");
    r.details.push_back("Sign\xC3\xA9" "e par la cl\xC3\xA9 de l'\xC3\xA9" "diteur (" + d.connection.editorKeyId + ") ; la cl\xC3\xA9 du service attendue : " + online::keyFingerprint(d.connection.serviceKey) + ".");
    if (d.connection.expired) r.details.push_back("Son certificat a pass\xC3\xA9 sa date : l'admin+ doit le renouveler (l'adresse reste la sienne).");
    return r;
}

OpResult Workspace::exportConnection(const std::string& path) const {
    const std::string text = connexion::exportText(folder_);
    if (text.empty())
        return OpResult::fail("aucun certificat du service v\xC3\xA9rifi\xC3\xA9 sur ce poste : V\xC3\xA9rifier d'abord (Service en ligne), puis recommence");
    connexion::Connection c;
    std::string why;
    std::vector<licensing::crypto::PublicKey> editors = editorKeys_;
    if (signing_) editors.push_back(signing_->publicKey());
    if (!connexion::read(text, editors, now(), c, &why)) return OpResult::fail("le certificat gard\xC3\xA9 sur ce poste ne se v\xC3\xA9rifie pas : " + why);
    if (!crypto::replaceFile(path, text, false, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Connexion au service export\xC3\xA9" "e : " + path + ".");
    r.details.push_back("Le certificat du service (" + c.url + "), sign\xC3\xA9 par la cl\xC3\xA9 de l'\xC3\xA9" "diteur : rien de secret.");
    r.details.push_back("Pose-le \xC3\xA0 c\xC3\xB4t\xC3\xA9 de l'installateur de l'Admin (" + std::string(connexion::kFileName) + ") : l'Admin install\xC3\xA9 d\xC3\xA9marre connect\xC3\xA9 \xC3\xA0 ton service. "
                          "Pour l'installateur publi\xC3\xA9 : ressources-licences/" + connexion::kFileName + " (outils/mingw/admin_editeur.sh l'y met).");
    r.output = path;
    return r;
}

} // namespace license_admin::gui
