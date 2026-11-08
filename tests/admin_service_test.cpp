// =============================================================================
//  tests/admin_service_test.cpp - 1.35.0 : L'OUTIL DE L'EDITEUR ET LE SERVICE
//  DES LICENCES (license_admin/ServiceClient, ArchiveSync), contre le VRAI
//  Worker lance sur ce poste (tests/service_local.hpp)
// -----------------------------------------------------------------------------
//  Sans reseau d'abord : la cle de synchro et son code de secours, le
//  chiffrement d'un fichier de l'archive (lie a son chemin), l'adresse du
//  service, une licence EN LIGNE signee sans PC (et les refus), le releve des
//  demandes (processRequests : ouvertes avec la cle de l'editeur, la signature
//  du PC, les gardes - rejouee, plus ancienne, sceau change, autre cle, faux
//  poste), le kit des Studios (ServiceAddress.hpp, cle-publique.txt).
//  Puis contre le service local : la cle de l'editeur acceptee, une AUTRE
//  refusee (403) ; certifier la cle du service ; deposer le registre ; une
//  licence en ligne et son code - un PC (une cle creee ici) s'active avec lui
//  par un appel signe, la liste le montre, le PC est retire, la licence
//  desactivee puis reactivee ; une licence flottante et ses jetons ; les
//  demandes manuelles (refusee : la raison scellee pour le PC ; acceptee : une
//  licence liee publiee a son PC ; une demande rejouee ignoree) ; la liste de
//  revocation privee (seul le PC concerne la recoit) ; la synchro de l'archive
//  entre deux postes Admin (un envoi concurrent : 409, relecture, fusion) ; le
//  code d'activation montre UNE fois et jamais ecrit dans un fichier ; le
//  certificat renouvele (l'horloge du service avancee).
//  Aucune vraie cle, aucun vrai client : des cles creees ici, des noms d'essai.
//
//    admin_service_test SOURCE [XPG-License-Admin]
//  (le second argument, facultatif : la console, pour licence-en-ligne).
//  Sans Node.js ni wrangler : la partie sans reseau, puis "SAUTE : ...".
// =============================================================================
#include "service_local.hpp"

#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/ArchiveSync.hpp"
#include "../src/license_admin/ServiceClient.hpp"
#include "../src/license_admin/gui/Workspace.hpp"
#include "../src/licensing/Documents.hpp"
#include "../src/licensing/Gateway.hpp"
#include "../src/licensing/KeyFile.hpp"
#include "../src/licensing/Service.hpp"
#include "../src/licensing/SignedFile.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace lic = licensing;
namespace sv = licensing::service;
namespace gw = licensing::gateway;
namespace json = core::json;
namespace la = license_admin;
namespace on = license_admin::online;
namespace ar = license_admin::archive;

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f << text;
}
std::string hex32() {
    std::uint8_t b[16];
    lic::crypto::randomBytes(b, sizeof b);
    return lic::crypto::hex(b, sizeof b);
}
// Un texte se trouve-t-il dans un des fichiers de ce dossier (tout l'arbre) ?
bool foundInFiles(const fs::path& dir, const std::string& needle) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && has(readFile(it->path()), needle)) return true;
    }
    return false;
}

// 1.37.0 : ... ailleurs que dans ce sous-dossier (le code garde : codes-activation/).
bool foundInFilesOutside(const fs::path& dir, const std::string& needle, const std::string& keptIn) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->path().parent_path().filename() == keptIn) continue;
        if (it->is_regular_file(ec) && has(readFile(it->path()), needle)) return true;
    }
    return false;
}

// ---- l'editeur : un dossier, sa cle (d'essai), le registre d'exemple signe et range ----
struct Editor {
    fs::path                         folder;
    std::unique_ptr<lic::SigningKey> key;
    std::string                      registry;
    Editor(const fs::path& f, const std::string& source, bool test = true) : folder(f) {
        std::string why;
        key = la::createPublisherKey(U(f), test ? "Editeur d'essai (service)" : "Editeur d'essai (kit)", test, &why);
        if (!key) {
            std::printf("  (cle : %s)\n", why.c_str());
            return;
        }
        const std::string input = readFile(fs::path(source) / "tests" / "fixtures" / "licences" / "registre-exemple.json");
        if (!la::signRegistry(*key, input, lic::systemNow(), registry, &why)) std::printf("  (registre : %s)\n", why.c_str());
        std::string what;
        if (!ar::archiveSigned(U(f), registry, ar::trustedKeys(key.get()), &what, &why)) std::printf("  (archive : %s)\n", why.c_str());
    }
    [[nodiscard]] on::Options options() const {
        on::Options o;
        o.now = lic::systemNow();
        o.tool = "admin_service_test";
        return o;
    }
    // Signer une licence (en ligne si `service` est donne ; liee aux `requests` sinon) et la ranger.
    bool licence(const std::string& id, const std::string& type, int devices, const std::string& service, const std::vector<std::string>& requests,
                 std::string& out, std::string* why) {
        json::Value p = json::Value::object();
        p.set("license_id", id);
        p.set("customer", std::string("Client d'essai ") + id);
        p.set("edition", "PROFESSIONAL");
        p.set("type", type);
        p.set("max_devices", devices);
        if (type != "perpetual") p.set("duration_days", 365);
        la::LicenseInputs in;
        in.params = json::write(p);
        in.registry = registry;
        in.service = service;
        in.requests = requests;
        std::string summary;
        if (!la::signLicense(*key, in, lic::systemNow(), out, why, &summary)) return false;
        std::string what;
        return ar::archiveSigned(U(folder), out, ar::trustedKeys(key.get()), &what, why);
    }
};

// ---- un PC d'essai : sa cle, ses demandes, ses appels signes ----
struct Pc {
    std::unique_ptr<lic::SigningKey> key;
    std::string                      name;
    Pc(const fs::path& dir, const std::string& n) : name(n) {
        std::string why;
        key = lic::SigningKey::create(U(dir / "identite-pc.cle"), lic::deviceKeySpec(), n, false, &why);
        if (!key) std::printf("  (cle du PC : %s)\n", why.c_str());
    }
    [[nodiscard]] std::string device() const { return sv::deviceId(key->publicKey()); }
    // L'empreinte de << son >> installation de Windows (le service la demande : activer, renouveler, jeton).
    [[nodiscard]] std::string station() const { return lic::stationHash("machine d'essai " + name, key->publicKey()); }
    // Le corps de /v1/renouveler, ou d'un jeton (licence non vide), avec la station de ce PC.
    [[nodiscard]] std::string body(const std::string& licence = {}) const {
        json::Value v = json::Value::object();
        if (!licence.empty()) v.set("licence", licence);
        v.set("station", station());
        return json::write(v, false);
    }
    // Une demande d'activation signee par ce PC.
    [[nodiscard]] std::string request(const std::string& licenseId, lic::Seconds at, std::string* nonce = nullptr) const {
        lic::ActivationRequest r;
        r.deviceKey = key->publicKeyHex();
        r.deviceName = name;
        r.licenseId = licenseId;
        r.software = "IHM Studio 1.35.0 (essai)";
        r.createdAt = at;
        r.nonce = hex32();
        r.station = station();
        if (nonce) *nonce = r.nonce;
        const auto b = lic::signBlock(lic::DocKind::Request, lic::writeRequest(r), key->publicKey(), [this](std::string_view m) { return key->sign(m); });
        return lic::writeBlock(b);
    }
};

// La demande manuelle (scellee pour l'editeur).
std::string sealedRequest(const lic::SigningKey& editor, const std::string& requestText, const std::string& customer) {
    gw::OnlineRequest o;
    o.request = requestText;
    o.customer = customer;
    o.contact = "essai@exemple.invalid";
    o.message = "Merci (essai)";
    return gw::sealFor(editor.publicKey(), gw::writeOnlineRequest(o));
}

struct Reply {
    int         status{0};
    std::string body;
    json::Value value;
};

