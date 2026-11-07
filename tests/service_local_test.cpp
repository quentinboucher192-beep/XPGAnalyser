// =============================================================================
//  tests/service_local_test.cpp - 1.35.0 : le C++ et le Worker s'entendent,
//  octet pour octet
// -----------------------------------------------------------------------------
//  Lance le VRAI service des licences sur ce poste (tests/service_local.hpp :
//  workerd par wrangler dev) et lui parle par le VRAI transport des Studios
//  (app::ai::makeHttpClient, libcurl sous Linux), avec les formats de
//  licensing/Service.hpp :
//    - GET /v1/etat ;
//    - l'editeur (une cle creee ici) lit la cle du service, signe son
//      certificat et le depose (appels signes : X-XPG-*) ;
//    - il depose une licence en ligne (writeLicense + signBlock) et l'empreinte
//      d'un code (newActivationCode, codeHash) ;
//    - un PC (deviceKeySpec) s'active avec le code et une demande .xpgreq
//      (writeRequest + signBlock) ; le C++ verifie l'activation rendue
//      (verifyCert + verifyServiceBlock + parseActivation) ;
//    - l'heure signee, le renouvellement, la Decouverte, le jeton d'une licence
//      flottante, une requete avec ?depuis= signee, et des refus.
//  Sans Node.js ni wrangler sur ce poste : "SAUTE : ..." et 0.
//  argv[1] (facultatif) : le dossier source (sinon XPG_SOURCE_DIR, sinon celui
//  de tests/service_local.hpp).
// =============================================================================
#include "service_local.hpp"

#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/licensing/Documents.hpp"
#include "../src/licensing/KeyFile.hpp"
#include "../src/licensing/Service.hpp"
#include "../src/licensing/SignedFile.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <string>

namespace fs = std::filesystem;
namespace lic = licensing;
namespace sv = licensing::service;
namespace json = core::json;

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}

struct Reply {
    int         status{0};
    std::string body;
    std::string failure;   // la panne du transport (vide : une reponse est arrivee)
    json::Value value;     // le corps, s'il est du JSON
};

// Un appel par le transport des Studios ; signe par `key` (si donnee) a l'heure `now` du service.
Reply call(const std::string& base, const std::string& method, const std::string& pathAndQuery, const std::string& body,
           const lic::SigningKey* key, lic::Seconds now, const lic::SigningKey* signer = nullptr) {
    auto client = app::ai::makeHttpClient();
    app::ai::HttpRequest req;
    req.method = method;
    req.url = base + pathAndQuery;
    req.body = body;
    req.connectTimeoutSeconds = 10;
    req.idleTimeoutSeconds = 30;
    if (!body.empty()) req.headers.emplace_back("Content-Type", "application/json");
    if (key) {
        const lic::SigningKey* s = signer ? signer : key;
        for (auto& h : sv::signRequest(method, pathAndQuery, body, key->publicKey(), [s](std::string_view m) { return s->sign(m); }, now))
            req.headers.push_back(h);
    }
    Reply out;
    app::ai::HttpSink sink;
    sink.onHeaders = [&out](int status, const app::ai::HttpHeaders&) { out.status = status; };
    sink.onData = [&out](std::string_view chunk) { out.body.append(chunk.data(), chunk.size()); };
    const auto r = client->perform(req, sink);
    if (!r.ok()) out.failure = r.error;
    if (!out.status) out.status = r.status;
    (void)json::parse(out.body, out.value);
    return out;
}

std::string body(std::initializer_list<std::pair<const char*, json::Value>> fields) {
    json::Value v = json::Value::object();
    for (const auto& [k, x] : fields) v.set(k, x);
    return json::write(v, false);
}

std::unique_ptr<lic::SigningKey> makeKey(const fs::path& dir, const std::string& name) {
    fs::create_directories(dir);
    std::string why;
    auto k = lic::SigningKey::create((dir / "cle.cle").string(), lic::deviceKeySpec(), name, false, &why);
    if (!k) std::printf("  cle impossible : %s\n", why.c_str());
    return k;
}

std::string signDoc(lic::DocKind kind, const std::string& doc, const lic::SigningKey& k) {
    return lic::writeBlock(lic::signBlock(kind, doc, k.publicKey(), [&k](std::string_view m) { return k.sign(m); }));
}

