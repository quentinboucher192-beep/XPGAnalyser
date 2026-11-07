// =============================================================================
//  tests/licensing_activation_test.cpp - 1.35.0 : le magasin et le service des
//  licences (licensing/Store, Licensing.hpp), SANS reseau
// -----------------------------------------------------------------------------
//  Une cle d'editeur d'ESSAI et une cle de service d'essai sont creees ici ; le
//  certificat du service, la licence en ligne, les activations, les jetons, les
//  heures et les Decouvertes sont signes a la main (licensing::signBlock et les
//  ecrivains de Service.hpp) ; l'horloge du PC et l'horloge monotone sont celles
//  de l'essai :
//    - les documents : une licence en ligne se lit (service donne, aucun PC),
//      un melange est refuse, une flottante sans service aussi ;
//    - une activation valable -> Valide (en ligne, le bail) ; renouvelee sans
//      bruit dans l'historique ; la licence en ligne ne s'importe pas en fichier ;
//    - les refus : une activation d'un autre PC, d'une autre emission, d'une
//      autre installation de Windows, d'une autre licence, signee par une cle de
//      service inconnue, un certificat d'une cle d'editeur inconnue, expire, sans
//      le droit, un bail deja fini, une licence pas en ligne, le type ;
//    - le bail : 7 jours avant la fin, Bail expire ; l'horloge reculee ne le
//      ravive pas, le temps ou le programme tourne compte (horloge monotone) ;
//      les copies du compteur ; une heure signee plus recente recale tout, une
//      plus ancienne est ignoree, un mauvais nonce refuse ; le certificat expire ;
//    - la licence flottante : sans jeton -> Flottante : aucun poste ; avec ->
//      Valide ; le jeton fini, rendu ; une horloge en avance ;
//    - la Decouverte enregistree par le service : la plus ancienne gagne ;
//      l'etiquette << Decouverte >> ;
//    - une licence liee par l'editeur : comme avant ; l'API (Licensing.hpp).
//
//  licensing_activation_test <racine du depot>   (XPG_VERBOSE=1 : chaque controle)
//  Aucune donnee client : un << Client d'essai >>.
// =============================================================================
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/licensing/Licensing.hpp"
#include "../src/licensing/Service.hpp"

#include "../src/core/Json.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace lic = licensing;
namespace adm = license_admin;
namespace sv = licensing::service;
using lic::LicenseStatus;
using lic::Seconds;