// Un appel au service (signe par `key` si donnee, a l'heure du service).
Reply call(const std::string& base, const std::string& method, const std::string& path, const std::string& body, const lic::SigningKey* key) {
    lic::Seconds now = lic::systemNow();
    if (key) {   // l'heure du service (son horloge peut etre avancee : restart)
        auto c = app::ai::makeHttpClient();
        app::ai::HttpRequest q;
        q.method = "GET";
        q.url = base + "/v1/etat";
        std::string b;
        app::ai::HttpSink s;
        s.onData = [&b](std::string_view x) { b.append(x); };
        (void)c->perform(q, s);
        json::Value v;
        if (json::parse(b, v, nullptr) && v.isObject()) now = static_cast<lic::Seconds>(v.integer("heure", now));
    }
    auto client = app::ai::makeHttpClient();
    app::ai::HttpRequest req;
    req.method = method;
    req.url = base + path;
    req.body = body;
    req.connectTimeoutSeconds = 10;
    req.idleTimeoutSeconds = 30;
    if (!body.empty()) req.headers.emplace_back("Content-Type", "application/json");
    if (key)
        for (auto& h : sv::signRequest(method, path, body, key->publicKey(), [key](std::string_view m) { return key->sign(m); }, now)) req.headers.push_back(h);
    Reply out;
    app::ai::HttpSink sink;
    sink.onData = [&out](std::string_view chunk) { out.body.append(chunk.data(), chunk.size()); };
    const auto r = client->perform(req, sink);
    out.status = r.status;
    (void)json::parse(out.body, out.value, nullptr);
    return out;
}

std::string activateBody(const std::string& code, const std::string& request, const std::string& name) {
    json::Value b = json::Value::object();
    b.set("code", code);
    b.set("demande", request);
    b.set("nom", name);
    return json::write(b, false);
}

// L'etat << poste >> d'un PC (GET /v1/poste) : le bloc signe par l'editeur, son contenu ouvert par le PC.
bool readPosteState(const std::string& base, const Pc& pc, const lic::SigningKey& editor, gw::DeviceStatus& st, gw::StatusContent& content) {
    const Reply r = call(base, "GET", "/v1/poste", {}, pc.key.get());
    if (r.status != 200 || !r.value.isObject() || !r.value.find("poste") || !r.value.at("poste").isString()) return false;
    std::vector<lic::SignedBlock> blocks;
    if (!lic::readBlocks(r.value.at("poste").asString(), blocks, nullptr) || blocks.size() != 1 || blocks[0].kind != lic::DocKind::DeviceStatus) return false;
    if (!lic::verifyBlock(blocks[0], editor.publicKey())) return false;
    if (!gw::parseDeviceStatus(blocks[0].document, st, nullptr)) return false;
    content = gw::StatusContent{};
    if (st.sealed.empty()) return true;
    std::string plain;
    return gw::openWith(*pc.key, st.sealed, plain, nullptr) && gw::parseStatusContent(plain, content, nullptr);
}

json::Value demande(int number, const Pc& pc, const std::string& keyId, const std::string& sealed, const std::string& state) {
    json::Value d = json::Value::object();
    d.set("numero", number);
    d.set("poste", pc.device());
    d.set("cle", keyId);
    d.set("scelle", sealed);
    d.set("etat", state);
    d.set("cree", lic::isoTime(lic::systemNow()));
    d.set("maj", lic::isoTime(lic::systemNow()));
    return d;
}

