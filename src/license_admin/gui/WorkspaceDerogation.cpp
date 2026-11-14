// =============================================================================
//  license_admin/gui/WorkspaceDerogation.cpp - 1.47.0 : GERER LE SERVICE
//  (l'admin+ seul ; la derogation courte d'un admin) ET LA CONNEXION AU SERVICE
//  QUI VOYAGE AVEC L'INSTALLATEUR (docs/ADMINS.md, par. 11)
// -----------------------------------------------------------------------------
//  La derogation : Derogation.hpp (ce que voit l'Admin), service/src/derogations.js
//  (l'arbitre, a son heure). Le document, signe par la cle de l'editeur :
//  "XPG-DEROGATION-1\n" + {"format":1,"id","droit":"service","admin","nom","debut",
//  "fin","emise"} - debut : l'heure du service, fin : debut + 1, 4, 24 ou 72 h.
//  1.52.0 : le format 2 - {"format":2,"id","droit":"tout","admin","nom","duree",
//  "valable","emise"} (duree : 10 min a 72 h ; valable : emise + 1 h, l'heure limite
//  pour taper le code). Le code (8 chiffres) est retire de la cle de l'editeur
//  (derogationCode : rien n'est garde) ; seule son empreinte part, a cote du
//  document ; l'admin le tape (activateDerogation), le service compare.
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
constexpr const char* kCodePrefix = "XPG-DEROGATION-CODE-1\n";   // 1.52.0 : le code (sa signature, son empreinte)
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

// 1.52.0 : un service d'avant (1.47 a 1.51) refuse le format 2 (<< format 1 attendu >>), ou n'a pas la route du code.
std::string serviceTooOldForCode(const online::Reply& r) {
    if (r.error.empty() && ((r.status == 404 && r.code() == "route") || (r.status == 400 && r.code() == "derogation" && r.message().find("format 1 attendu") != std::string::npos)))
        return "ton service en ligne ne conna\xC3\xAEt pas encore la d\xC3\xA9rogation par code : l'admin+ doit d\xC3\xA9ployer la 1.52.0 (Service en ligne \xE2\x80\xBA D\xC3\xA9ployer le service)";
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
    // 1.52.0 : le format 2 (jamais le code ni son empreinte : le service ne les rend pas)
    out.format = static_cast<int>(d.integer("format", 1));
    out.right = d.str("droit").substr(0, 20);
    out.duration = static_cast<Seconds>(d.integer("duree"));
    out.validUntil = static_cast<Seconds>(d.integer("valable"));
    out.cancelled = static_cast<Seconds>(d.integer("annulee"));
    out.triesLeft = static_cast<int>(d.integer("essais_restants"));
    out.reason = d.str("raison").substr(0, 20);
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
    if (doc.integer("format") == 2)   // 1.52.0 : << tout >>, la duree et l'heure limite du code (le debut : quand il tape le code)
        return doc.str("id") == view.id && doc.str("admin") == view.adminKey && doc.str("droit") == "tout" && !doc.find("code") &&
               static_cast<Seconds>(doc.integer("duree")) == view.duration && static_cast<Seconds>(doc.integer("valable")) == view.validUntil &&
               (view.state != "active" || view.end - view.start == view.duration);
    return doc.str("id") == view.id && doc.str("admin") == view.adminKey && doc.str("droit") == "service" &&
           static_cast<Seconds>(doc.integer("fin")) == view.end && static_cast<Seconds>(doc.integer("debut")) == view.start;
}