namespace {

int g_checks = 0, g_failures = 0;
bool g_verbose = false;
Seconds g_now = 0;
Seconds g_steady = 1000;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_verbose) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string U(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

Seconds T(const char* iso) {
    Seconds t = 0;
    if (!lic::parseIsoTime(iso, t)) std::printf("  (date d'essai illisible : %s)\n", iso);
    return t;
}

// ---- le monde de l'essai ----
fs::path g_root;
std::unique_ptr<lic::SigningKey> g_editor;    // la cle de l'editeur (d'essai)
std::unique_ptr<lic::SigningKey> g_service;   // la cle du service (d'essai)
std::unique_ptr<lic::SigningKey> g_rogue;     // une cle que personne ne certifie
std::unique_ptr<lic::SigningKey> g_otherPc;   // la cle d'un autre PC
std::string g_fixture, g_registry;
fs::path g_bundled;
lic::Registry g_reg;
const Seconds T0 = T("2026-11-07T10:00:00Z");
constexpr Seconds D = lic::kDay;
constexpr const char* kUrl = "https://xpg-licences.essai.workers.dev";

std::unique_ptr<lic::SigningKey> makeKey(const fs::path& dir, const std::string& name) {
    fs::create_directories(dir);
    std::string why;
    auto k = lic::SigningKey::create(U(dir / "cle.cle"), lic::deviceKeySpec(), name, false, &why);
    if (!k) std::printf("  (cle impossible : %s)\n", why.c_str());
    return k;
}

std::string block(lic::DocKind kind, const std::string& doc, const lic::SigningKey& k) {
    return lic::writeBlock(lic::signBlock(kind, doc, k.publicKey(), [&](std::string_view m) { return k.sign(m); }));
}

lic::TrustedKey trustedOf(const lic::SigningKey& k) {
    lic::TrustedKey t;
    t.id = k.keyId();
    t.name = k.name();
    t.key = k.publicKey();
    t.test = k.test();
    return t;
}

lic::StoreOptions optionsFor(const fs::path& folder) {
    lic::StoreOptions o;
    o.folder = U(folder);
    o.trustedKeys = {trustedOf(*g_editor)};
    o.compiledKeys = false;
    o.clock = [] { return g_now; };
    o.monotonicClock = [] { return g_steady; };
    o.machineId = "machine-essai-" + folder.filename().string();
    o.evaluationPeriod = true;
    o.bundledRegistry = U(g_bundled);
    return o;
}

// Le certificat du service : la cle du service (par defaut), signe par l'editeur (par defaut).
std::string certText(Seconds issued, Seconds expires, std::vector<std::string> rights = {"activation", "flottante", "heure", "decouverte"},
                     const lic::SigningKey* serviceKey = nullptr, const lic::SigningKey* signer = nullptr) {
    sv::ServiceCert c;
    c.key = (serviceKey ? serviceKey : g_service.get())->publicKeyHex();
    c.url = kUrl;
    c.issuedAt = issued;
    c.expiresAt = expires;
    c.rights = std::move(rights);
    return block(lic::DocKind::ServiceCert, sv::writeCert(c), signer ? *signer : *g_editor);
}
std::string g_cert;   // T0 - 30 jours .. T0 + 365 jours, tous les droits

// Une licence en ligne (aucun PC, l'adresse du service), signee par l'editeur.
lic::License onlineLicense(const std::string& id, lic::LicenseType type, Seconds issued, Seconds expires) {
    lic::License l;
    l.licenseId = id;
    l.customer = "Client d'essai";
    l.edition = "PROFESSIONAL";
    l.type = type;
    l.service = kUrl;
    l.maxDevices = 3;
    if (const lic::EditionDef* e = g_reg.edition("PROFESSIONAL")) l.features = e->features;
    lic::normalizeGrants(l.features);
    l.issuedAt = issued;
    l.expiresAt = expires;
    l.offlineGraceDays = 14;
    l.manifest = g_reg.number;
    l.nonce = lic::newNonce();
    return l;
}
std::string licenseText(const lic::License& l) { return block(lic::DocKind::License, lic::writeLicense(l), *g_editor); }

sv::Activation activationFor(const lic::License& l, const std::string& keyHex, const std::string& station, Seconds issued, Seconds until,
                             std::uint64_t number = 1) {
    sv::Activation a;
    a.licenseId = l.licenseId;
    a.licenseIssuedAt = l.issuedAt;
    a.deviceKey = keyHex;
    a.station = station;
    a.floating = l.type == lic::LicenseType::Floating;
    a.number = number;
    a.issuedAt = issued;
    a.until = until;
    return a;
}
std::string activationText(const sv::Activation& a, const lic::SigningKey* signer = nullptr) {
    return block(lic::DocKind::Activation, sv::writeActivation(a), signer ? *signer : *g_service);
}

std::string timeText(Seconds t, const std::string& nonce, const lic::SigningKey* signer = nullptr) {
    sv::SignedTime x;
    x.time = t;
    x.nonce = nonce;
    return block(lic::DocKind::Time, sv::writeTime(x), signer ? *signer : *g_service);
}

std::string leaseText(const std::string& licenseId, const std::string& keyHex, Seconds issued, Seconds until) {
    sv::Lease l;
    l.licenseId = licenseId;
    l.deviceKey = keyHex;
    l.issuedAt = issued;
    l.until = until;
    return block(lic::DocKind::Lease, sv::writeLease(l), *g_service);
}

std::string trialText(const std::string& keyHex, const std::string& machine, Seconds start) {
    sv::Trial t;
    t.deviceKey = keyHex;
    t.machine = machine;
    t.start = start;
    t.days = 30;
    return block(lic::DocKind::Trial, sv::writeTrial(t), *g_service);
}

std::string nonce() { return lic::newNonce(); }

// La cle de ce PC (creee par la signature d'un premier appel) et son empreinte du poste.
struct Pc {
    fs::path    folder;
    std::string keyHex, station, machineId;
};
Pc pcKey(lic::LicenseStore& st, const fs::path& folder) {
    Pc pc;
    pc.folder = folder;
    lic::crypto::Signature sig{};
    lic::crypto::PublicKey pub{};
    std::string why;
    const std::string msg = sv::requestMessage("POST", "/v1/activer", "1793961600", nonce(), "{}");
    if (!st.deviceSignServiceMessage(msg, sig, pub, nullptr, &why)) std::printf("  (signature du PC : %s)\n", why.c_str());
    pc.keyHex = lic::crypto::hex(pub);
    pc.machineId = st.machineId();
    pc.station = lic::stationHash(pc.machineId, pub);
    return pc;
}

int countKind(const lic::LicenseStore& st, const std::string& kind) {
    int n = 0;
    for (const auto& e : st.history()) n += e.kind == kind ? 1 : 0;
    return n;
}

const std::string kNoLoss = " Ton projet reste ouvert ; Enregistrer et Exporter restent permis.";

// ===================================================================== les documents ==
void testDocuments() {
    std::printf("-- les documents : la licence en ligne (service, aucun PC)\n");
    std::string why;
    const lic::License l = onlineLicense("XPG-2026-00135", lic::LicenseType::Temporary, T0 - D, T0 + 365 * D);
    lic::License back;
    check(lic::parseLicense(lic::writeLicense(l), back, &why) && back.service == kUrl && back.devices.empty() && back.maxDevices == 3,
          "une licence en ligne (service, aucun PC, 3 postes) se lit : " + why);
    lic::License mix = l;
    lic::BoundDevice d;
    d.key = g_otherPc->publicKeyHex();
    d.name = "Poste essai";
    d.activatedAt = T0;
    mix.devices.push_back(d);
    check(!lic::parseLicense(lic::writeLicense(mix), back, &why) && has(why, "ne lie aucun PC"), "en ligne ET un PC lie : refusee (" + why + ")");
    lic::License fl = l;
    fl.service.clear();
    fl.type = lic::LicenseType::Floating;
    fl.expiresAt = 0;
    fl.devices.push_back(d);
    check(!lic::parseLicense(lic::writeLicense(fl), back, &why) && has(why, "flottante"), "flottante sans service : refusee (" + why + ")");
    fl.service = kUrl;
    fl.devices.clear();
    check(lic::parseLicense(lic::writeLicense(fl), back, &why) && back.type == lic::LicenseType::Floating, "flottante en ligne : se lit : " + why);
    lic::License badUrl = l;
    badUrl.service = "http://exemple.com";
    check(!lic::parseLicense(lic::writeLicense(badUrl), back, &why) && has(why, "adresse du service"), "une adresse de service en http ailleurs : refusee (" + why + ")");
    lic::License none = l;
    none.service.clear();
    check(!lic::parseLicense(lic::writeLicense(none), back, &why) && has(why, "aucun PC"), "ni service ni PC : refusee, comme avant (" + why + ")");
}

// ====================================================================== l'activation ==
void testActivation() {
    std::printf("-- une activation valable : Valide, en ligne ; renouvelee sans bruit\n");
    g_now = T0;
    const fs::path folder = g_root / "pc-act";
    lic::LicenseStore st(optionsFor(folder));
    const Pc pc = pcKey(st, folder);
    check(pc.keyHex.size() == 64 && !pc.station.empty(), "la cle de ce PC creee par la signature d'un appel");
    {
        lic::crypto::Signature sig{};
        lic::crypto::PublicKey pub{};
        std::string why;
        check(!st.deviceSignServiceMessage("XPG-LICENSING-1\nlicence\n{}", sig, pub, nullptr, &why) && has(why, "service des licences"),
              "la cle de ce PC ne signe pas autre chose qu'un appel au service (" + why + ")");
    }
    auto s = st.evaluate();
    check(s.status == LicenseStatus::Evaluation && lic::statusLabel(s.status) == "D\xC3\xA9" "couverte" && !s.online,
          "sans licence : la Decouverte (etiquette << D\xC3\xA9" "couverte >>)");
    const lic::License l = onlineLicense("XPG-2026-00135", lic::LicenseType::Temporary, T0 - D, T0 + 365 * D);
    const std::string lt = licenseText(l);
    auto rep = st.installActivation(lt, activationText(activationFor(l, pc.keyHex, pc.station, T0, T0 + 30 * D)), g_cert);
    s = rep.state;
    check(rep.ok && rep.licenseInstalled && rep.why.empty(), "installActivation : acceptee (" + rep.why + ")");
    check(s.status == LicenseStatus::Valid && s.online && !s.floating && s.licenseId == "XPG-2026-00135" && s.edition == "PROFESSIONAL"
              && s.hasFeature("ai_generation") && s.boundDevices == 0 && s.maxDevices == 3,
          "Valide, en ligne, PROFESSIONAL (" + std::string(lic::statusKey(s.status)) + " : " + s.message + " / " + s.detail + ")");
    check(s.activationUntil == T0 + 30 * D && s.activationDaysLeft == 30 && !s.activationWarning && s.trustedNow == T0,
          "le bail : jusqu'au 07/12/2026, 30 jours, pas d'avertissement (" + std::to_string(s.activationDaysLeft) + ")");
    check(has(s.message, "Activ\xC3\xA9" "e en ligne jusqu'au 07/12/2026"), "la phrase dit le bail : " + s.message);
    std::error_code ec;
    check(fs::is_regular_file(folder / lic::LicenseStore::kLicenseFile, ec) && fs::is_regular_file(folder / lic::LicenseStore::kActivationFile, ec)
              && readFile(folder / lic::LicenseStore::kLicenseFile) == lt,
          "licence.xpglic et activation.xpgact ecrits");
    check(has(readFile(folder / lic::LicenseStore::kActivationFile), "XPG-LICENSING 1 activation")
              && has(readFile(folder / lic::LicenseStore::kActivationFile), "XPG-LICENSING 1 service"),
          "activation.xpgact : l'activation et le certificat");
    {
        const auto h = st.history();
        check(!h.empty() && h[0].kind == "etat" && h.size() >= 2 && h[1].kind == "import" && h[1].ok && has(h[1].text, "activ\xC3\xA9" "e sur ce PC en ligne"),
              "historique : l'activation, puis << -> Valide >> (" + (h.size() > 1 ? h[1].text : std::string()) + ")");
    }
    const int imports = countKind(st, "import"), lines = static_cast<int>(st.history().size());
    g_now = T0 + 2 * D;
    rep = st.installActivation(lt, activationText(activationFor(l, pc.keyHex, pc.station, T0 + 2 * D, T0 + 32 * D)), g_cert);
    check(rep.ok && !rep.licenseInstalled && rep.state.activationUntil == T0 + 32 * D && rep.state.activationDaysLeft == 30
              && !rep.lines.empty() && has(rep.lines[0], "renouvel"),
          "renouvelee (meme numero) : le bail jusqu'au 09/12/2026");
    check(countKind(st, "import") == imports && static_cast<int>(st.history().size()) == lines, "un renouvellement n'ajoute rien a l'historique");
    rep = st.installActivation(lt, activationText(activationFor(l, pc.keyHex, pc.station, T0, T0 + 30 * D)), g_cert);
    check(rep.ok && rep.state.activationUntil == T0 + 32 * D, "une activation plus courte (rejouee) : la plus longue reste");
    // la licence en ligne, et les documents du service, ne s'importent pas en fichier
    rep = st.importBytes(lt);
    check(!rep.ok && has(rep.why, "licence en ligne") && has(rep.why, "code d'activation") && rep.state.status == LicenseStatus::Valid,
          "la licence en ligne importee en fichier : refusee (" + rep.why + ")");
    rep = st.importBytes(g_cert);
    check(!rep.ok && has(rep.why, "service des licences") && !has(rep.why, "GitHub"), "le certificat importe en fichier : refuse (" + rep.why + ")");
    check(lic::licenseFeaturesApply(LicenseStatus::Valid) && !lic::licenseFeaturesApply(LicenseStatus::LeaseExpired)
              && !lic::licenseFeaturesApply(LicenseStatus::NoFloatingSeat),
          "licenseFeaturesApply : ni Bail expire, ni Flottante : aucun poste");
    check(std::string(lic::statusKey(LicenseStatus::LeaseExpired)) == "lease-expired" && std::string(lic::statusKey(LicenseStatus::NoFloatingSeat)) == "no-floating-seat"
              && lic::statusLabel(LicenseStatus::LeaseExpired) == "Bail expir\xC3\xA9" && lic::statusLabel(LicenseStatus::NoFloatingSeat) == "Flottante : aucun poste",
          "les cles et les etiquettes des deux etats neufs");
}

// ========================================================================= les refus ==
void testRefusals() {
    std::printf("-- les refus : autre PC, autre emission, autre installation, cles et certificats\n");
    g_now = T0;
    const fs::path folder = g_root / "pc-refus";
    lic::LicenseStore st(optionsFor(folder));
    const Pc pc = pcKey(st, folder);
    (void)st.evaluate();
    const lic::License l = onlineLicense("XPG-2026-00140", lic::LicenseType::Perpetual, T0 - D, 0);
    const std::string lt = licenseText(l);
    const auto good = activationFor(l, pc.keyHex, pc.station, T0, T0 + 30 * D);
    std::error_code ec;
    const auto refused = [&](const std::string& act, const std::string& cert, const std::string& part, const std::string& what, const std::string& lic2 = std::string()) {
        const auto rep = st.installActivation(lic2.empty() ? lt : lic2, act, cert);
        check(!rep.ok && has(rep.why, part) && !fs::exists(folder / lic::LicenseStore::kLicenseFile, ec) && !fs::exists(folder / lic::LicenseStore::kActivationFile, ec)
                  && rep.state.status == LicenseStatus::Evaluation,
              what + " : refusee, rien d'ecrit (" + rep.why + ")");
    };
    auto a = good;
    a.deviceKey = g_otherPc->publicKeyHex();
    refused(activationText(a), g_cert, "pour un autre PC", "une activation pour un autre PC");
    a = good;
    a.licenseIssuedAt = l.issuedAt - D;
    refused(activationText(a), g_cert, "autre \xC3\xA9mission", "une activation pour une autre emission de la licence");
    a = good;
    a.station = std::string(32, 'a');
    refused(activationText(a), g_cert, "autre installation de Windows", "une activation pour une autre installation de Windows");
    a = good;
    a.licenseId = "XPG-2026-00999";
    refused(activationText(a), g_cert, "pour une autre licence", "une activation pour une autre licence");
    refused(activationText(good, g_rogue.get()), g_cert, "la signature n'est pas celle de la cl\xC3\xA9 du service", "une activation signee par une cle de service inconnue");
    refused(activationText(good), certText(T0 - 30 * D, T0 + 365 * D, {"activation"}, nullptr, g_rogue.get()), "une cl\xC3\xA9 de l'\xC3\xA9" "diteur que ce programme croit",
            "un certificat signe par une cle d'editeur inconnue");
    refused(activationText(good), certText(T0 - 100 * D, T0 - D), "le certificat du service a expir\xC3\xA9 le 06/11/2026", "un certificat expire");
    refused(activationText(good), certText(T0 - 30 * D, T0 + 365 * D, {"heure", "decouverte"}), "ne donne pas le droit \xC2\xAB activation \xC2\xBB",
            "un certificat sans le droit << activation >>");
    refused(activationText(activationFor(l, pc.keyHex, pc.station, T0 - 40 * D, T0 - 10 * D)), g_cert, "bail est d\xC3\xA9j\xC3\xA0 fini",
            "un bail deja fini");
    a = good;
    a.floating = true;
    refused(activationText(a), g_cert, "flottante", "une activation flottante pour une licence fixe");
    refused(activationText(good), g_cert, "illisible", "sans licence", "rien");
    // une licence liee par l'editeur n'est pas une licence en ligne
    lic::License bound = l;
    bound.service.clear();
    lic::BoundDevice d;
    d.key = pc.keyHex;
    d.name = "Poste essai";
    d.activatedAt = T0;
    bound.devices.push_back(d);
    refused(activationText(good), g_cert, "pas une licence en ligne", "une licence liee par l'editeur", licenseText(bound));
    // une licence que l'editeur n'a pas signee
    refused(activationText(good), g_cert, "inconnue de ce programme", "une licence signee par une autre cle",
            block(lic::DocKind::License, lic::writeLicense(l), *g_rogue));
    check(countKind(st, "import") >= 12, "chaque refus est dans l'historique");
    // un programme sans cle de l'editeur : rien ne se verifie
    lic::StoreOptions o = optionsFor(g_root / "pc-sans-cle");
    o.trustedKeys.clear();
    lic::LicenseStore none(o);
    const auto rep = none.installActivation(lt, activationText(good), g_cert);
    check(!rep.ok && rep.state.status == LicenseStatus::NotConfigured, "sans cle de l'editeur : refusee, Non activees");
    // la bonne passe enfin
    check(st.installActivation(lt, activationText(good), g_cert).state.status == LicenseStatus::Valid, "la bonne activation : Valide");
}

// =========================================================================== le bail ==
void testBail() {
    std::printf("-- le bail : 7 jours avant, Bail expire, l'horloge reculee, l'horloge monotone, les copies\n");
    g_now = T0;
    g_steady = 5000;
    const fs::path folder = g_root / "pc-bail";
    auto st = std::make_unique<lic::LicenseStore>(optionsFor(folder));
    const Pc pc = pcKey(*st, folder);
    const lic::License l = onlineLicense("XPG-2026-00150", lic::LicenseType::Perpetual, T0 - D, 0);
    const std::string lt = licenseText(l);
    check(st->installActivation(lt, activationText(activationFor(l, pc.keyHex, pc.station, T0, T0 + 30 * D)), g_cert).state.status == LicenseStatus::Valid,
          "activee : bail jusqu'au 07/12/2026");
    std::string why;
    const std::string n1 = nonce();
    check(st->noteServiceTime(timeText(T0, n1), n1, g_cert, &why), "l'heure signee du service est notee : " + why);
    auto s = st->evaluate();
    check(s.lastServiceSync == T0 && s.trustedNow == T0, "lastServiceSync et l'heure retenue : T0");
    std::error_code ec;
    check(fs::is_regular_file(folder / lic::LicenseStore::kClockFile, ec), "horloge.bin : une copie du compteur");

    g_now = T0 + 24 * D;
    g_steady += 24 * D;
    s = st->evaluate();
    check(s.status == LicenseStatus::Valid && s.activationDaysLeft == 6 && s.activationWarning && s.trustedNow == T0 + 24 * D,
          "jour 24 : 6 jours de bail, l'avertissement (" + std::to_string(s.activationDaysLeft) + ", heure retenue " + lic::isoTime(s.trustedNow) + ")");
    check(has(s.message, "Connecte ce PC \xC3\xA0 Internet avant le 07/12/2026 pour renouveler sa licence."), "la phrase des 7 jours : " + s.message);

    g_now = T0 + 31 * D;
    g_steady += 7 * D;
    s = st->evaluate();
    check(s.status == LicenseStatus::LeaseExpired && lic::statusLabel(s.status) == "Bail expir\xC3\xA9" && s.edition == "FREE" && !s.hasFeature("ai_generation")
              && s.activationUntil == T0 + 30 * D && s.activationDaysLeft == 0 && s.online,
          "jour 31 : Bail expire, l'edition FREE (" + std::string(lic::statusKey(s.status)) + ")");
    check(s.message == "Connecte ce PC \xC3\xA0 Internet pour renouveler sa licence (bail fini le 07/12/2026). En attendant, l'\xC3\xA9" "dition FREE s'applique." + kNoLoss,
          "la phrase : " + s.message);

    // l'horloge reculee ne ravive pas le bail ; le temps ou le programme tourne compte
    g_now = T0 + 5 * D;
    g_steady += 3600;
    s = st->evaluate();
    check(s.status == LicenseStatus::LeaseExpired && s.trustedNow == T0 + 31 * D + 3600,
          "l'horloge reculee au jour 5 : toujours Bail expire ; l'heure retenue avance de l'heure ecoulee (" + lic::isoTime(s.trustedNow) + ")");
    g_steady += 2 * D;
    s = st->evaluate();
    check(s.trustedNow == T0 + 33 * D + 3600, "deux jours de plus, l'horloge du PC arretee : l'horloge monotone les compte (" + lic::isoTime(s.trustedNow) + ")");
    // un autre lancement : pas d'horloge monotone d'avant, le compteur garde
    st = std::make_unique<lic::LicenseStore>(optionsFor(folder));
    g_steady = 10;
    s = st->evaluate();
    check(s.status == LicenseStatus::LeaseExpired && s.trustedNow == T0 + 33 * D + 3600, "un autre lancement, l'horloge toujours reculee : le compteur est garde");
    fs::remove(folder / lic::LicenseStore::kClockFile, ec);
    s = lic::LicenseStore(optionsFor(folder)).evaluate();
    check(s.trustedNow == T0 + 33 * D + 3600 && fs::is_regular_file(folder / lic::LicenseStore::kClockFile, ec), "horloge.bin efface : repris d'etat.bin, et remis");
    fs::remove(folder / lic::LicenseStore::kStateFile, ec);
    s = lic::LicenseStore(optionsFor(folder)).evaluate();
    check(s.status == LicenseStatus::LeaseExpired && s.trustedNow == T0 + 33 * D + 3600, "etat.bin efface : repris d'horloge.bin");

    // une heure signee plus recente, puis le renouvellement : Valide de nouveau
    st = std::make_unique<lic::LicenseStore>(optionsFor(folder));
    const std::string n2 = nonce();
    check(st->noteServiceTime(timeText(T0 + 33 * D, n2), n2, g_cert, &why), "l'heure du service (jour 33) notee : " + why);
    auto rep = st->installActivation(lt, activationText(activationFor(l, pc.keyHex, pc.station, T0 + 33 * D, T0 + 63 * D)), g_cert);
    check(rep.ok && rep.state.status == LicenseStatus::Valid && rep.state.activationDaysLeft == 30 && rep.state.trustedNow == T0 + 33 * D,
          "renouvelee au jour 33 (l'horloge du PC toujours au jour 5) : Valide, 30 jours (" + rep.why + ")");
    g_steady += 3600;
    s = st->evaluate();
    check(s.trustedNow == T0 + 33 * D + 3600 && s.status == LicenseStatus::Valid, "une heure plus tard (monotone) : l'heure retenue suit");
}

void testRecalibration() {
    std::printf("-- l'heure signee : recale (une horloge avancee par erreur), une plus ancienne ignoree, le nonce, le certificat\n");
    g_now = T0;
    g_steady = 100;
    const fs::path folder = g_root / "pc-avance";
    lic::LicenseStore st(optionsFor(folder));
    const Pc pc = pcKey(st, folder);
    const lic::License l = onlineLicense("XPG-2026-00160", lic::LicenseType::Perpetual, T0 - D, 0);
    (void)st.installActivation(licenseText(l), activationText(activationFor(l, pc.keyHex, pc.station, T0, T0 + 30 * D)), g_cert);
    std::string why;
    std::string n = nonce();
    check(st.noteServiceTime(timeText(T0, n), n, g_cert, &why), "l'heure du service notee (T0)");
    g_now = T0 + 60 * D;   // l'horloge avancee par erreur
    auto s = st.evaluate();
    check(s.status == LicenseStatus::LeaseExpired, "l'horloge avancee de 60 jours : Bail expire");
    g_now = T0 + D;        // remise a l'heure : le bail ne revient pas tout seul
    s = st.evaluate();
    check(s.status == LicenseStatus::LeaseExpired && s.trustedNow == T0 + 60 * D, "remise a l'heure (jour 1) : toujours Bail expire (l'heure retenue ne recule pas)");
    n = nonce();
    check(st.noteServiceTime(timeText(T0 + D, n), n, g_cert, &why), "une heure signee plus recente (jour 1) : notee");
    s = st.evaluate();
    check(s.status == LicenseStatus::Valid && s.trustedNow == T0 + D && s.lastServiceSync == T0 + D && s.activationDaysLeft == 29,
          "elle recale tout : Valide, 29 jours (" + lic::isoTime(s.trustedNow) + ")");
    n = nonce();
    check(st.noteServiceTime(timeText(T0 + 12 * 3600, n), n, g_cert, &why) && st.evaluate().lastServiceSync == T0 + D,
          "une heure signee plus ancienne : ignoree (la derniere reste)");
    // l'horloge du PC et l'horloge monotone avancent ensemble : compte une fois
    g_now += 3600;
    g_steady += 3600;
    s = st.evaluate();
    check(s.trustedNow == T0 + D + 3600, "une heure de plus sur les deux horloges : une heure, pas deux (" + lic::isoTime(s.trustedNow) + ")");
    n = nonce();
    check(!st.noteServiceTime(timeText(T0 + 2 * D, n), nonce(), g_cert, &why) && has(why, "nonce"), "un autre nonce : refusee (" + why + ")");
    check(!st.noteServiceTime(timeText(T0 + 2 * D, n, g_rogue.get()), n, g_cert, &why) && has(why, "signature"), "signee par une cle inconnue : refusee");
    check(!st.noteServiceTime(timeText(T0 + 2 * D, n), n, certText(T0 - D, T0 + 365 * D, {"activation"}), &why) && has(why, "droit \xC2\xAB heure"),
          "un certificat sans le droit << heure >> : refusee");
    check(!st.noteServiceTime(timeText(T0 + 2 * D, n), n, certText(T0 - 100 * D, T0 - D), &why) && has(why, "expir"),
          "une heure apres la fin du certificat : refusee");
    check(st.evaluate().lastServiceSync == T0 + D, "rien de tout cela n'a touche le compteur");

    std::printf("-- le certificat du service expire pendant le bail\n");
    g_now = T0;
    const fs::path f2 = g_root / "pc-certif";
    lic::LicenseStore s2(optionsFor(f2));
    const Pc p2 = pcKey(s2, f2);
    const lic::License l2 = onlineLicense("XPG-2026-00161", lic::LicenseType::Perpetual, T0 - D, 0);
    auto rep = s2.installActivation(licenseText(l2), activationText(activationFor(l2, p2.keyHex, p2.station, T0, T0 + 30 * D)), certText(T0 - D, T0 + 20 * D));
    check(rep.ok && rep.state.status == LicenseStatus::Valid, "activee avec un certificat qui finit le 27/11/2026");
    g_now = T0 + 21 * D;
    s = s2.evaluate();
    check(s.status == LicenseStatus::LeaseExpired && has(s.message, "le certificat du service a expir\xC3\xA9 le 27/11/2026"),
          "jour 21 : Bail expire, le certificat le dit (" + s.message + ")");
}

// ============================================================================ la flottante ==
void testFloating() {
    std::printf("-- la licence flottante : sans jeton, avec, fini, rendu, une horloge en avance\n");
    g_now = T0;
    g_steady = 100;
    const fs::path folder = g_root / "pc-flottante";
    lic::LicenseStore st(optionsFor(folder));
    const Pc pc = pcKey(st, folder);
    const lic::License l = onlineLicense("XPG-2026-00170", lic::LicenseType::Floating, T0 - D, 0);
    auto rep = st.installActivation(licenseText(l), activationText(activationFor(l, pc.keyHex, pc.station, T0, T0 + 30 * D)), g_cert);
    auto s = rep.state;
    check(rep.ok && s.status == LicenseStatus::NoFloatingSeat && s.floating && s.online && s.edition == "FREE" && !s.hasFeature("ai_generation")
              && lic::statusLabel(s.status) == "Flottante : aucun poste",
          "activee, sans jeton : Flottante : aucun poste, FREE (" + rep.why + ")");
    check(has(s.message, "Licence flottante PROFESSIONAL : ce PC n'a pas de poste en ce moment. Connecte-le \xC3\xA0 Internet pour en prendre un"),
          "la phrase : " + s.message);
    std::string why;
    check(!st.installLease(leaseText(l.licenseId, g_otherPc->publicKeyHex(), T0, T0 + 900), g_cert, &why) && has(why, "autre PC"), "un jeton d'un autre PC : refuse");
    check(!st.installLease(leaseText("XPG-2026-00999", pc.keyHex, T0, T0 + 900), g_cert, &why) && has(why, "autre licence"), "un jeton d'une autre licence : refuse");
    check(!st.installLease(leaseText(l.licenseId, pc.keyHex, T0, T0 + 900), certText(T0 - D, T0 + D, {"activation"}), &why) && has(why, "flottante"),
          "un certificat sans le droit << flottante >> : refuse (" + why + ")");
    check(!st.installLease(leaseText(l.licenseId, pc.keyHex, T0 - 1000, T0 - 100), g_cert, &why) && has(why, "pris fin"), "un jeton deja fini : refuse");
    check(st.installLease(leaseText(l.licenseId, pc.keyHex, T0, T0 + 900), g_cert, &why), "un jeton de 15 minutes : accepte (" + why + ")");
    s = st.evaluate();
    check(s.status == LicenseStatus::Valid && s.leaseUntil == T0 + 900 && s.hasFeature("ai_generation") && has(s.message, "ce PC a un poste"),
          "avec le jeton : Valide (" + s.message + ")");
    g_now = T0 + 16 * 60;
    g_steady += 16 * 60;
    s = st.evaluate();
    check(s.status == LicenseStatus::NoFloatingSeat && has(s.detail, "jeton a pris fin"), "16 minutes plus tard : Flottante : aucun poste (" + s.detail + ")");
    check(st.installLease(leaseText(l.licenseId, pc.keyHex, g_now, g_now + 900), g_cert, &why) && st.evaluate().status == LicenseStatus::Valid, "un nouveau jeton : Valide");
    st.dropLease();
    check(st.evaluate().status == LicenseStatus::NoFloatingSeat, "le jeton rendu : Flottante : aucun poste");
    // l'horloge du PC 20 minutes en avance sur le service : le jeton se compte a l'heure du service
    const std::string n = nonce();
    const Seconds service = g_now;
    g_now += 20 * 60;
    check(st.noteServiceTime(timeText(service, n), n, g_cert, &why), "l'heure du service (le PC a 20 minutes d'avance)");
    check(st.installLease(leaseText(l.licenseId, pc.keyHex, service, service + 900), g_cert, &why) && st.evaluate().status == LicenseStatus::Valid,
          "un jeton de 15 minutes, l'horloge du PC 20 minutes en avance : Valide (" + why + ")");
    // le service dit : poste libere
    check(st.dropActivation(l.licenseId, &why), "dropActivation : " + why);
    std::error_code ec;
    s = st.evaluate();
    check(!fs::exists(folder / lic::LicenseStore::kActivationFile, ec) && !fs::exists(folder / lic::LicenseStore::kLicenseFile, ec)
              && !fs::exists(folder / lic::LicenseStore::kLeaseFile, ec) && s.status == LicenseStatus::Absent && !s.online,
          "l'activation, le jeton et la licence en ligne effaces : Absente (pas de nouvelle Decouverte)");
    check(countKind(st, "service") == 1 && has(st.history()[1].text, "XPG-2026-00170"), "l'historique le dit (une ligne << service >>)");
    check(st.dropActivation("XPG-2026-00999", &why), "dropActivation d'une licence absente : rien a faire, vrai");
}

// ======================================================================= la Decouverte ==
void testTrial() {
    std::printf("-- la Decouverte enregistree par le service : la plus ancienne gagne\n");
    g_now = T0;
    const fs::path folder = g_root / "pc-decouverte";
    lic::LicenseStore st(optionsFor(folder));
    const Pc pc = pcKey(st, folder);
    auto s = st.evaluate();
    check(s.status == LicenseStatus::Evaluation && s.evaluationStartedAt == T0 && s.evaluationDaysLeft == 30
              && s.message == "D\xC3\xA9" "couverte : il te reste 30 jours (jusqu'au 07/12/2026). Pour continuer ensuite, demande une licence (menu Licence\xE2\x80\xA6).",
          "premier lancement : la Decouverte commence, 30 jours (" + s.message + ")");
    const std::string machine = st.thisMachineHash();
    check(machine == sv::machineHash(pc.machineId) && machine.size() == 32, "thisMachineHash : l'empreinte de la machine (32 hexadecimaux)");
    std::string why;
    check(!st.installTrialRecord(trialText(pc.keyHex, sv::machineHash("une-autre-machine"), T0 - 10 * D), g_cert, &why) && has(why, "autre machine"),
          "la Decouverte d'une autre machine : refusee (" + why + ")");
    check(!st.installTrialRecord(trialText(g_otherPc->publicKeyHex(), machine, T0 - 10 * D), g_cert, &why) && has(why, "autre PC"), "d'un autre PC : refusee");
    check(!st.installTrialRecord(trialText(pc.keyHex, machine, T0 - 10 * D), certText(T0 - D, T0 + D, {"activation"}), &why) && has(why, "decouverte"),
          "un certificat sans le droit << decouverte >> : refusee");
    check(st.installTrialRecord(trialText(pc.keyHex, machine, T0 - 10 * D), g_cert, &why), "le service l'a vue commencer 10 jours plus tot : acceptee (" + why + ")");
    s = st.evaluate();
    check(s.status == LicenseStatus::Evaluation && s.evaluationStartedAt == T0 - 10 * D && s.evaluationDaysLeft == 20 && has(s.message, "il te reste 20 jours (jusqu'au 27/11/2026)"),
          "le debut le plus ancien gagne : 20 jours (" + s.message + ")");
    {
        bool line = false;
        for (const auto& e : st.history()) line = line || (e.kind == "evaluation" && has(e.text, "selon le service des licences"));
        check(line, "l'historique : << Decouverte commencee le 28/10/2026 sur cette machine, selon le service des licences >>");
    }
    check(st.installTrialRecord(trialText(pc.keyHex, machine, T0 - 2 * D), g_cert, &why) && st.evaluate().evaluationStartedAt == T0 - 10 * D,
          "un debut plus tardif du service : rien ne change (la plus ancienne reste)");
    // un autre PC : sa Decouverte locale commence aujourd'hui ; le service la dit plus tardive : ignoree
    const fs::path f2 = g_root / "pc-decouverte-2";
    lic::LicenseStore s2(optionsFor(f2));
    const Pc p2 = pcKey(s2, f2);
    check(s2.evaluate().evaluationStartedAt == T0, "pc 2 : la Decouverte commence aujourd'hui");
    g_now = T0 + 3 * D;
    check(s2.installTrialRecord(trialText(p2.keyHex, s2.thisMachineHash(), T0 + 2 * D), g_cert, &why), "pc 2 : le service la dit commencee au jour 2");
    s = s2.evaluate();
    check(s.evaluationStartedAt == T0 && s.evaluationDaysLeft == 27, "pc 2 : la locale, plus ancienne, reste (27 jours)");
    // efface toutes les copies locales : le service la retrouve
    std::error_code ec;
    for (const char* f : {lic::LicenseStore::kStateFile, lic::LicenseStore::kEvaluationFile, lic::LicenseStore::kHistoryFile}) fs::remove(folder / f, ec);
    s = lic::LicenseStore(optionsFor(folder)).evaluate();
    check(s.status == LicenseStatus::Evaluation && s.evaluationStartedAt == T0 - 10 * D, "les copies locales effacees : decouverte.xpgdec la garde (jour -10)");
    g_now = T0 + 21 * D;
    s = lic::LicenseStore(optionsFor(folder)).evaluate();
    check(s.status == LicenseStatus::Absent && s.evaluationOver && has(s.message, "Ta D\xC3\xA9" "couverte de 30 jours est finie depuis le 27/11/2026"),
          "jour 21 : finie (selon le debut du service) (" + s.message + ")");
}

// ======================================================= une licence liee par l'editeur ==
void testBoundUnchanged() {
    std::printf("-- une licence liee par l'editeur : comme avant\n");
    g_now = T0;
    const fs::path folder = g_root / "pc-liee";
    lic::LicenseStore st(optionsFor(folder));
    const fs::path req = g_root / "pc-liee.xpgreq";
    std::string why;
    check(st.writeActivationRequest(U(req), "Poste essai", "", "IHM Studio (essai)", &why), "la demande de ce PC");
    adm::LicenseInputs in;
    in.params = "{\"license_id\": \"XPG-2026-00180\", \"customer\": \"Client d'essai\", \"edition\": \"PROFESSIONAL\"}";
    in.requests = {readFile(req)};
    in.registry = g_registry;
    std::string out, summary;
    if (!adm::signLicense(*g_editor, in, g_now, out, &why, &summary)) std::printf("  (licence refusee : %s)\n", why.c_str());
    auto rep = st.importBytes(out);
    check(rep.ok && rep.state.status == LicenseStatus::Valid && !rep.state.online && rep.state.activationUntil == 0 && !has(rep.state.message, "en ligne"),
          "importee : Valide, pas en ligne (" + rep.why + " " + rep.state.message + ")");
    check(rep.state.message == "Licence PROFESSIONAL valide jusqu'au 2027-11-07.", "la phrase d'avant : " + rep.state.message);
    // l'heure du service et l'horloge reculee : la licence liee garde ses regles (Horloge reculee)
    const std::string n = nonce();
    check(st.noteServiceTime(timeText(T0, n), n, g_cert, &why), "l'heure du service notee");
    g_now = T0 - 3 * D;
    check(st.evaluate().status == LicenseStatus::ClockRolledBack, "l'horloge reculee de 3 jours : Horloge reculee, comme avant");
    g_now = T0 + D;
    check(st.evaluate().status == LicenseStatus::Valid, "remise : Valide");
    // une activation qui traine n'y change rien
    writeFile(folder / lic::LicenseStore::kActivationFile, "rien");
    check(st.evaluate().status == LicenseStatus::Valid, "un activation.xpgact abime a cote : sans effet sur une licence liee");
}

// ============================================================================== l'API ==
void testApi() {
    std::printf("-- l'API (Licensing.hpp)\n");
    g_now = T0;
    g_steady = 100;
    const fs::path folder = g_root / "pc-api";
    lic::resetForTests();
    std::string why;
    check(!lic::noteServiceTime("", "", "", &why) && has(why, "initialize"), "avant initialize : refuse");
    lic::initialize(optionsFor(folder));
    check(lic::getLicenseStatus() == LicenseStatus::Evaluation && lic::getStatusLabel() == "D\xC3\xA9" "couverte", "initialize : la Decouverte");
    check(lic::thisMachineHash() == sv::machineHash("machine-essai-pc-api"), "thisMachineHash");
    check(lic::pendingActivationCode().empty(), "pas de code laisse par l'installateur");
    writeFile(folder / lic::LicenseStore::kPendingCodeFile, "\xEF\xBB\xBF  XPG-7K3M-9QPT-2WXD-H8NB \r\n");
    std::string norm;
    check(lic::pendingActivationCode() == "XPG-7K3M-9QPT-2WXD-H8NB" && sv::normalizeCode(lic::pendingActivationCode(), norm), "le code laisse par l'installateur, relu");
    check(lic::clearPendingActivationCode(&why) && lic::pendingActivationCode().empty() && !fs::exists(folder / lic::LicenseStore::kPendingCodeFile),
          "efface une fois l'activation faite");
    check(lic::clearPendingActivationCode(&why), "deja efface : vrai");
    for (const auto& e : lic::getHistory(0)) check(!has(e.text, "7K3M"), "le code n'est jamais dans l'historique");
    lic::crypto::Signature sig{};
    lic::crypto::PublicKey pub{};
    const std::string msg = sv::requestMessage("POST", "/v1/renouveler", "1793961600", nonce(), "{}");
    check(lic::deviceSignServiceMessage(msg, sig, pub, &why) && lic::crypto::verify(pub, msg, sig) && !lic::thisDeviceFingerprint().empty(),
          "deviceSignServiceMessage : la cle creee, la signature verifiee, l'empreinte a l'ecran");
    check(!lic::deviceSignServiceMessage("autre chose", sig, pub, &why), "deviceSignServiceMessage : seulement XPG-SERVICE-1");
    const std::string keyHex = lic::crypto::hex(pub);
    const std::string station = lic::stationHash("machine-essai-pc-api", pub);
    const lic::License l = onlineLicense("XPG-2026-00190", lic::LicenseType::Floating, T0 - D, 0);
    const auto rep = lic::installActivation(licenseText(l), activationText(activationFor(l, keyHex, station, T0, T0 + 30 * D)), g_cert);
    check(rep.ok && lic::getLicenseStatus() == LicenseStatus::NoFloatingSeat && lic::getLicenseState().floating && lic::getLicenseId() == "XPG-2026-00190",
          "installActivation : Flottante : aucun poste (" + rep.why + ")");
    check(lic::installLease(leaseText(l.licenseId, keyHex, T0, T0 + 900), g_cert, &why) && lic::getLicenseStatus() == LicenseStatus::Valid
              && lic::isFeatureEnabled("ai_generation"),
          "installLease : Valide, les fonctionnalites ouvertes");
    const std::string n = nonce();
    check(lic::noteServiceTime(timeText(T0, n), n, g_cert, &why) && lic::getLicenseState().lastServiceSync == T0, "noteServiceTime : lastServiceSync");
    lic::dropLease();
    check(lic::getLicenseStatus() == LicenseStatus::NoFloatingSeat && !lic::isFeatureEnabled("ai_generation"), "dropLease : Flottante : aucun poste");
    check(lic::installTrialRecord(trialText(keyHex, lic::thisMachineHash(), T0 - D), g_cert, &why), "installTrialRecord : " + why);
    check(lic::dropActivation(l.licenseId, &why) && lic::getLicenseId().empty() && lic::getLicenseStatus() == LicenseStatus::Evaluation
              && lic::getLicenseState().evaluationStartedAt == T0 - D && lic::getLicenseState().evaluationDaysLeft == 29,
          "dropActivation : la licence partie, la Decouverte d'avant reprend (commencee la veille selon le service : 29 jours)");
    lic::resetForTests();
}

} // namespace