const on::Entry* entry(const std::vector<on::Entry>& all, int number) {
    for (const auto& e : all)
        if (e.number == number) return &e;
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : std::string(".");
    const std::string console = argc > 2 ? argv[2] : std::string();
    if (!lic::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    std::mt19937_64 rng(std::random_device{}());
    const fs::path root = fs::temp_directory_path() / ("xpg-admin-service-" + std::to_string(rng() % 1000000000ULL));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(p, e);
        }
    } cleanup{root};

    Editor ed(root / "editeur-a", source);
    check(ed.key && !ed.registry.empty(), "l'editeur d'essai : sa cle, le registre signe");
    if (!ed.key) return 1;

    // ======================================================================== sans reseau
    std::printf("-- la cle de synchro, son code de secours, le chiffrement\n");
    {
        ar::SyncKey k{}, back{};
        std::string why;
        check(ar::createSyncKey(U(ed.folder), k, &why), "la cle de synchro creee (" + why + ")");
        check(!ar::createSyncKey(U(ed.folder), back, &why), "une seconde : refusee (jamais remplacee)");
        const std::string code = ar::recoveryCode(k);
        check(code.rfind("XPGS1-", 0) == 0, "le code de secours : XPGS1-...");
        check(ar::parseRecoveryCode(code, back, &why) && back == k, "le code redonne la cle");
        std::string typo = code;
        typo[8] = typo[8] == 'A' ? 'B' : 'A';
        check(!ar::parseRecoveryCode(typo, back, &why), "une faute de frappe : refusee");
        const std::string sealed = ar::seal(k, "licences/X.xpglic.chiffre", "contenu d'essai");
        std::string plain;
        check(ar::openSealed(k, "licences/X.xpglic.chiffre", sealed, plain, &why) && plain == "contenu d'essai", "chiffre, dechiffre");
        check(!ar::openSealed(k, "licences/Y.xpglic.chiffre", sealed, plain, &why), "deplace (un autre chemin) : ne se dechiffre plus");
        ar::SyncKey other{};
        lic::crypto::randomBytes(other.data(), other.size());
        check(!ar::openSealed(other, "licences/X.xpglic.chiffre", sealed, plain, &why) && has(why, "autre cl"), "une autre cle de synchro : refusee");
        check(!has(sealed, "contenu"), "le texte chiffre ne montre rien");
        check(ar::missingLines("a\nb\n", "b\nc\na\nd") == std::vector<std::string>{"c", "d"}, "les lignes qui manquent (registre des activations)");
        lic::crypto::wipe(k.data(), k.size());
    }

    std::printf("-- l'adresse du service\n");
    {
        const std::string f = U(root / "adresses");
        std::string why;
        on::Config c;
        c.url = "https://xpg-licences.essai.workers.dev/";
        check(on::writeConfig(f, c, &why), "https://... : acceptee (le / final retire)");
        on::Config back;
        check(on::readConfig(f, back, &why) && back.url == "https://xpg-licences.essai.workers.dev", "relue sans le / final");
        c.url = "HTTPS://XPG-Licences.Essai.Workers.dev";
        check(on::writeConfig(f, c, &why) && on::readConfig(f, back, &why) && back.url == "https://xpg-licences.essai.workers.dev", "en minuscules");
        c.url = "http://127.0.0.1:8787";
        check(on::writeConfig(f, c, &why), "http://127.0.0.1 (les essais) : acceptee");
        for (const char* bad : {"http://exemple.invalid", "https://exemple.invalid/chemin", "ftp://x", "", "https://a@b"}) {
            c.url = bad;
            check(!on::writeConfig(f, c, &why), std::string("refusee : ") + bad);
        }
    }

    std::printf("-- une licence EN LIGNE signee sans PC\n");
    const std::string fakeUrl = "https://xpg-licences.essai.workers.dev";
    {
        std::string out, why;
        check(ed.licence("XPG-ESSAI-HORS-1", "temporary", 3, fakeUrl, {}, out, &why), "signee (" + why + ")");
        std::vector<lic::SignedBlock> blocks;
        lic::License l;
        check(lic::readBlocks(out, blocks, nullptr) && lic::parseLicense(blocks[0].document, l, &why) && l.service == fakeUrl && l.devices.empty() && l.maxDevices == 3,
              "elle nomme le service, ne lie aucun PC");
        Pc pc(root / "pc-hors", "PC d'essai hors ligne");
        check(!ed.licence("XPG-ESSAI-HORS-2", "temporary", 1, fakeUrl, {pc.request({}, lic::systemNow())}, out, &why) && has(why, "aucun PC"),
              "une demande de PC : refusee (" + why + ")");
        check(!ed.licence("XPG-ESSAI-HORS-3", "floating", 2, {}, {pc.request({}, lic::systemNow())}, out, &why) && has(why, "EN LIGNE"),
              "flottante sans service : refusee");
        check(ed.licence("XPG-ESSAI-HORS-4", "floating", 2, fakeUrl, {}, out, &why), "flottante en ligne : signee (" + why + ")");
        std::vector<la::ActivationRecord> recs;
        (void)la::readActivations(U(ed.folder), recs, nullptr, &why);
        check(true, "le registre des activations (une ligne par licence en ligne, sans PC)");
    }

    std::printf("-- les demandes relevees (sans reseau : processRequests)\n");
    {
        const fs::path f = root / "releve";
        Editor e2(f, source);
        Pc a(root / "pc-a", "Poste essai A"), b(root / "pc-b", "Poste essai B");
        const lic::Seconds t0 = lic::systemNow() - 100;
        std::string nonceA;
        const std::string reqA = a.request({}, t0, &nonceA);
        const std::string sealedA = sealedRequest(*e2.key, reqA, "Client d'essai A");
        json::Value list = json::Value::object();
        json::Value arr = json::Value::array();
        arr.push(demande(1, a, e2.key->keyId(), sealedA, "attente"));
        arr.push(demande(2, b, "xpg-0000000000000000", sealedRequest(*e2.key, b.request({}, t0), "Client d'essai B"), "attente"));
        // une demande dont le poste n'est pas celui de la cle du PC
        arr.push(demande(3, b, e2.key->keyId(), sealedRequest(*e2.key, a.request({}, t0 + 1), "Client d'essai C"), "attente"));
        list.set("demandes", arr);
        on::Result r = on::processRequests(U(f), *e2.key, list, "https://xpg-licences.essai.workers.dev", e2.options());
        auto all = on::listRequests(U(f));
        check(r.ok && all.size() == 3, "trois demandes rangees (" + r.text() + ")");
        const on::Entry* e1 = entry(all, 1);
        check(e1 && e1->waiting() && e1->customer == "Client d'essai A" && e1->deviceName == "Poste essai A" && e1->nonce == nonceA && !e1->requestPath.empty() &&
                  fs::is_regular_file(e1->requestPath),
              "la demande ouverte : le client, le PC, le .xpgreq range");
        check(entry(all, 2) && entry(all, 2)->state == on::RequestState::Unreadable && has(entry(all, 2)->problems.front(), "autre cl"),
              "scellee pour une autre cle : illisible");
        check(entry(all, 3) && entry(all, 3)->state == on::RequestState::Unreadable, "le poste n'est pas celui de la cle du PC : illisible");
        check(on::readFetchState(U(f)).pending == 1, "une en attente");
        // la meme demande (meme nonce) sous un autre numero : rejouee ; une plus ancienne : rejouee ; le sceau change : note
        json::Value arr2 = json::Value::array();
        arr2.push(demande(1, a, e2.key->keyId(), sealedRequest(*e2.key, reqA, "Client d'essai A (modifie)"), "attente"));
        arr2.push(demande(4, a, e2.key->keyId(), sealedA, "attente"));
        arr2.push(demande(5, a, e2.key->keyId(), sealedRequest(*e2.key, a.request({}, t0 - 50), "Client d'essai A"), "attente"));
        json::Value list2 = json::Value::object();
        list2.set("demandes", arr2);
        r = on::processRequests(U(f), *e2.key, list2, "https://xpg-licences.essai.workers.dev", e2.options());
        all = on::listRequests(U(f));
        check(entry(all, 4) && entry(all, 4)->state == on::RequestState::Ignored, "la meme demande sous un autre numero : rejouee, ignoree");
        check(entry(all, 5) && entry(all, 5)->state == on::RequestState::Ignored && has(entry(all, 5)->problems.front(), "pas plus r"),
              "une demande plus ancienne que la derniere de ce PC : ignoree");
        check(entry(all, 1) && entry(all, 1)->customer == "Client d'essai A" && !entry(all, 1)->problems.empty() && has(entry(all, 1)->problems.front(), "sceau"),
              "un sceau change apres coup : garde son contenu d'origine, le probleme note");
        check(has(r.text(), "rejou"), "le releve dit les rejouees");
        // une demande plus recente du meme PC : l'ancienne est remplacee ; une demande d'ajout a une licence : a verifier
        json::Value arr3 = json::Value::array();
        arr3.push(demande(6, a, e2.key->keyId(), sealedRequest(*e2.key, a.request("XPG-2026-00001", t0 + 50), "Client d'essai A"), "attente"));
        arr3.push(demande(1, a, e2.key->keyId(), sealedA, "remplacee"));
        json::Value list3 = json::Value::object();
        list3.set("demandes", arr3);
        r = on::processRequests(U(f), *e2.key, list3, "https://xpg-licences.essai.workers.dev", e2.options());
        all = on::listRequests(U(f));
        check(entry(all, 1) && entry(all, 1)->state == on::RequestState::Replaced, "l'ancienne : remplacee");
        check(entry(all, 6) && entry(all, 6)->waiting() && entry(all, 6)->joinRequest() && has(on::joinWarning(*entry(all, 6)), "confirme avec ton client"),
              "une demande d'ajout a une licence : a verifier");
        on::Entry w;
        std::string why;
        check(on::findWaiting(U(f), "6", w, &why) && w.number == 6, "trouvee par son numero");
        check(on::findWaiting(U(f), a.device().substr(0, 8), w, &why) && w.number == 6, "trouvee par le debut de l'identifiant du poste");
        check(!on::findWaiting(U(f), "4", w, &why), "une ignoree n'attend pas de decision");
        check(on::isOnlineRequestPath(U(f), w.requestPath) && !on::isOnlineRequestPath(U(f), U(root / "x.xpgreq")), "un .xpgreq releve se reconnait");
    }

    std::printf("-- le kit des Studios\n");
    {
        Editor kit(root / "editeur-kit", source, false);
        const std::string out = U(root / "kit");
        on::Config c;
        c.url = "https://xpg-licences.essai.workers.dev";
        std::string why;
        check(on::writeConfig(U(kit.folder), c, &why), "l'adresse reglee");
        const on::Result r = on::studioKit(U(kit.folder), kit.key.get(), out, lic::systemNow());
        check(r.ok, "les fichiers ecrits (" + r.text() + ")");
        const std::string addr = readFile(fs::path(out) / "ServiceAddress.hpp");
        check(has(addr, "kCompiledServiceUrl = \"https://xpg-licences.essai.workers.dev\";") && has(addr, "namespace licensing::service"),
              "ServiceAddress.hpp : l'adresse, la forme du fichier livre");
        const std::string pub = readFile(fs::path(out) / "cle-publique.txt");
        check(has(pub, kit.key->publicKeyHex()) && pub.find("PRIV") == std::string::npos, "cle-publique.txt : la cle publique en hexadecimal");
        check(readFile(fs::path(out) / "registre.xpgreg") == kit.registry && has(readFile(fs::path(out) / "PublisherKey.hpp"), kit.key->keyId()),
              "PublisherKey.hpp et registre.xpgreg");
        check(has(readFile(fs::path(out) / "LISEZ-MOI.txt"), "\r\n"), "LISEZ-MOI.txt (CRLF)");
        check(!on::studioKit(U(ed.folder), ed.key.get(), U(root / "kit2"), lic::systemNow()).ok, "une cle d'ESSAI : refusee");
        c.url = "http://127.0.0.1:9";
        (void)on::writeConfig(U(kit.folder), c, &why);
        check(!on::studioKit(U(kit.folder), kit.key.get(), U(root / "kit3"), lic::systemNow()).ok, "un service d'essai (http://) : refuse pour les Studios livres");
    }

    // ======================================================================== le service local
    std::printf("-- le service local (le vrai Worker)\n");
    Editor other(root / "autre-editeur", source);
    xpg_test::LocalService::Options so;
    so.sourceDir = source;
    xpg_test::LocalService svc({ed.key->publicKeyHex()}, so);
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
        return g_failures ? 1 : 0;
    }
    check(svc.ready(), "le service demarre (" + svc.error() + ")");
    if (!svc.ready()) {
        std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
        return 1;
    }
    const std::string url = svc.url();
    const std::string A = U(ed.folder);
    {
        on::Config c;
        c.url = url;
        std::string why;
        check(on::writeConfig(A, c, &why) && on::writeConfig(U(other.folder), c, &why), "l'adresse locale reglee");
    }

    std::printf("-- verifier, une autre cle refusee, certifier\n");
    {
        on::ServiceState st;
        on::Result r = on::verify(A, *ed.key, ed.options(), &st);
        check(r.ok && st.reached && st.adminOk && st.serviceKey.size() == 64 && !st.certOk && !st.certified, "le service repond, ta cle acceptee, pas de certificat (" + r.text() + ")");
        r = on::verify(U(other.folder), *other.key, other.options(), &st);
        check(!r.ok && has(r.message, "PUBLISHER_KEYS"), "une cle qui n'est pas dans PUBLISHER_KEYS : refusee (403)");
        std::string serviceKey;
        r = on::readServiceKey(A, *ed.key, ed.options(), serviceKey);
        check(r.ok && lic::isKeyHex(serviceKey) && !on::keyFingerprint(serviceKey).empty(), "la cle du service lue, son empreinte");
        r = on::certify(A, *ed.key, ed.options(), std::string(64, 'a'));
        check(!r.ok && has(r.message, "a chang"), "une autre cle que celle montree : rien n'est signe");
        r = on::certify(A, *ed.key, ed.options(), serviceKey);
        check(r.ok, "certifiee (" + r.text() + ")");
        r = on::verify(A, *ed.key, ed.options(), &st);
        check(r.ok && st.certOk && st.certified && st.certUrl == url && st.certExpires > lic::systemNow() + 360 * 86400 && !st.needsRenewal(lic::systemNow()),
              "le certificat : valable un an, pour cette adresse");
        const Reply cert = call(url, "GET", "/v1/certificat", {}, nullptr);
        sv::ServiceCert sc;
        std::string why;
        check(cert.status == 200 && sv::verifyCert(cert.body, {ed.key->publicKey()}, lic::systemNow(), sc, &why) && sc.key == serviceKey && sc.grants(sv::kRightFloating),
              "GET /v1/certificat : signe par l'editeur, les quatre droits");
    }

    std::printf("-- le registre\n");
    {
        on::Result r = on::publishRegistry(A, *ed.key, ed.options());
        check(r.ok && has(r.message, "318"), "depose (" + r.text() + ")");
        const Reply e = call(url, "GET", "/v1/etat", {}, nullptr);
        check(e.value.integer("registre") == 318, "/v1/etat : registre 318");
        r = on::publishRegistry(A, *ed.key, ed.options());
        check(r.ok && has(r.message, "d\xC3\xA9j\xC3\xA0 \xC3\xA0 jour"), "deja a jour : rien n'est redepose");
    }

    std::printf("-- une licence en ligne et son code ; un PC s'active\n");
    Pc pc1(root / "pc-1", "PC d'essai 1"), pc2(root / "pc-2", "PC d'essai 2"), pc3(root / "pc-3", "PC d'essai 3"), pc6(root / "pc-6", "PC d'essai 6");
    std::string code1;
    {
        std::string out, why;
        check(ed.licence("XPG-ESSAI-EN-LIGNE-1", "temporary", 2, url, {}, out, &why), "signee (" + why + ")");
        code1 = sv::newActivationCode();
        std::string norm;
        check(sv::normalizeCode(code1, norm), "un code neuf : " + std::string(code1.size() == 23 ? "XPG-XXXX-XXXX-XXXX-XXXX" : "?"));
        on::Result r = on::putLicence(A, *ed.key, out, sv::codeHash(norm), 30, true, ed.options());
        check(r.ok, "deposee avec l'empreinte du code (" + r.text() + ")");
        check(!foundInFiles(ed.folder, code1) && !foundInFiles(ed.folder, norm), "le code n'est dans aucun fichier du poste");
        // le PC s'active : un appel signe par sa cle
        Reply a = call(url, "POST", "/v1/activer", activateBody(code1, pc1.request("XPG-ESSAI-EN-LIGNE-1", lic::systemNow()), pc1.name), pc1.key.get());
        check(a.status == 200 && a.value.isObject(), "POST /v1/activer : 200 (" + a.body.substr(0, 200) + ")");
        sv::ServiceCert cert;
        lic::SignedBlock blk;
        sv::Activation act;
        check(sv::verifyCert(a.value.str("certificat"), {ed.key->publicKey()}, lic::systemNow(), cert, &why) &&
                  sv::findBlock(a.value.str("activation"), lic::DocKind::Activation, blk, &why) &&
                  sv::verifyServiceBlock(blk, lic::DocKind::Activation, cert, sv::kRightActivation, &why) && sv::parseActivation(blk.document, act, &why) &&
                  act.licenseId == "XPG-ESSAI-EN-LIGNE-1" && act.deviceKey == pc1.key->publicKeyHex() && act.until > lic::systemNow() + 29 * 86400,
              "l'activation : signee par la cle du service certifiee, pour ce PC, bail de 30 jours (" + why + ")");
        a = call(url, "POST", "/v1/activer", activateBody("XPG-0000-0000-0000-0000", pc2.request({}, lic::systemNow()), pc2.name), pc2.key.get());
        check(a.status == 403 && a.value.str("erreur") == "code", "un faux code : 403 code");
        a = call(url, "POST", "/v1/activer", activateBody(code1, pc6.request({}, lic::systemNow()), pc6.name), pc6.key.get());
        check(a.status == 200, "un second PC : active (2 au plus)");
        a = call(url, "POST", "/v1/activer", activateBody(code1, pc3.request({}, lic::systemNow()), pc3.name), pc3.key.get());
        check(a.status == 409 && a.value.str("erreur") == "complet", "un troisieme : 409 complet");
        on::Snapshot s;
        r = on::listLicences(A, *ed.key, ed.options(), s);
        const on::OnlineLicence* l = s.licence("XPG-ESSAI-EN-LIGNE-1");
        check(r.ok && l && l->seats == 2 && l->leaseDays == 30 && l->active && l->hasCode && l->activeCount() == 2, "la liste : la licence, 2 PC actives (" + r.text() + ")");
        const on::OnlineActivation* x = l ? l->activation(pc1.key->publicKeyHex()) : nullptr;
        check(x && x->name == "PC d'essai 1" && x->state == "active" && x->until > lic::systemNow() && x->seenAt > 0, "le PC : son nom, vu le, bail jusqu'au");
        const on::Snapshot back = on::readSnapshot(A);
        check(back.at > 0 && back.url == url && back.licence("XPG-ESSAI-EN-LIGNE-1") && back.devices() == 2, "le releve garde sur ce poste (service-releve.json)");
        check(!foundInFiles(ed.folder, code1), "le code n'est toujours dans aucun fichier");
        // retirer un PC
        r = on::retireDevice(A, *ed.key, "XPG-ESSAI-EN-LIGNE-1", pc1.key->publicKeyHex(), ed.options());
        check(r.ok, "retirer ce PC (" + r.text() + ")");
        r = on::listLicences(A, *ed.key, ed.options(), s);
        l = s.licence("XPG-ESSAI-EN-LIGNE-1");
        check(l && l->activation(pc1.key->publicKeyHex()) && l->activation(pc1.key->publicKeyHex())->state == "retiree" && l->activeCount() == 1, "la liste : retire");
        const Reply ren = call(url, "POST", "/v1/renouveler", pc1.body(), pc1.key.get());
        bool retired = false;
        if (const json::Value* rr = ren.value.find("retirees"); rr && rr->isArray())
            for (const auto& v : rr->items()) retired = retired || v.asString() == "XPG-ESSAI-EN-LIGNE-1";
        check(ren.status == 200 && retired, "le PC retire, a sa connexion : sa licence dans << retirees >>");
        a = call(url, "POST", "/v1/activer", activateBody(code1, pc3.request({}, lic::systemNow()), pc3.name), pc3.key.get());
        check(a.status == 200, "la place liberee : un autre PC s'active");
        r = on::retireDevice(A, *ed.key, "XPG-ESSAI-EN-LIGNE-1", std::string(64, 'b'), ed.options());
        check(!r.ok, "retirer un PC inconnu : refuse");
        // desactiver, reactiver
        r = on::setActive(A, *ed.key, "XPG-ESSAI-EN-LIGNE-1", false, 30, ed.options());
        check(r.ok, "desactivee (" + r.text() + ")");
        a = call(url, "POST", "/v1/activer", activateBody(code1, pc2.request({}, lic::systemNow()), pc2.name), pc2.key.get());
        check(a.status == 403, "desactivee : le code ne marche plus");
        r = on::listLicences(A, *ed.key, ed.options(), s);
        check(s.licence("XPG-ESSAI-EN-LIGNE-1") && !s.licence("XPG-ESSAI-EN-LIGNE-1")->active && s.licence("XPG-ESSAI-EN-LIGNE-1")->hasCode,
              "la liste : desactivee, le code garde");
        r = on::setActive(A, *ed.key, "XPG-ESSAI-EN-LIGNE-1", true, 30, ed.options());
        check(r.ok, "reactivee");
        // une licence liee a des PC n'est pas une licence en ligne
        std::string bound;
        check(ed.licence("XPG-ESSAI-LIEE-0", "temporary", 1, {}, {pc2.request({}, lic::systemNow())}, bound, &why), "une licence liee signee");
        r = on::putLicence(A, *ed.key, bound, {}, 30, true, ed.options());
        check(!r.ok && has(r.message, "pas une licence en ligne"), "deposer une licence liee comme en ligne : refuse");
    }

    std::printf("-- une licence flottante et ses jetons\n");
    {
        std::string out, why, norm;
        check(ed.licence("XPG-ESSAI-FLOTTANTE-1", "floating", 1, url, {}, out, &why), "signee (" + why + ")");
        const std::string code = sv::newActivationCode();
        (void)sv::normalizeCode(code, norm);
        check(on::putLicence(A, *ed.key, out, sv::codeHash(norm), 30, true, ed.options()).ok, "deposee");
        Pc f1(root / "pc-f1", "PC flottant 1"), f2(root / "pc-f2", "PC flottant 2");
        check(call(url, "POST", "/v1/activer", activateBody(code, f1.request({}, lic::systemNow()), f1.name), f1.key.get()).status == 200 &&
                  call(url, "POST", "/v1/activer", activateBody(code, f2.request({}, lic::systemNow()), f2.name), f2.key.get()).status == 200,
              "deux PC actives (une flottante n'a pas de limite d'activations)");
        const Reply j1 = call(url, "POST", "/v1/flottante/jeton", f1.body("XPG-ESSAI-FLOTTANTE-1"), f1.key.get());
        check(j1.status == 200 && j1.value.isObject() && !j1.value.str("jeton").empty(), "le premier prend le jeton");
        const Reply j2 = call(url, "POST", "/v1/flottante/jeton", f2.body("XPG-ESSAI-FLOTTANTE-1"), f2.key.get());
        check(j2.status == 409 && j2.value.str("erreur") == "complet", "le second : 409 complet (un poste en meme temps)");
        on::Snapshot s;
        (void)on::listLicences(A, *ed.key, ed.options(), s);
        const on::OnlineLicence* l = s.licence("XPG-ESSAI-FLOTTANTE-1");
        check(l && l->floating() && l->seats == 1 && l->leases.size() == 1 && l->leases[0].deviceKey == f1.key->publicKeyHex() && l->leases[0].until > lic::systemNow(),
              "la liste : le jeton en cours, son PC, jusqu'a quand");
        check(s.leases() == 1, "le compte des jetons");
        check(on::retireDevice(A, *ed.key, "XPG-ESSAI-FLOTTANTE-1", f1.key->publicKeyHex(), ed.options()).ok, "retirer le PC qui tient le jeton");
        (void)on::listLicences(A, *ed.key, ed.options(), s);
        check(s.licence("XPG-ESSAI-FLOTTANTE-1") && s.licence("XPG-ESSAI-FLOTTANTE-1")->leases.empty(), "son jeton est rendu");
        check(call(url, "POST", "/v1/flottante/jeton", f2.body("XPG-ESSAI-FLOTTANTE-1"), f2.key.get()).status == 200, "l'autre PC le prend");
    }

    std::printf("-- les demandes manuelles : refusee, acceptee, rejouee\n");
    Pc pc4(root / "pc-4", "PC d'essai 4"), pc5(root / "pc-5", "PC d'essai 5");
    {
        std::string why;
        json::Value b = json::Value::object();
        b.set("cle", ed.key->keyId());
        b.set("scelle", sealedRequest(*ed.key, pc4.request({}, lic::systemNow()), "Client d'essai refuse"));
        Reply d = call(url, "POST", "/v1/demandes", json::write(b, false), pc4.key.get());
        check(d.status == 201 && d.value.integer("numero") > 0, "un PC depose sa demande (201)");
        const int n4 = static_cast<int>(d.value.integer("numero"));
        on::Result r = on::fetchRequests(A, *ed.key, ed.options());
        auto all = on::listRequests(A);
        check(r.ok && entry(all, n4) && entry(all, n4)->waiting() && entry(all, n4)->customer == "Client d'essai refuse", "relevee : en attente (" + r.text() + ")");
        r = on::refuse(A, *ed.key, std::to_string(n4), "Pas de contrat d'essai pour ce poste", ed.options());
        check(r.ok, "refusee (" + r.text() + ")");
        gw::DeviceStatus st;
        gw::StatusContent content;
        check(readPosteState(url, pc4, *ed.key, st, content) && st.state == gw::State::Refused && content.message == "Pas de contrat d'essai pour ce poste",
              "le PC lit << refusee >> et la raison (scellee pour lui, signee par l'editeur)");
        all = on::listRequests(A);
        check(entry(all, n4) && entry(all, n4)->state == on::RequestState::Refused, "sur ce poste : refusee");
        r = on::fetchRequests(A, *ed.key, ed.options());
        check(r.ok && entry(on::listRequests(A), n4)->state == on::RequestState::Refused, "un nouveau releve ne la remet pas en attente");
        // acceptee : une licence liee a ce PC, publiee a lui
        std::string nonce5;
        const std::string req5 = pc5.request({}, lic::systemNow() - 10, &nonce5);
        const std::string sealed5 = sealedRequest(*ed.key, req5, "Client d'essai accepte");
        b.set("scelle", sealed5);
        d = call(url, "POST", "/v1/demandes", json::write(b, false), pc5.key.get());
        const int n5 = static_cast<int>(d.value.integer("numero"));
        check(d.status == 201, "une seconde demande");
        (void)on::fetchRequests(A, *ed.key, ed.options());
        all = on::listRequests(A);
        const on::Entry* e5 = entry(all, n5);
        check(e5 && e5->waiting() && fs::is_regular_file(e5->requestPath), "relevee, son .xpgreq range");
        std::string lic5;
        check(e5 && ed.licence("XPG-ESSAI-LIEE-5", "temporary", 1, {}, {readFile(e5->requestPath)}, lic5, &why), "la licence liee a ce PC signee (" + why + ")");
        r = on::publishBoundLicence(A, *ed.key, lic5, false, ed.options());
        check(r.ok && has(r.message, "1 demande accept"), "publiee a son PC, la demande acceptee (" + r.text() + ")");
        check(readPosteState(url, pc5, *ed.key, st, content) && st.state == gw::State::Accepted && st.requestNonce == nonce5 && content.license == lic5,
              "le PC lit << acceptee >> et SA licence (scellee pour lui)");
        const std::uint64_t number5 = st.number;
        on::Client c(url, *ed.key, ed.options());
        check(c.connect(&why), "le client du service");
        const on::Reply list = c.call("GET", "/admin/demandes?depuis=" + std::to_string(n5 - 1));
        bool treated = false;
        if (const json::Value* arr = list.json.find("demandes"); arr && arr->isArray())
            for (const auto& x : arr->items()) treated = treated || (x.integer("numero") == n5 && x.str("etat") == "traitee");
        check(treated, "sur le service : traitee");
        // rejouee : le meme sceau redepose par ce PC (une ancienne demande) : ignoree, rien n'est publie
        b.set("scelle", sealed5);
        d = call(url, "POST", "/v1/demandes", json::write(b, false), pc5.key.get());
        const int n5b = static_cast<int>(d.value.integer("numero"));
        r = on::fetchRequests(A, *ed.key, ed.options());
        all = on::listRequests(A);
        check(entry(all, n5b) && entry(all, n5b)->state == on::RequestState::Ignored, "la demande rejouee : ignoree");
        check(readPosteState(url, pc5, *ed.key, st, content) && st.number == number5 && st.state == gw::State::Accepted, "l'etat du PC n'a pas change");
        // une reponse EN LIGNE (sans code dans l'etat du PC)
        Pc pc7(root / "pc-7", "PC d'essai 7");
        b.set("scelle", sealedRequest(*ed.key, pc7.request({}, lic::systemNow()), "Client d'essai en ligne"));
        d = call(url, "POST", "/v1/demandes", json::write(b, false), pc7.key.get());
        const int n7 = static_cast<int>(d.value.integer("numero"));
        (void)on::fetchRequests(A, *ed.key, ed.options());
        // 1.37.0 : LE CODE part SCELLE pour CE PC (son Studio s'active seul), avec l'identifiant et l'etat du mail.
        int mailed = 0;
        std::string mailedCode, mailedTo;
        const on::Mailer failing = [&](const on::Entry& e, const lic::License& l, const std::string& code) {
            ++mailed;
            mailedCode = code;
            mailedTo = e.customer + " / " + l.licenseId;
            on::MailOutcome m;
            m.state = gw::kMailFailed;
            m.to = "client@exemple.fr";
            m.note = "la boite d'essai ne repond pas";
            return m;
        };
        r = on::answerOnline(A, *ed.key, n7, "XPG-ESSAI-EN-LIGNE-1", code1, {}, failing, ed.options());
        check(r.ok && mailed == 1 && mailedCode == code1 && has(mailedTo, "XPG-ESSAI-EN-LIGNE-1"), "acceptee par une licence en ligne, le mail tente (" + r.text() + ")");
        check(has(r.text(), "Mail : la boite d'essai ne repond pas"), "le resultat dit ce qu'est devenu le mail");
        check(readPosteState(url, pc7, *ed.key, st, content) && st.state == gw::State::Accepted && content.license.empty() && content.code == code1 &&
                  content.onlineLicense == "XPG-ESSAI-EN-LIGNE-1" && content.mail == gw::kMailFailed && content.mailTo == "client@exemple.fr" &&
                  !has(content.message, code1),
              "le PC lit << acceptee >> : le code scelle pour lui, la licence, le mail en echec (jamais le code dans le mot)");
        // Une seule acceptation : elle n'attend plus.
        r = on::answerOnline(A, *ed.key, n7, "XPG-ESSAI-EN-LIGNE-1", code1, {}, {}, ed.options());
        check(!r.ok && mailed == 1, "une seconde reponse a la meme demande est refusee (" + r.text() + ")");

        // 1.37.0 : << acceptee ici >> - la licence signee, la reponse pas encore partie : ni refus ni seconde decision.
        Pc pc8(root / "pc-ici", "PC d'essai ici");
        b.set("scelle", sealedRequest(*ed.key, pc8.request({}, lic::systemNow()), "Client d'essai ici"));
        d = call(url, "POST", "/v1/demandes", json::write(b, false), pc8.key.get());
        const int n8 = static_cast<int>(d.value.integer("numero"));
        (void)on::fetchRequests(A, *ed.key, ed.options());
        std::string w;
        check(on::markAcceptedHere(A, n8, "XPG-ESSAI-ICI-1", &w), "acceptee ici des la signature (" + w + ")");
        bool here = false;
        for (const auto& e : on::listRequests(A))
            if (e.number == n8) here = e.acceptedHere() && e.answer == "XPG-ESSAI-ICI-1" && e.waiting();
        check(here, "la demande dit << acceptee ici >> (la licence), toujours en attente de sa reponse");
        check(!on::markAcceptedHere(A, n8, "XPG-ESSAI-ICI-2", &w) && has(w, "XPG-ESSAI-ICI-1"), "une autre licence pour la meme demande : refusee (" + w + ")");
        r = on::refuse(A, *ed.key, std::to_string(n8), "trop tard", ed.options());
        check(!r.ok && has(r.text(), "d\xC3\xA9j\xC3\xA0 accept\xC3\xA9" "e ici") && has(r.text(), "Renvoyer la r\xC3\xA9ponse"), "le refus d'une demande acceptee ici : refuse (" + r.text() + ")");
        r = on::answerOnline(A, *ed.key, n8, "XPG-ESSAI-ICI-1", code1, {}, {}, ed.options());   // le renvoi
        bool done8 = false;
        for (const auto& e : on::listRequests(A))
            if (e.number == n8) done8 = e.state == on::RequestState::Accepted && e.answer == "XPG-ESSAI-ICI-1";
        check(r.ok && done8, "la reponse renvoyee : acceptee (" + r.text() + ")");
        check(readPosteState(url, pc8, *ed.key, st, content) && content.code == code1 && content.mail.empty(), "sans mail : l'etat ne dit rien du mail");

        // 1.37.0 : le code garde sur ce poste (Voir le code..., code-licence) ; illisible ailleurs (DPAPI sous Windows).
        check(on::storeCode(A, "XPG-ESSAI-EN-LIGNE-1", code1, &w) && on::hasCode(A, "XPG-ESSAI-EN-LIGNE-1"), "le code garde (" + w + ")");
        std::string back;
        check(on::readCode(A, "XPG-ESSAI-EN-LIGNE-1", back, &w) && back == code1, "le code relu tel quel");
        check(!on::readCode(A, "XPG-ESSAI-INCONNUE", back, &w) && back.empty() && has(w, "Nouveau code"), "une licence sans code garde : la phrase dit quoi faire (" + w + ")");
#ifdef _WIN32
        check(!foundInFiles(ed.folder, code1), "sous Windows : le code n'est en clair dans aucun fichier (DPAPI)");
#endif
        // 1.37.0 : le texte du mail d'acceptation.
        lic::License ml;
        ml.licenseId = "XPG-ESSAI-EN-LIGNE-1";
        ml.customer = "Client d'essai en ligne";
        ml.edition = "PROFESSIONAL";
        ml.maxDevices = 2;
        ml.expiresAt = lic::systemNow() + 365 * lic::kDay;
        on::Entry me;
        me.customer = "Client d'essai en ligne";
        me.contact = "Camille Essai";
        me.email = "client@exemple.fr";
        const on::MailText mt = on::acceptanceMail(ml, nullptr, me, code1, "\xC3\x89" "diteur d'essai");
        check(has(mt.subject, "XPG-ESSAI-EN-LIGNE-1") && has(mt.body, code1) && has(mt.body, "Activer avec un code") && has(mt.body, "\xC3\x89" "diteur d'essai") &&
                  has(mt.body, "PROFESSIONAL"),
              "le mail : la licence, son edition, le code et comment l'activer, la signature");
        const on::MailText mb = on::acceptanceMail(ml, nullptr, me, {}, {});
        check(!has(mb.body, "code d'activation") && has(mb.body, "Ton \xC3\xA9" "diteur"), "une licence liee : pas de code ; sans nom : << Ton editeur >>");
    }

    std::printf("-- la liste de revocation : privee\n");
    {
        std::string out, why, what;
        json::Value v = json::Value::object();
        v.set("number", 1);
        json::Value arr = json::Value::array();
        json::Value x = json::Value::object();
        x.set("license_id", "XPG-ESSAI-EN-LIGNE-1");
        x.set("reason", "Essai de revocation");
        arr.push(x);
        v.set("revoked", arr);
        check(la::signRevocations(*ed.key, json::write(v), lic::systemNow(), out, &why) && ar::archiveSigned(A, out, ar::trustedKeys(ed.key.get()), &what, &why),
              "signee et rangee");
        const on::Result r = on::publishRevocations(A, *ed.key, ed.options());
        check(r.ok, "deposee (" + r.text() + ")");
        check(call(url, "GET", "/v1/revocations", {}, nullptr).status >= 400, "aucune route publique ne la donne");
        // 1.35.0 (revue) : la liste ne sort jamais du service ; le PC concerne lit sa revocation dans le motif de son retrait.
        Reply ren = call(url, "POST", "/v1/renouveler", pc6.body(), pc6.key.get());
        const json::Value* motifs = ren.value.find("motifs");
        check(ren.status == 200 && !ren.value.find("revocations") && motifs && motifs->str("XPG-ESSAI-EN-LIGNE-1") == "revoquee" &&
                  !has(ren.body, "Essai de revocation"),
              "le PC concerne : sa licence retiree (motif revoquee), sans la liste ni sa raison");
        Pc f3(root / "pc-f3", "PC sans rapport");
        ren = call(url, "POST", "/v1/renouveler", f3.body(), f3.key.get());
        check(ren.status == 200 && !ren.value.find("revocations") && !has(ren.body, "XPG-LICENSING 1 revocations"), "un PC sans rapport : rien");
        on::Snapshot s;
        (void)on::listLicences(A, *ed.key, ed.options(), s);
        const auto* l = s.licence("XPG-ESSAI-EN-LIGNE-1");
        check(l && l->activation(pc6.key->publicKeyHex()) && l->activation(pc6.key->publicKeyHex())->state == "revoquee", "la liste : le PC revoque");
    }

    std::printf("-- les compteurs\n");
    {
        const on::Result r = on::stats(A, *ed.key, ed.options());
        check(r.ok && has(r.text(), "licences") && has(r.text(), "jetons"), "les compteurs du service (" + r.message + ")");
    }

    std::printf("-- la licence en ligne par l'application (Workspace) : le code montre une fois\n");
    {
        license_admin::gui::Workspace ws(A);
        check(ws.service().ready() && ws.service().syncKey, "le Workspace voit le service et la cle de synchro");
        license_admin::gui::LicenceDraft d;
        d.online = true;
        d.licenseId = "XPG-ESSAI-APPLI-1";
        d.customer = "Client d'essai application";
        d.edition = "PERSONAL";
        d.maxDevices = 1;
        d.durationDays = 90;
        d.leaseDays = 15;
        license_admin::gui::ServiceJob job;
        std::string code;
        license_admin::gui::OpResult r = ws.prepareOnlineLicence(d, {}, job, code);
        check(r.ok && !code.empty() && job.codeHash.size() == 64 && job.kind == license_admin::gui::ServiceJob::Kind::OnlineLicence, "preparee : la licence, un code (" + r.text() + ")");
        check(!foundInFiles(ed.folder, code), "le code n'est dans aucun fichier (avant le depot)");
        r = ws.serviceRun(job);
        check(r.ok, "deposee (" + r.text() + ")");
        Pc pc8(root / "pc-8", "PC d'essai 8");
        check(call(url, "POST", "/v1/activer", activateBody(code, pc8.request({}, lic::systemNow()), pc8.name), pc8.key.get()).status == 200, "un PC s'active avec ce code");
        check(!foundInFiles(ed.folder, code), "le code n'est dans aucun fichier (apres)");
        // un nouveau code : l'ancien ne marche plus
        ws.reload();
        license_admin::gui::ServiceJob j2;
        std::string code2;
        r = ws.prepareNewCode("XPG-ESSAI-APPLI-1", j2, code2);
        check(r.ok && code2 != code && ws.serviceRun(j2).ok, "un code neuf depose");
        Pc pc9(root / "pc-9", "PC d'essai 9");
        check(call(url, "POST", "/v1/activer", activateBody(code, pc9.request({}, lic::systemNow()), pc9.name), pc9.key.get()).status == 403, "l'ancien code : refuse");
        // prolonger : le meme code
        on::Snapshot s;
        (void)ws.serviceListLicences(s);
        ws.adoptSnapshot(s);
        license_admin::gui::LicenceDraft e = d;
        e.extend = true;
        e.durationDays = 400;
        e.maxDevices = 2;
        license_admin::gui::ServiceJob j3;
        std::string none;
        r = ws.prepareOnlineLicence(e, {}, j3, none);
        check(r.ok && none.empty() && j3.codeHash.empty() && j3.leaseDays == 0, "prolongee : pas de code neuf, le bail du service garde (" + r.text() + ")");
        check(ws.serviceRun(j3).ok, "la nouvelle emission deposee");
        (void)ws.serviceListLicences(s);
        check(s.licence("XPG-ESSAI-APPLI-1") && s.licence("XPG-ESSAI-APPLI-1")->leaseDays == 15 && s.licence("XPG-ESSAI-APPLI-1")->seats == 2,
              "sur le service : le bail de 15 j garde, 2 postes");
        check(call(url, "POST", "/v1/activer", activateBody(code2, pc9.request({}, lic::systemNow()), pc9.name), pc9.key.get()).status == 200,
              "le code reste le meme ; 2 PC maintenant");
    }

    std::printf("-- la synchro de l'archive entre deux postes Admin\n");
    {
        std::string why;
        ar::SyncOptions o;
        o.now = lic::systemNow();
        o.tool = "admin_service_test (poste A)";
        ar::SyncReport r = ar::synchronize(A, *ed.key, o);
        check(r.ok && !r.sent.empty() && r.version >= 1, "le poste A envoie son archive (" + r.text() + ")");
        bool clearText = false;
        {
            on::Client c(url, *ed.key, ed.options());
            (void)c.connect(&why);
            // aucun fichier de l'archive en ligne ne nomme un client en clair
            for (const char* path : {"licences/XPG-ESSAI-EN-LIGNE-1.xpglic.chiffre", "activations/activations.jsonl.chiffre", "revocations/revocations.xpgrev.chiffre"}) {
                std::string p = path;
                std::string enc;
                for (char ch : p) enc += ch == '/' ? std::string("%2F") : std::string(1, ch);
                const on::Reply t = c.call("GET", "/admin/archive/fichier?chemin=" + enc);
                clearText = clearText || t.status != 200 || has(t.body, "Client d'essai");
            }
        }
        check(!clearText, "en ligne : les licences, les revocations, les activations sont chiffrees (aucun nom de client en clair)");
        r = ar::synchronize(A, *ed.key, o);
        check(r.ok && r.upToDate, "une seconde fois : deja a jour");
        check(ar::lastSync(A).at > 0 && ar::lastSync(A).version == r.version, "la derniere synchro notee");
        // le poste B : la meme cle d'editeur, la cle de synchro redonnee par le code de secours
        const fs::path B = root / "editeur-b";
        on::Config cfg;
        cfg.url = url;
        check(on::writeConfig(U(B), cfg, &why), "B : l'adresse");
        ar::SyncKey ka{}, kb{};
        check(ar::loadSyncKey(A, ka, &why) && ar::restoreSyncKey(U(B), ar::recoveryCode(ka), kb, &why), "B : la cle de synchro redonnee par le code de secours");
        o.tool = "admin_service_test (poste B)";
        o.receiveOnly = true;
        r = ar::synchronize(U(B), *ed.key, o);
        check(r.ok && r.received.size() >= 4, "B recoit (" + r.text() + ")");
        check(readFile(B / "archive" / "licences" / "XPG-ESSAI-EN-LIGNE-1.xpglic") == readFile(ed.folder / "archive" / "licences" / "XPG-ESSAI-EN-LIGNE-1.xpglic") &&
                  readFile(B / "archive" / "registre" / "registre.xpgreg") == ed.registry,
              "B a les memes licences et le meme registre");
        check(fs::is_regular_file(B / "activations.jsonl") && readFile(B / "activations.jsonl") == readFile(ed.folder / "activations.jsonl"),
              "B a le registre des activations");
        // un envoi concurrent : A et B signent chacun une licence ; B envoie pendant que A s'apprete a envoyer
        Editor& eA = ed;
        std::string la1, lb1;
        check(eA.licence("XPG-ESSAI-POSTE-A", "temporary", 1, url, {}, la1, &why), "A signe une licence");
        {
            la::LicenseInputs in;
            json::Value p = json::Value::object();
            p.set("license_id", "XPG-ESSAI-POSTE-B");
            p.set("customer", "Client d'essai poste B");
            p.set("edition", "PERSONAL");
            p.set("duration_days", 30);
            in.params = json::write(p);
            in.registry = ed.registry;
            in.service = url;
            std::string summary, what;
            check(la::signLicense(*ed.key, in, lic::systemNow(), lb1, &why, &summary) && ar::archiveSigned(U(B), lb1, ar::trustedKeys(ed.key.get()), &what, &why),
                  "B signe une autre licence");
        }
        int raced = 0;
        ar::SyncOptions oa;
        oa.now = lic::systemNow();
        oa.tool = "admin_service_test (poste A)";
        oa.beforeSend = [&] {
            if (raced++ > 0) return;
            ar::SyncOptions ob;
            ob.now = lic::systemNow();
            ob.tool = "admin_service_test (poste B)";
            const ar::SyncReport rb = ar::synchronize(U(B), *ed.key, ob);
            check(rb.ok && !rb.sent.empty(), "B envoie pendant ce temps (" + rb.text() + ")");
        };
        r = ar::synchronize(A, *ed.key, oa);
        check(r.ok && r.attempts == 2 && !r.warnings.empty() && has(r.warnings.front(), "un autre poste"), "A : 409, relu, fusionne, recommence (" + r.text() + ")");
        check(fs::is_regular_file(ed.folder / "archive" / "licences" / "XPG-ESSAI-POSTE-B.xpglic"), "A a recu la licence de B");
        o.receiveOnly = false;
        o.tool = "admin_service_test (poste B)";
        r = ar::synchronize(U(B), *ed.key, o);
        check(r.ok && fs::is_regular_file(B / "archive" / "licences" / "XPG-ESSAI-POSTE-A.xpglic"), "B recoit celle de A");
        // une AUTRE cle de synchro : refusee
        const fs::path C = root / "editeur-c";
        (void)on::writeConfig(U(C), cfg, &why);
        ar::SyncKey kc{};
        (void)ar::createSyncKey(U(C), kc, &why);
        r = ar::synchronize(U(C), *ed.key, o);
        check(!r.ok && has(r.why, "AUTRE cl"), "une autre cle de synchro : refusee, rien n'est recu ni envoye");
        // l'index modifie hors de l'outil : refuse
        on::Client c(url, *ed.key, ed.options());
        (void)c.connect(&why);
        const on::Reply lst = c.call("GET", "/admin/archive");
        json::Value body = json::Value::object();
        body.set("base", static_cast<std::uint64_t>(lst.json.integer("version")));
        json::Value files = json::Value::object();
        std::string idx;
        {
            const on::Reply t = c.call("GET", "/admin/archive/fichier?chemin=index.xpgsync");
            idx = t.body;
        }
        const auto at = idx.find("\"compteur\"");
        if (at != std::string::npos) idx.insert(at, " ");
        files.set("index.xpgsync", idx);
        body.set("fichiers", files);
        check(c.call("POST", "/admin/archive", json::write(body, false)).status == 200, "(l'index change a la main)");
        r = ar::synchronize(A, *ed.key, o);
        check(!r.ok && has(r.why, "modifi"), "un index change hors de l'outil : refuse (" + r.why + ")");
        lic::crypto::wipe(ka.data(), ka.size());
        lic::crypto::wipe(kb.data(), kb.size());
        lic::crypto::wipe(kc.data(), kc.size());
    }

    if (!console.empty()) {
        std::printf("-- la console : licence-en-ligne montre le code UNE fois\n");
        const fs::path params = root / "p.json";
        writeFile(params, "{\"license_id\": \"XPG-ESSAI-CONSOLE-1\", \"customer\": \"Client d'essai console\", \"edition\": \"PERSONAL\", \"duration_days\": 30}");
        const std::string cmd = "\"" + console + "\" licence-en-ligne --parametres \"" + U(params) + "\" --bail-jours 20 --dossier \"" + U(ed.folder) + "\" 2>&1";
        std::string out;
        if (FILE* p = popen(cmd.c_str(), "r")) {
            char buf[4096];
            std::size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
            const int rc = pclose(p);
            check(rc == 0, "la commande reussit (" + out.substr(0, 400) + ")");
        }
        const std::regex re("XPG-[0-9A-Z]{4}-[0-9A-Z]{4}-[0-9A-Z]{4}-[0-9A-Z]{4}");
        std::vector<std::string> codes;
        for (auto it = std::sregex_iterator(out.begin(), out.end(), re); it != std::sregex_iterator(); ++it) codes.push_back(it->str());
        check(codes.size() == 1, "le code apparait une seule fois (" + std::to_string(codes.size()) + ")");
        if (codes.size() == 1) {
            // 1.37.0 : garde chiffre (DPAPI sous Windows) dans codes-activation/ seulement ; code-licence le remontre.
            check(!foundInFilesOutside(ed.folder, codes[0], "codes-activation"), "et dans aucun autre fichier du poste");
            std::string kept, keptWhy;
            check(on::readCode(U(ed.folder), "XPG-ESSAI-CONSOLE-1", kept, &keptWhy) && kept == codes[0], "1.37.0 : le code garde sur ce poste (" + keptWhy + ")");
            std::string again;
            const std::string cmd2 = "\"" + console + "\" code-licence --licence XPG-ESSAI-CONSOLE-1 --dossier \"" + U(ed.folder) + "\" 2>&1";
            if (FILE* p = popen(cmd2.c_str(), "r")) {
                char buf[4096];
                std::size_t n = 0;
                while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) again.append(buf, n);
                check(pclose(p) == 0 && has(again, codes[0]), "code-licence le remontre");
            }
            Pc pcC(root / "pc-console", "PC d'essai console");
            check(call(url, "POST", "/v1/activer", activateBody(codes[0], pcC.request({}, lic::systemNow()), pcC.name), pcC.key.get()).status == 200,
                  "un PC s'active avec lui");
        }
        on::Snapshot s;
        (void)on::listLicences(A, *ed.key, ed.options(), s);
        check(s.licence("XPG-ESSAI-CONSOLE-1") && s.licence("XPG-ESSAI-CONSOLE-1")->leaseDays == 20, "le bail donne (20 j)");
    }

    std::printf("-- le certificat a renouveler (l'horloge du service avancee de 340 jours)\n");
    {
        check(svc.restart(340LL * 86400), "le service relance, son horloge avancee (" + svc.error() + ")");
        on::Config c;
        c.url = svc.url();
        std::string why;
        (void)on::writeConfig(A, c, &why);
        on::ServiceState st;
        on::Result r = on::verify(A, *ed.key, ed.options(), &st);
        // l'adresse a change (un autre port) : le certificat nomme l'ancienne -> a refaire ; sinon : bientot expire
        check(r.ok && st.needsRenewal(st.time), "il faut le renouveler (" + r.text() + ")");
        r = on::certify(A, *ed.key, ed.options(), st.serviceKey);
        check(r.ok, "renouvele (" + r.text() + ")");
        r = on::verify(A, *ed.key, ed.options(), &st);
        check(r.ok && st.certOk && !st.needsRenewal(st.time) && st.certExpires > st.time + 360 * 86400, "valable un an de plus, depuis l'heure du service");
    }

    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