// 1.52.0 : << 15:42 >> pour aujourd'hui, la date sinon.
std::string clockOrDate(Seconds t, Seconds now) {
    const std::string a = dateTimeFr(t), b = dateTimeFr(now);
    const auto sa = a.find(' '), sb = b.find(' ');
    if (sa != std::string::npos && sb != std::string::npos && a.compare(0, sa, b, 0, sb) == 0) return a.substr(sa + 1);
    return a;
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

std::string durationText(Seconds s) {
    if (s <= 0) return "0 min";
    if (s < 3600) return std::to_string((s + 59) / 60) + " min";
    const long h = static_cast<long>(s / 3600), m = static_cast<long>((s % 3600) / 60);
    return m ? std::to_string(h) + " h " + two(m) : std::to_string(h) + " h";
}

std::string clockText(Seconds localTime, Seconds localNow) { return clockOrDate(localTime, localNow); }

std::string derogationText(const Derogation& d, Seconds localNow) {
    if (!d.exists()) return "aucune";
    if (d.revoked || d.state == "revoquee") return "r\xC3\xA9voqu\xC3\xA9" "e par l'admin+" + (d.revoked ? " le " + dateTimeFr(d.revoked) : std::string());
    // 1.52.0 : en attente du code, annulee
    if (d.pendingAt(localNow))
        return "en attente du code (valable jusqu'\xC3\xA0 " + clockOrDate(d.localTime(d.validUntil, localNow), localNow) + ")";
    if (d.state == "en_attente" || d.state == "annulee" || d.cancelled)
        return std::string("annul\xC3\xA9" "e (") + (d.reason == "code_faux" ? "5 codes faux" : "le code n'a pas \xC3\xA9t\xC3\xA9 tap\xC3\xA9 \xC3\xA0 temps") + ")";
    if (d.activeAt(localNow)) {
        const Seconds left = d.remaining(localNow);
        // l'heure de la fin, en heure de ce PC : le service l'a donnee a son heure (l'ecart corrige)
        const Seconds endLocal = d.end - (d.serviceNow(localNow) - localNow);
        return "il reste " + remainingText(left) + " (jusqu'\xC3\xA0 " + (left > 20 * 3600 ? dateTimeFr(endLocal) : hourMinute(endLocal)) + ")";
    }
    if (d.serviceNow(localNow) < d.start) return "pas encore commenc\xC3\xA9" "e";
    return "termin\xC3\xA9" "e le " + dateTimeFr(d.end - (d.serviceNow(localNow) - localNow));
}

// ============================================================================================ 1.52.0 : le code
std::string derogationCode(const licensing::SigningKey& editorKey, const std::string& id) {
    const auto sig = editorKey.sign(std::string(kCodePrefix) + id);   // Ed25519 : deterministe
    const auto h = crypto::sha256(sig.data(), sig.size());
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<std::uint64_t>(h[static_cast<std::size_t>(i)]);
    char b[16];
    std::snprintf(b, sizeof b, "%08llu", static_cast<unsigned long long>(v % 100000000ULL));
    return b;
}

std::string derogationCodeHash(const std::string& id, const std::string& code) { return crypto::sha256Hex(std::string(kCodePrefix) + id + "\n" + code); }

std::string formatDerogationCode(const std::string& code) { return code.size() == 8 ? code.substr(0, 4) + " " + code.substr(4) : code; }

std::string normalizeDerogationCode(const std::string& typed) {
    std::string out;
    for (const char c : typed) {
        if (c >= '0' && c <= '9') out += c;
        else if (c != ' ' && c != '-' && c != '.' && c != '\t' && c != '_') return {};
        if (out.size() > 8) return {};
    }
    return out.size() == 8 ? out : std::string();
}

std::string Workspace::derogationCode(const std::string& id) const {
    if (role_ != Role::Plus || !signing_ || id.size() != 32) return {};
    return gui::derogationCode(*signing_, id);
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
        if (derogation_.pendingAt(now()))   // 1.52.0
            return "Ta d\xC3\xA9rogation attend son code : tape le code que l'admin+ voit sur son PC (Mon compte).";
        if (derogation_.state == "annulee" || derogation_.state == "en_attente" || derogation_.cancelled)
            return "Ta d\xC3\xA9rogation est annul\xC3\xA9" "e (" + std::string(derogation_.reason == "code_faux" ? "5 codes faux" : "le code n'a pas \xC3\xA9t\xC3\xA9 tap\xC3\xA9 \xC3\xA0 temps") +
                   ") : l'admin+ seul g\xC3\xA8re le service (demande-lui une nouvelle d\xC3\xA9rogation).";
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
    // 1.52.0 : le format 2
    d.format = static_cast<int>(w.derogationFormat > 0 ? w.derogationFormat : 1);
    d.right = w.derogationRight;
    d.duration = static_cast<Seconds>(w.derogationDuration);
    d.validUntil = static_cast<Seconds>(w.derogationValidUntil);
    d.cancelled = static_cast<Seconds>(w.derogationCancelled);
    d.triesLeft = static_cast<int>(w.derogationTriesLeft);
    d.reason = w.derogationReason;
    // la verification du document se garde d'une lecture complete de la meme derogation
    d.signatureOk = derogation_.id == d.id && derogation_.signatureOk;
    derogation_ = d;
}

OpResult Workspace::grantDerogation(const std::string& adminKeyHex, Seconds duration, Derogation* out) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (detail::callerDenied(caller, {})) return OpResult::fail("seul l'admin+ accorde une d\xC3\xA9rogation");
    if (duration < kDerogationMinSeconds || duration > kDerogationMaxSeconds)
        return OpResult::fail("une d\xC3\xA9rogation dure de 10 min \xC3\xA0 72 h : jamais plus, jamais \xC2\xAB permanente \xC2\xBB");
    const adm::Admin* a = caller.admins ? caller.admins->byKey(adminKeyHex) : nullptr;
    if (!a) return OpResult::fail("cet admin n'est pas dans la liste des admins en place");
    if (!a->active()) return OpResult::fail(a->name + " est retir\xC3\xA9 : il ne peut rien recevoir");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    // 1.52.0 : le format 2 - emise a l'heure du service (a 10 minutes pres) ; le code se tape dans l'heure ; la duree
    // commence quand il le tape.
    const Seconds issued = c->serviceNow();
    unsigned char rnd[16];
    crypto::randomBytes(rnd, sizeof rnd);
    const std::string id = crypto::hex(rnd, sizeof rnd);
    Value doc = Value::object();
    doc.set("format", 2);
    doc.set("id", id);
    doc.set("droit", "tout");
    doc.set("admin", a->key);
    doc.set("nom", a->name.substr(0, 60));
    doc.set("duree", static_cast<std::int64_t>(duration));
    doc.set("valable", static_cast<std::int64_t>(issued + kDerogationCodeSeconds));
    doc.set("emise", static_cast<std::int64_t>(issued));
    const std::string text = core::json::write(doc, false);
    const auto sig = caller.key->sign(std::string(kPrefix) + text);
    Value body = Value::object();
    body.set("derogation", text);
    body.set("signature", crypto::hex(sig));
    // le code n'est jamais garde ni envoye : seulement son empreinte, a cote du document (jamais dedans)
    body.set("code", derogationCodeHash(id, gui::derogationCode(*caller.key, id)));
    const online::Reply r = c->call("PUT", "/admin/derogation", core::json::write(body, false));
    if (const std::string old = serviceTooOldForCode(r); !old.empty()) return OpResult::fail(old);
    if (r.status != 201 || !r.error.empty() || !r.json.isObject()) return OpResult::fail(c->explain(r, "accorder la d\xC3\xA9rogation"));
    Derogation d = fromJson(r.json.at("derogation"), static_cast<Seconds>(r.json.integer("heure", issued)), now());
    d.signatureOk = true;
    if (out) *out = d;
    const std::string until = clockOrDate(d.localTime(d.validUntil, now()), now());
    const std::string howLong = durationText(duration);
    Value det = Value::object();
    det.set("admin", a->key);
    det.set("nom", a->name);
    det.set("format", 2);
    det.set("droit", "tout");
    det.set("duree", static_cast<std::int64_t>(duration));
    det.set("valable", static_cast<std::int64_t>(d.validUntil));
    det.set("derogation", id);
    (void)detail::queueJournal(folder_, *caller.key, now(), "derogation_accorder", a->id,
                               "a accord\xC3\xA9 \xC3\xA0 " + a->name + " une d\xC3\xA9rogation (tout, " + howLong + "), \xC3\xA0 activer par son code avant " + until,
                               core::json::write(det, false), {}, {}, nullptr);
    (void)detail::flushJournalWith(folder_, *c);
    OpResult res = OpResult::done("D\xC3\xA9rogation accord\xC3\xA9" "e \xC3\xA0 " + a->name + " pour " + howLong + " : elle commence quand il tape le code (valable jusqu'\xC3\xA0 " +
                                  until + ").");
    if (!r.json.str("remplacee").empty()) res.details.push_back("Elle remplace la d\xC3\xA9rogation qui attendait son code ou courait encore.");
    res.details.push_back("Donne-lui le code de vive voix (au t\xC3\xA9l\xC3\xA9phone) : il le tape dans XPG Admin. Ce PC le montre tant qu'il attend (Service en ligne, "
                          "Administrateurs) ; 5 codes faux l'annulent.");
    res.details.push_back("Il voit tout comme toi pendant " + howLong + ", sauf ce qui reste \xC3\xA0 ta cl\xC3\xA9 (les admins, les invitations, les versions \xC3\xA0 publier, la boutique, "
                          "Stripe, la sauvegarde, la restauration) ; le service la tient \xC3\xA0 son heure, R\xC3\xA9voquer l'arr\xC3\xAAte avant.");
    res.details.push_back("Pour mettre le service en ligne, " + a->name + " se connecte \xC3\xA0 Cloudflare avec SON compte (invite-le comme membre de ton compte "
                          "Cloudflare) : jamais un jeton partag\xC3\xA9.");
    res.details.push_back("Quand il tape le code, et chaque fois qu'il s'en sert, le journal le dit et tu es pr\xC3\xA9venu.");
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
    const std::string st = cur.str("etat");
    if (st != "active" && st != "a_venir" && st != "en_attente") {   // 1.52.0 : en attente du code aussi
        if (out) *out = fromJson(cur, static_cast<Seconds>(g.json.integer("heure")), now());
        return OpResult::fail("la d\xC3\xA9rogation de " + cur.str("nom") + " ne court plus (" +
                              (st == "revoquee" ? std::string("d\xC3\xA9j\xC3\xA0 r\xC3\xA9voqu\xC3\xA9" "e") : st == "annulee" ? std::string("annul\xC3\xA9" "e") : std::string("termin\xC3\xA9" "e")) + ")");
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
    det.set("restait", static_cast<std::int64_t>(st == "en_attente" ? 0 : std::max<Seconds>(0, d.end - d.serverNow)));
    if (st == "en_attente") det.set("en_attente", true);
    std::string adminId = d.adminKey;
    if (caller.admins)
        if (const adm::Admin* a = caller.admins->byKey(d.adminKey)) adminId = a->id;
    (void)detail::queueJournal(folder_, *caller.key, now(), "derogation_revoquer", adminId,
                               std::string(d.everything() ? "a r\xC3\xA9voqu\xC3\xA9 la d\xC3\xA9rogation de " : "a r\xC3\xA9voqu\xC3\xA9 la d\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB de ") +
                                   d.adminName + (st == "en_attente" ? std::string(" (son code pas encore tap\xC3\xA9)") : std::string()),
                               core::json::write(det, false), {}, {}, nullptr);
    (void)detail::flushJournalWith(folder_, *c);
    OpResult res = OpResult::done("D\xC3\xA9rogation de " + d.adminName + " r\xC3\xA9voqu\xC3\xA9" "e" +
                                  (st == "en_attente"   ? std::string(" : son code ne sert plus.")
                                   : d.everything() ? std::string(" : son XPG Admin repasse \xC3\xA0 la gestion.")
                                                    : std::string(" : l'admin+ seul g\xC3\xA8re de nouveau le service.")));
    res.details.push_back(d.everything() ? "Son XPG Admin repasse \xC3\xA0 la gestion seule au prochain regard (15 s au plus) ; le service refuse d\xC3\xA9j\xC3\xA0 ce qu'elle ouvrait."
                                         : "Son Admin repasse en lecture seule au prochain regard (15 s au plus) ; le service refuse d\xC3\xA9j\xC3\xA0 ses gestes.");
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
    if (!d.isObject() || out.id.empty()) return OpResult::done("Aucune d\xC3\xA9rogation en cours.");
    std::vector<licensing::crypto::PublicKey> editors = caller.editorKeys;
    if (caller.role == Role::Plus) editors.push_back(caller.key->publicKey());
    out.signatureOk = documentOk(d, editors, out);
    if (!out.signatureOk)
        return OpResult::fail("la d\xC3\xA9rogation rendue par le service n'est pas sign\xC3\xA9" "e par une cl\xC3\xA9 de l'\xC3\xA9" "diteur que ce poste conna\xC3\xAEt : elle est ignor\xC3\xA9" "e");
    OpResult res = OpResult::done(std::string(out.everything() ? "D\xC3\xA9rogation de " : "D\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB de ") + out.adminName + " : " +
                                  derogationText(out, now()) + ".");
    if (out.everything()) res.details.push_back("Dur\xC3\xA9" "e choisie : " + durationText(out.duration) + (out.pendingAt(now()) ? " ; essais qui restent : " + std::to_string(out.triesLeft) : std::string()) + ".");
    res.details.push_back("Sign\xC3\xA9" "e par la cl\xC3\xA9 de l'\xC3\xA9" "diteur ; utilis\xC3\xA9" "e " + std::to_string(out.uses) + " fois.");
    return res;
}

// 1.52.0 : un admin tape le code que l'admin+ voit sur son PC.
OpResult Workspace::activateDerogation(const std::string& idIn, const std::string& typedCode, Derogation* out) const {
    const std::string code = normalizeDerogationCode(typedCode);
    if (code.empty()) return OpResult::fail("le code fait 8 chiffres : celui que l'admin+ voit sur son PC (les espaces ne comptent pas)");
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (caller.role != Role::Admin) return OpResult::fail("seul l'admin qui re\xC3\xA7oit la d\xC3\xA9rogation tape son code");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    std::vector<licensing::crypto::PublicKey> editors = caller.editorKeys;
    // relire la sienne (son etat, ses essais) : apres un refus, et quand l'identifiant n'est pas connu
    const auto reread = [&](Derogation& into) {
        const online::Reply g = c->call("GET", "/admin/derogation");
        if (g.status != 200 || !g.error.empty() || !g.json.isObject()) return false;
        const Value& v = g.json.at("derogation");
        into = fromJson(v, static_cast<Seconds>(g.json.integer("heure")), now());
        into.signatureOk = v.isObject() && documentOk(v, editors, into);
        return true;
    };
    std::string id = idIn;
    if (id.empty()) {
        Derogation cur;
        if (!reread(cur) || !cur.exists()) return OpResult::fail("aucune d\xC3\xA9rogation \xC3\xA0 ton nom : l'admin+ ne t'en a pas envoy\xC3\xA9 (ou le service ne r\xC3\xA9pond pas)");
        id = cur.id;
    }
    Value body = Value::object();
    body.set("id", id);
    body.set("code", code);
    const online::Reply r = c->call("POST", "/admin/derogation/activer", core::json::write(body, false));
    if (const std::string old = serviceTooOldForCode(r); !old.empty()) return OpResult::fail(old);
    if (r.error.empty() && r.status >= 400 && r.status < 500 && !r.message().empty()) {
        if (out) {
            Derogation cur;
            if (reread(cur)) *out = cur;
        }
        return OpResult::fail(r.message());   // << Code faux : il te reste 4 essais. >>, trop tard, annulee, revoquee
    }
    if (r.status != 200 || !r.error.empty() || !r.json.isObject()) return OpResult::fail(c->explain(r, "activer ta d\xC3\xA9rogation"));
    const Value& v = r.json.at("derogation");
    Derogation d = fromJson(v, static_cast<Seconds>(r.json.integer("heure")), now());
    d.signatureOk = v.isObject() && documentOk(v, editors, d);
    if (!d.signatureOk)
        return OpResult::fail("la d\xC3\xA9rogation rendue par le service n'est pas sign\xC3\xA9" "e par une cl\xC3\xA9 de l'\xC3\xA9" "diteur que ce poste conna\xC3\xAEt : elle est ignor\xC3\xA9" "e");
    if (out) *out = d;
    const Seconds endLocal = d.localTime(d.end, now());
    OpResult res = OpResult::done("D\xC3\xA9rogation active : tu vois tout comme l'admin+ pendant " + durationText(d.duration > 0 ? d.duration : d.end - d.start) +
                                  " (jusqu'\xC3\xA0 " + clockOrDate(endLocal, now()) + ").");
    res.details.push_back("Le service la tient \xC3\xA0 son heure : \xC3\xA0 la fin, ou si l'admin+ la r\xC3\xA9voque, XPG Admin repasse \xC3\xA0 la gestion seule.");
    res.details.push_back("Chaque geste qu'elle ouvre est \xC3\xA9" "crit au journal des admins ; l'admin+ est pr\xC3\xA9venu.");
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
        if (connexion::adopt(folder_, d.connection, false, now(), &why)) {
            if (!d.refused.empty()) connexion::noteRefused(folder_, d.refused, now());   // ce qui a ete refuse avant reste dit
            return;
        }
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