int main(int argc, char** argv) {
    g_verbose = std::getenv("XPG_VERBOSE") != nullptr;
    if (argc < 2) {
        std::printf("usage : licensing_activation_test <racine du depot>\n");
        return 2;
    }
    if (!deploy::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    g_fixture = readFile(fs::path(argv[1]) / "tests" / "fixtures" / "licences" / "registre-exemple.json");
    if (g_fixture.empty()) {
        std::printf("le registre d'exemple ne se lit pas (%s)\n", argv[1]);
        return 1;
    }
    std::uint8_t rnd[6];
    deploy::crypto::randomBytes(rnd, sizeof rnd);
    g_root = fs::temp_directory_path() / ("xpg-activation-test-" + deploy::crypto::hex(rnd, sizeof rnd));
    fs::create_directories(g_root);
    g_now = T0 - D;
    std::string why;
    g_editor = adm::createPublisherKey(U(g_root / "admin"), "essai licensing_activation_test", /*test=*/true, &why);
    g_service = makeKey(g_root / "service", "Service essai");
    g_rogue = makeKey(g_root / "inconnue", "Cle inconnue");
    g_otherPc = makeKey(g_root / "autre-pc", "Autre PC");
    if (!g_editor || !g_service || !g_rogue || !g_otherPc) {
        std::printf("les cles d'essai ne se creent pas : %s\n", why.c_str());
        return 1;
    }
    if (!adm::signRegistry(*g_editor, g_fixture, g_now, g_registry, &why)) std::printf("  (registre refuse : %s)\n", why.c_str());
    g_bundled = g_root / "livre" / "registre.xpgreg";
    writeFile(g_bundled, g_registry);
    {
        std::vector<lic::SignedBlock> blocks;
        check(lic::readBlocks(g_registry, blocks, &why) && blocks.size() == 1 && lic::parseRegistry(blocks[0].document, g_reg, &why),
              "le registre d'exemple signe (n. 318)");
    }
    g_cert = certText(T0 - 30 * D, T0 + 365 * D);

    testDocuments();
    testActivation();
    testRefusals();
    testBail();
    testRecalibration();
    testFloating();
    testTrial();
    testBoundUnchanged();
    testApi();

    lic::resetForTests();
    g_editor.reset();
    g_service.reset();
    g_rogue.reset();
    g_otherPc.reset();
    std::error_code ec;
    fs::remove_all(g_root, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