std::string onlineLicense(const std::string& id, lic::LicenseType type, int maxDevices, lic::Seconds now, const std::string& url,
                          const lic::SigningKey& editor, lic::Seconds* issued) {
    lic::License l;
    l.licenseId = id;
    l.customer = "Client d'essai";
    l.edition = "PROFESSIONAL";
    l.type = type;
    l.service = url;
    l.maxDevices = maxDevices;
    l.features = {{"ai_generation", {}}};
    l.issuedAt = now - 60;
    l.expiresAt = type == lic::LicenseType::Perpetual ? 0 : now + 365 * lic::kDay;
    l.manifest = 1;
    l.nonce = lic::newNonce();
    if (issued) *issued = l.issuedAt;
    return signDoc(lic::DocKind::License, lic::writeLicense(l), editor);
}

std::string request(const lic::SigningKey& pc, const std::string& station, lic::Seconds now) {
    lic::ActivationRequest q;
    q.deviceKey = pc.publicKeyHex();
    q.deviceName = "PC d'essai";
    q.software = "IHM Studio 1.35.0";
    q.createdAt = now;
    q.nonce = lic::newNonce();
    q.station = station;
    return signDoc(lic::DocKind::Request, lic::writeRequest(q), pc);
}

} // namespace

int main(int argc, char** argv) {
    std::mt19937_64 rng(std::random_device{}());
    const fs::path root = fs::temp_directory_path() / ("xpg-service-local-test-" + std::to_string(rng() % 1000000000ULL));
    auto editor = makeKey(root / "editeur", "Editeur essai");
    auto pc = makeKey(root / "pc", "PC essai");
    auto pc2 = makeKey(root / "pc2", "PC essai 2");
    auto other = makeKey(root / "autre", "Autre");
    if (!editor || !pc || !pc2 || !other) return 1;

    std::printf("-- le service local\n");
    xpg_test::LocalService::Options options;
    if (argc > 1) options.sourceDir = argv[1];
    xpg_test::LocalService svc({editor->publicKeyHex()}, options);
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        std::error_code ec;
        fs::remove_all(root, ec);
        return 0;
    }
    check(svc.ready(), "le service demarre : " + svc.error());
    if (!svc.ready()) {
        std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
        return 1;
    }
    std::printf("  %s (pret en %.1f s)\n", svc.url().c_str(), svc.startSeconds());
    const std::string base = svc.url();
    std::string why;
    check(app::ai::httpAvailable(&why), "le transport du systeme : " + why);

    std::printf("-- GET /v1/etat\n");
    const Reply etat = call(base, "GET", "/v1/etat", "", nullptr, 0);
    check(etat.failure.empty() && etat.status == 200, "etat : " + etat.failure + " " + std::to_string(etat.status));
    check(etat.value.str("service") == "xpg-licences" && etat.value.str("version") == "1.35.0", "le service et sa version");
    const std::string serviceKey = etat.value.str("cle_service");
    check(sv::isKeyHex(serviceKey), "la cle du service");
    check(!etat.value.flag("certifie", true), "pas encore certifie");
    const lic::Seconds now = etat.value.integer("heure");
    check(now > 1700000000 && std::llabs(now - lic::systemNow()) < 60, "l'heure du service");

    std::printf("-- l'editeur certifie la cle du service\n");
    Reply r = call(base, "GET", "/admin/cle-service", "", editor.get(), now);
    check(r.status == 200 && r.value.str("cle") == serviceKey && r.value.at("certificat").isNull(), "GET /admin/cle-service : " + r.body);
    r = call(base, "GET", "/admin/cle-service", "", other.get(), now);
    check(r.status == 403 && r.value.str("erreur") == "editeur", "une autre cle : 403");
    r = call(base, "GET", "/admin/cle-service", "", editor.get(), now, other.get());
    check(r.status == 401 && r.value.str("erreur") == "signature", "une signature d'une autre cle : 401");
    r = call(base, "GET", "/admin/cle-service", "", editor.get(), now - 700);
    check(r.status == 401 && r.value.str("erreur") == "heure", "une heure trop ancienne : 401");

    sv::ServiceCert cert;
    cert.key = serviceKey;
    cert.url = base;
    cert.issuedAt = now - 60;
    cert.expiresAt = now + 365 * lic::kDay;
    cert.rights = {sv::kRightActivation, sv::kRightFloating, sv::kRightTime, sv::kRightTrial};
    const std::string certText = signDoc(lic::DocKind::ServiceCert, sv::writeCert(cert), *editor);
    r = call(base, "PUT", "/admin/certificat", body({{"certificat", certText}}), editor.get(), now);
    check(r.status == 200, "PUT /admin/certificat : " + r.body);
    r = call(base, "GET", "/v1/certificat", "", nullptr, 0);
    check(r.status == 200 && r.body == certText, "GET /v1/certificat : le meme bloc, octet pour octet");
    sv::ServiceCert trusted;
    check(sv::verifyCert(r.body, {editor->publicKey()}, now, trusted, &why) && trusted.key == serviceKey, "verifyCert : " + why);
    check(call(base, "GET", "/v1/etat", "", nullptr, 0).value.flag("certifie"), "certifie");

    std::printf("-- l'heure signee\n");
    {
        const std::string n = lic::newNonce();
        r = call(base, "GET", "/v1/heure?n=" + n, "", nullptr, 0);
        lic::SignedBlock b;
        sv::SignedTime t;
        check(r.status == 200 && sv::findBlock(r.body, lic::DocKind::Time, b, &why) &&
                  sv::verifyServiceBlock(b, lic::DocKind::Time, trusted, sv::kRightTime, &why) && sv::parseTime(b.document, t, &why),
              "l'heure : " + why);
        check(t.nonce == n && std::llabs(t.time - now) < 60, "le nonce rendu, l'heure");
        check(call(base, "GET", "/v1/heure?n=XYZ", "", nullptr, 0).status == 400, "un nonce mal ecrit : 400");
    }

    std::printf("-- la licence en ligne et son code\n");
    const std::string code = sv::newActivationCode();
    std::string norm;
    check(sv::normalizeCode(code, norm), "le code se relit");
    lic::Seconds issued = 0;
    const std::string licText = onlineLicense("XPG-2026-00042", lic::LicenseType::Temporary, 1, now, base, *editor, &issued);
    {
        lic::SignedBlock lb;
        lic::License parsed;
        check(sv::findBlock(licText, lic::DocKind::License, lb, &why) && lic::parseLicense(lb.document, parsed, &why), "la licence en ligne se relit en C++ : " + why);
    }
    r = call(base, "PUT", "/admin/licences",
             body({{"licence", licText}, {"code_hash", sv::codeHash(norm)}, {"bail_jours", 30}, {"active", true}}), editor.get(), now);
    check(r.status == 200 && r.value.str("licence") == "XPG-2026-00042" && r.value.integer("postes_max") == 1 && r.value.flag("code"),
          "PUT /admin/licences : " + r.body);
    check(r.body.find(sv::codeHash(norm)) == std::string::npos, "l'empreinte du code ne ressort pas");

    std::printf("-- un PC s'active avec le code\n");
    const std::string station = lic::stationHash("machine-d-essai", pc->publicKey());
    std::uint64_t firstNumber = 0;
    {
        // le code tape avec souplesse (minuscules, espaces)
        std::string typed;
        for (char c : code) typed.push_back(c == '-' ? ' ' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
        r = call(base, "POST", "/v1/activer", body({{"code", typed}, {"demande", request(*pc, station, now)}, {"nom", "PC d'essai"}}), pc.get(), now);
        check(r.status == 200, "POST /v1/activer : " + std::to_string(r.status) + " " + r.body);
        check(r.value.str("licence") == licText, "la licence rendue : le bloc depose, octet pour octet");
        check(r.value.str("certificat") == certText, "le certificat rendu");
        check(!r.value.str("message").empty(), "un message");
        lic::SignedBlock ab;
        sv::Activation a;
        sv::ServiceCert c2;
        check(sv::verifyCert(r.value.str("certificat"), {editor->publicKey()}, now, c2, &why), "le certificat rendu se verifie : " + why);
        check(sv::findBlock(r.value.str("activation"), lic::DocKind::Activation, ab, &why) &&
                  sv::verifyServiceBlock(ab, lic::DocKind::Activation, c2, sv::kRightActivation, &why) && sv::parseActivation(ab.document, a, &why),
              "l'activation : signee par le service certifie, lue par le C++ : " + why);
        check(a.licenseId == "XPG-2026-00042" && a.licenseIssuedAt == issued && a.deviceKey == pc->publicKeyHex() && a.station == station &&
                  !a.floating && a.number >= 1,
              "ses champs : " + ab.document);
        check(a.until - a.issuedAt == 30 * lic::kDay && std::llabs(a.issuedAt - now) < 60, "le bail : 30 jours");
        check(sv::writeActivation(a) == ab.document, "le JSON du Worker est exactement celui qu'ecrit le C++");
        firstNumber = a.number;
        // un deuxieme PC : la licence n'a qu'un poste
        r = call(base, "POST", "/v1/activer", body({{"code", code}, {"demande", request(*pc2, "", now)}}), pc2.get(), now);
        check(r.status == 409 && r.value.str("erreur") == "complet", "un deuxieme PC : 409 complet");
        r = call(base, "POST", "/v1/activer", body({{"code", sv::newActivationCode()}, {"demande", request(*pc2, "", now)}}), pc2.get(), now);
        check(r.status == 403 && r.value.str("erreur") == "code", "un code inconnu : 403");
        r = call(base, "POST", "/v1/activer", body({{"code", code}, {"demande", request(*pc, "", now)}}), pc2.get(), now);
        check(r.status == 400 && r.value.str("erreur") == "demande", "la demande d'un autre PC : 400");
        check(r.body.find(norm) == std::string::npos && r.body.find(code) == std::string::npos, "le code n'est jamais rendu");
    }

    std::printf("-- renouveler\n");
    {
        r = call(base, "POST", "/v1/renouveler", "{}", pc.get(), now);
        check(r.status == 200, "POST /v1/renouveler : " + r.body);
        const json::Value& acts = r.value.at("activations");
        check(acts.isArray() && acts.items().size() == 1 && r.value.at("retirees").items().empty() && r.value.at("revocations").isNull() &&
                  r.value.at("poste").isNull(),
              "une activation, rien de retire");
        if (acts.isArray() && acts.items().size() == 1) {
            lic::SignedBlock ab;
            sv::Activation a;
            check(sv::findBlock(acts.items()[0].str("activation"), lic::DocKind::Activation, ab, &why) &&
                      sv::verifyServiceBlock(ab, lic::DocKind::Activation, trusted, sv::kRightActivation, &why) && sv::parseActivation(ab.document, a, &why),
                  "l'activation renouvelee : " + why);
            check(a.number > firstNumber && a.station == station, "un numero neuf, la station gardee");
            check(acts.items()[0].str("licence") == licText, "la licence");
        }
    }

    std::printf("-- la Decouverte\n");
    {
        const std::string m = sv::machineHash("machine-d-essai");
        r = call(base, "POST", "/v1/decouverte", body({{"machine", m}}), pc.get(), now);
        lic::SignedBlock b;
        sv::Trial t;
        check(r.status == 200 && sv::findBlock(r.body, lic::DocKind::Trial, b, &why) &&
                  sv::verifyServiceBlock(b, lic::DocKind::Trial, trusted, sv::kRightTrial, &why) && sv::parseTrial(b.document, t, &why),
              "la Decouverte : " + why);
        check(t.machine == m && t.deviceKey == pc->publicKeyHex() && t.days == 30 && std::llabs(t.start - now) < 60, "ses champs");
        check(sv::writeTrial(t) == b.document, "le JSON exact");
    }

    std::printf("-- la licence flottante et son jeton\n");
    {
        const std::string fcode = sv::newActivationCode();
        std::string fnorm;
        (void)sv::normalizeCode(fcode, fnorm);
        const std::string fText = onlineLicense("XPG-2026-F0042", lic::LicenseType::Floating, 1, now, base, *editor, nullptr);
        r = call(base, "PUT", "/admin/licences", body({{"licence", fText}, {"code_hash", sv::codeHash(fnorm)}}), editor.get(), now);
        check(r.status == 200 && r.value.str("type") == "floating", "la licence flottante deposee : " + r.body);
        for (const auto* k : {pc.get(), pc2.get()}) {
            r = call(base, "POST", "/v1/activer", body({{"code", fcode}, {"demande", request(*k, "", now)}}), k, now);
            lic::SignedBlock ab;
            sv::Activation a;
            check(r.status == 200 && sv::findBlock(r.value.str("activation"), lic::DocKind::Activation, ab, &why) && sv::parseActivation(ab.document, a, &why) &&
                      a.floating,
                  "l'activation flottante : " + r.body);
        }
        r = call(base, "POST", "/v1/flottante/jeton", body({{"licence", "XPG-2026-F0042"}}), pc.get(), now);
        lic::SignedBlock b;
        sv::Lease l;
        check(r.status == 200 && sv::findBlock(r.value.str("jeton"), lic::DocKind::Lease, b, &why) &&
                  sv::verifyServiceBlock(b, lic::DocKind::Lease, trusted, sv::kRightFloating, &why) && sv::parseLease(b.document, l, &why),
              "le jeton : " + why);
        check(l.licenseId == "XPG-2026-F0042" && l.deviceKey == pc->publicKeyHex() && l.until - l.issuedAt == 900 && sv::writeLease(l) == b.document,
              "ses champs, 15 minutes");
        r = call(base, "POST", "/v1/flottante/jeton", body({{"licence", "XPG-2026-F0042"}}), pc2.get(), now);
        check(r.status == 409 && r.value.str("erreur") == "complet" && r.value.integer("libre_dans") > 0, "le seul jeton est pris : 409 et libre_dans");
        check(call(base, "POST", "/v1/flottante/rendre", body({{"licence", "XPG-2026-F0042"}}), pc.get(), now).status == 200, "rendu");
        check(call(base, "POST", "/v1/flottante/jeton", body({{"licence", "XPG-2026-F0042"}}), pc2.get(), now).status == 200, "repris par l'autre PC");
    }

    std::printf("-- une requete signee avec ?depuis=, l'archive\n");
    {
        r = call(base, "POST", "/v1/demandes", body({{"cle", editor->keyId()}, {"scelle", "AAAA"}}), pc.get(), now);
        check(r.status == 201 && r.value.integer("numero") >= 1, "POST /v1/demandes : " + r.body);
        r = call(base, "GET", "/admin/demandes?depuis=0", "", editor.get(), now);
        check(r.status == 200 && r.value.at("demandes").items().size() == 1 && r.value.at("demandes").items()[0].str("poste") == sv::deviceId(pc->publicKey()),
              "GET /admin/demandes?depuis=0 : l'identifiant du poste est le meme en C++ et en JS : " + r.body);
        r = call(base, "POST", "/admin/archive", body({{"base", 0}, {"fichiers", [] {
                                                          json::Value f = json::Value::object();
                                                          f.set("a/b.txt", "contenu");
                                                          return f;
                                                      }()}}),
                 editor.get(), now);
        check(r.status == 200 && r.value.integer("version") == 1, "POST /admin/archive : " + r.body);
        r = call(base, "GET", "/admin/archive/fichier?chemin=a%2Fb.txt", "", editor.get(), now);
        check(r.status == 200 && r.body == "contenu", "GET /admin/archive/fichier (chemin encode) : " + r.body);
    }

    std::printf("-- le temps qui passe (le service relance, son horloge avancee de 31 jours)\n");
    {
        check(svc.restart(31 * lic::kDay), "relance : " + svc.error());
        const std::string base2 = svc.url();
        const Reply e2 = call(base2, "GET", "/v1/etat", "", nullptr, 0);
        const lic::Seconds later = e2.value.integer("heure");
        check(e2.value.str("cle_service") == serviceKey && later - now > 30 * lic::kDay, "la meme cle, 31 jours plus tard");
        // le bail de pc est echu : pc2 prend la place ; pc, revenu, est retire
        r = call(base2, "POST", "/v1/activer", body({{"code", code}, {"demande", request(*pc2, "", later)}}), pc2.get(), later);
        check(r.status == 200, "pc2 prend la place liberee par le bail echu : " + r.body);
        r = call(base2, "POST", "/v1/renouveler", "{}", pc.get(), later);
        bool retired = false;
        for (const auto& x : r.value.at("retirees").items()) retired = retired || x.asString() == "XPG-2026-00042";
        check(r.status == 200 && retired, "pc est retire : " + r.body);
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
