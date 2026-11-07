// =============================================================================
//  tests/licensing_evaluation_test.cpp - 1.34.0 : la periode d'evaluation de ce PC
// -----------------------------------------------------------------------------
//  Tout se passe dans un dossier temporaire, avec une cle d'editeur d'ESSAI creee
//  pour l'occasion, le registre d'exemple (tests/fixtures/licences : son edition
//  EVALUATION, 30 jours) et une horloge donnee par l'essai :
//    - le premier lancement commence l'evaluation (Evaluation, 30 jours, les
//      fonctionnalites d'EVALUATION) ; jour 12 : 18 jours ; la fin (jour 31) :
//      Absente, evaluationOver, FREE ; l'horloge reculee : Horloge reculee ;
//    - ses copies (etat.bin, evaluation.bin, la ligne de l'historique) : en effacer
//      une, ou deux, ne la fait pas recommencer ; une copie plus tardive ne gagne
//      pas ; un etat.bin d'avant (1.33) se lit ;
//    - jamais prolongee (un registre qui donne 60 jours), raccourcie par un registre
//      qui en donne moins ; une autre edition d'evaluation, une edition retiree ;
//    - une licence importee passe avant ; retiree, l'evaluation d'avant reprend
//      (pas une nouvelle) ; expiree ou revoquee : l'etat d'avant ; un PC qui a eu
//      une licence sans evaluation n'en recoit pas ;
//    - sans cle de l'editeur (NotConfigured) : rien ne commence, rien n'est ecrit ;
//      sans registre, sans edition d'evaluation, ou l'option laissee vide avec des
//      cles donnees (les essais d'avant) : Absente, comme avant ;
//    - un dossier ou ce compte n'ecrit pas ; l'API (Licensing.hpp).
//
//  licensing_evaluation_test <racine du depot>   (XPG_VERBOSE=1 : chaque controle)
//  Aucune donnee client : un << Client d'essai >> et des PC nommes << Poste essai >>.
// =============================================================================
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/licensing/Licensing.hpp"

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
using lic::LicenseStatus;
using lic::Seconds;

namespace {

int g_checks = 0, g_failures = 0;
bool g_verbose = false;
Seconds g_now = 0;

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
std::unique_ptr<lic::SigningKey> g_key;
std::string g_fixture;    // le registre d'exemple (JSON)
std::string g_registry;   // signe (n. 318)
fs::path g_bundled;       // le registre livre avec le programme
const Seconds T0 = T("2026-11-07T10:00:00Z");
constexpr Seconds D = lic::kDay;

lic::TrustedKey trustedOf(const lic::SigningKey& k) {
    lic::TrustedKey t;
    t.id = k.keyId();
    t.name = k.name();
    t.key = k.publicKey();
    t.test = k.test();
    return t;
}

// Un PC du Studio : la cle d'essai, le registre livre, l'horloge de l'essai, l'evaluation donnee.
lic::StoreOptions optionsFor(const fs::path& folder, bool bundled = true) {
    lic::StoreOptions o;
    o.folder = U(folder);
    o.trustedKeys = {trustedOf(*g_key)};
    o.compiledKeys = false;
    o.clock = [] { return g_now; };
    o.machineId = "machine-essai-" + folder.filename().string();
    o.evaluationPeriod = true;
    if (bundled) o.bundledRegistry = U(g_bundled);
    return o;
}

std::string signRegistryJson(const std::string& json) {
    std::string out, why;
    if (!adm::signRegistry(*g_key, json, g_now, out, &why)) std::printf("  (registre refuse : %s)\n", why.c_str());
    return out;
}

// Le registre d'exemple, son numero et la duree de son edition EVALUATION changes.
std::string registryWith(int number, int evaluationDays) {
    std::string j = g_fixture;
    const auto n = j.find("\"number\": 318");
    if (n != std::string::npos) j.replace(n, 13, "\"number\": " + std::to_string(number));
    const auto ev = j.find("\"id\": \"EVALUATION\"");
    const auto d = ev == std::string::npos ? ev : j.find("\"duration_days\": 30", ev);
    if (d != std::string::npos) j.replace(d, 19, "\"duration_days\": " + std::to_string(evaluationDays));
    return signRegistryJson(j);
}

std::string requestOf(lic::LicenseStore& store, const fs::path& folder, const std::string& name) {
    const fs::path out = folder.parent_path() / (folder.filename().string() + ".xpgreq");
    std::string why;
    if (!store.writeActivationRequest(U(out), name, "", "IHM Studio (essai)", &why)) std::printf("  (demande : %s)\n", why.c_str());
    return readFile(out);
}

std::string signLicense(const std::string& params, const std::string& request) {
    adm::LicenseInputs in;
    in.params = params;
    in.requests = {request};
    in.registry = g_registry;
    std::string out, why, summary;
    if (!adm::signLicense(*g_key, in, g_now, out, &why, &summary)) std::printf("  (licence refusee : %s)\n", why.c_str());
    return out;
}

int countKind(const lic::LicenseStore& st, const std::string& kind) {
    int n = 0;
    for (const auto& e : st.history()) n += e.kind == kind ? 1 : 0;
    return n;
}

// Le contenu protege d'un fichier d'etat (etat.bin, evaluation.bin), relu en JSON.
core::json::Value readProtected(const fs::path& p) {
    core::json::Value v;
    deploy::crypto::Bytes sealed, plain;
    std::string why;
    if (!deploy::crypto::readFile(U(p), sealed, &why) || !deploy::crypto::unprotect(sealed, plain, &why)) return v;
    (void)core::json::parse(std::string(plain.begin(), plain.end()), v);
    return v;
}

void writeProtected(const fs::path& p, const std::string& json) {
    deploy::crypto::Bytes plain(json.begin(), json.end()), sealed;
    std::string why;
    if (!deploy::crypto::protect(plain, sealed, true, &why) || !deploy::crypto::writePrivateFile(U(p), sealed, &why))
        std::printf("  (fichier protege non ecrit : %s)\n", why.c_str());
}

bool sameEvaluation(const lic::LicenseState& s, Seconds start, int left) {
    return s.status == LicenseStatus::Evaluation && s.evaluationStartedAt == start && s.evaluationEndsAt == start + 30 * D && s.evaluationDaysLeft == left
           && !s.evaluationOver && s.edition == "EVALUATION";
}

// ======================================================== le premier lancement ==
void testFirstLaunch() {
    std::printf("-- premier lancement : l'evaluation commence (30 jours, EVALUATION)\n");
    g_now = T0;
    const fs::path pc = g_root / "pc-a";
    lic::LicenseStore st(optionsFor(pc));
    check(st.evaluationPeriod() && st.configured(), "l'evaluation est donnee, les licences sont actives");
    auto s = st.evaluate();
    check(s.status == LicenseStatus::Evaluation && std::string(lic::statusKey(s.status)) == "evaluation" && lic::statusLabel(s.status) == "\xC3\x89valuation",
          "Evaluation (cle << evaluation >>, etiquette << \xC3\x89valuation >>) : " + lic::statusLabel(s.status));
    check(sameEvaluation(s, T0, 30) && s.evaluationEdition == "EVALUATION" && s.editionLabel == "EVALUATION",
          "commencee maintenant, 30 jours, l'edition EVALUATION (" + std::to_string(s.evaluationDaysLeft) + " jours)");
    check(s.hasFeature("ai_generation") && s.hasFeature("api_compiler") && s.hasFeature("professional_tools") && s.hasFeature("hmi_export")
              && !s.hasFeature("ai_advanced_models") && !s.hasFeature("multi_user") && !s.hasFeature("floating_licenses") && s.features.size() == 18,
          "les fonctionnalites d'EVALUATION (18) ; ni les modeles avances, ni le multi-utilisateur");
    check(s.maxDevices == 1 && s.type == lic::LicenseType::Evaluation && s.expirationDate() == "2026-12-07" && s.licenseId.empty(),
          "maxDevices de l'edition (1), type evaluation, fin 2026-12-07, pas d'identifiant de licence");
    check(s.message == "\xC3\x89valuation : il te reste 30 jours (jusqu'au 07/12/2026). Pour continuer ensuite, demande une licence (menu Licence\xE2\x80\xA6).",
          "la phrase : " + s.message);
    check(lic::licenseFeaturesApply(s.status) && lic::statusIsNormal(s.status) && !lic::allFeaturesOpen(s.status), "Evaluation : fonctionnalites de l'edition, sans anomalie");
    std::error_code ec;
    check(fs::is_regular_file(pc / lic::LicenseStore::kStateFile, ec) && fs::is_regular_file(pc / lic::LicenseStore::kEvaluationFile, ec),
          "etat.bin et evaluation.bin ecrits");
    auto h = st.history();
    check(h.size() == 2 && h[0].kind == "etat" && h[0].ok && h[0].text == s.message && h[1].kind == "evaluation" && h[1].ok
              && h[1].text == "\xC3\x89valuation commenc\xC3\xA9" "e : 30 jours, jusqu'au 07/12/2026." && h[1].at == T0,
          "historique : << Evaluation commencee : 30 jours, jusqu'au 07/12/2026. >>, puis l'etat (sans << Evaluation : >> repete) (" + std::to_string(h.size()) + ")");
    const auto etat = readProtected(pc / lic::LicenseStore::kStateFile);
    check(etat.str("format") == "xpg-etat-licences" && etat.integer("version", 0) == 1 && etat.str("evaluation_started") == "2026-11-07T10:00:00Z"
              && etat.integer("evaluation_days", 0) == 30 && etat.integer("registry", 0) == 318 && !etat.str("latest_seen").empty(),
          "etat.bin : le meme format (version 1, latest_seen, registry) et deux champs de plus (evaluation_started, evaluation_days)");
    const auto copy = readProtected(pc / lic::LicenseStore::kEvaluationFile);
    check(copy.str("format") == "xpg-evaluation" && copy.str("started") == "2026-11-07T10:00:00Z" && copy.integer("days", 0) == 30,
          "evaluation.bin : le debut et les jours");
    s = st.evaluate();
    check(sameEvaluation(s, T0, 30) && st.history().size() == 2, "une deuxieme evaluation : la meme, rien de plus dans l'historique");
    lic::LicenseStore other(optionsFor(pc));   // l'autre Studio de ce PC
    s = other.evaluate();
    check(sameEvaluation(s, T0, 30) && countKind(other, "evaluation") == 1 && other.history().size() == 2, "l'autre programme de ce PC : la meme evaluation");
}

// ================================================= les jours, les copies, l'horloge ==
void testDaysAndCopies() {
    std::printf("-- jour 12 : 18 jours ; une copie effacee a la fois ; l'horloge reculee ; la fin\n");
    const fs::path pc = g_root / "pc-a";
    lic::LicenseStore st(optionsFor(pc));
    g_now = T0 + 12 * D;
    auto s = st.evaluate();
    check(sameEvaluation(s, T0, 18) && has(s.message, "il te reste 18 jours (jusqu'au 07/12/2026)"), "jour 12 : 18 jours (" + s.message + ")");
    g_now = T0 + 12 * D + 3600;
    check(st.evaluate().evaluationDaysLeft == 18, "jour 12 et une heure : 18 jours (arrondi au jour superieur)");
    check(st.history().size() == 2, "les jours qui passent n'ecrivent rien dans l'historique");

    std::error_code ec;
    const fs::path etat = pc / lic::LicenseStore::kStateFile, copy = pc / lic::LicenseStore::kEvaluationFile, hist = pc / lic::LicenseStore::kHistoryFile;
    // etat.bin efface : repris des autres copies, et recree (une ligne << controle >>).
    fs::remove(etat, ec);
    s = st.evaluate();
    check(sameEvaluation(s, T0, 18) && readProtected(etat).str("evaluation_started") == "2026-11-07T10:00:00Z", "etat.bin efface : la meme evaluation ; etat.bin refait avec son debut");
    bool controle = false;
    for (const auto& e : st.history()) controle = controle || (e.kind == "controle" && has(e.text, "pendant l'\xC3\xA9valuation"));
    check(controle && has(s.detail, "recr\xC3\xA9\xC3\xA9"), "etat.bin efface : << Etat recree ... pendant l'evaluation >> dans l'historique, detail le dit");
    // evaluation.bin efface.
    fs::remove(copy, ec);
    s = st.evaluate();
    check(sameEvaluation(s, T0, 18) && readProtected(copy).str("started") == "2026-11-07T10:00:00Z", "evaluation.bin efface : la meme evaluation ; la copie est remise");
    // l'historique efface.
    fs::remove(hist, ec);
    s = st.evaluate();
    check(sameEvaluation(s, T0, 18) && countKind(st, "evaluation") == 1, "historique efface : la meme evaluation ; sa ligne est remise");
    {
        const auto h = st.history();
        bool back = false;
        for (const auto& e : h) back = back || (e.kind == "evaluation" && has(e.text, "commenc\xC3\xA9" "e le 07/11/2026") && has(e.text, "jusqu'au 07/12/2026"));
        check(back, "la ligne remise dit le debut et la fin");
    }
    // deux copies a la fois.
    fs::remove(etat, ec);
    fs::remove(copy, ec);
    s = lic::LicenseStore(optionsFor(pc)).evaluate();
    check(sameEvaluation(s, T0, 18), "etat.bin et evaluation.bin effaces : repris de l'historique");
    fs::remove(copy, ec);
    fs::remove(hist, ec);
    s = lic::LicenseStore(optionsFor(pc)).evaluate();
    check(sameEvaluation(s, T0, 18), "evaluation.bin et l'historique effaces : repris d'etat.bin");
    fs::remove(etat, ec);
    fs::remove(hist, ec);
    s = lic::LicenseStore(optionsFor(pc)).evaluate();
    check(sameEvaluation(s, T0, 18), "etat.bin et l'historique effaces : repris d'evaluation.bin");
    writeFile(copy, "abime");
    s = lic::LicenseStore(optionsFor(pc)).evaluate();
    check(sameEvaluation(s, T0, 18) && readProtected(copy).str("started") == "2026-11-07T10:00:00Z", "evaluation.bin abime : la meme evaluation, la copie refaite");
    // une copie qui dit plus tard (et plus de jours) ne gagne pas : le debut le plus ancien, les jours les moins nombreux.
    writeProtected(copy, "{\"format\": \"xpg-evaluation\", \"version\": 1, \"started\": \"2026-11-17T10:00:00Z\", \"days\": 60}");
    s = lic::LicenseStore(optionsFor(pc)).evaluate();
    check(sameEvaluation(s, T0, 18) && readProtected(copy).str("started") == "2026-11-07T10:00:00Z" && readProtected(copy).integer("days", 0) == 30,
          "une copie plus tardive (et 60 jours) : ignoree, remise au plus ancien");
    check(fs::is_regular_file(etat, ec) && fs::is_regular_file(copy, ec) && countKind(st, "evaluation") >= 1, "toutes les copies sont de nouveau la");

    // L'horloge reculee de plus de 24 h pendant l'evaluation : Horloge reculee (FREE) ; moins : rien.
    g_now = T0 + 9 * D;
    s = st.evaluate();
    check(s.status == LicenseStatus::ClockRolledBack && s.edition == "FREE" && s.hasFeature("api_editor") && !s.hasFeature("ai_generation")
              && s.evaluationStartedAt == T0 && has(s.message, "remets-la"),
          "horloge reculee de 3 jours : Horloge reculee, FREE (" + s.message + ")");
    g_now = T0 + 11 * D + 12 * 3600;
    s = st.evaluate();
    check(s.status == LicenseStatus::Evaluation && s.evaluationDaysLeft == 19, "horloge reculee de 12 h (tolere) : Evaluation, 19 jours");
    g_now = T0 + 12 * D;
    check(sameEvaluation(st.evaluate(), T0, 18), "horloge remise : Evaluation, 18 jours");

    // Le dernier jour, puis la fin.
    g_now = T0 + 29 * D + 12 * 3600;
    s = st.evaluate();
    check(s.status == LicenseStatus::Evaluation && s.evaluationDaysLeft == 1 && has(s.message, "il te reste 1 jour ("), "jour 29 et demi : << il te reste 1 jour >> (" + s.message + ")");
    g_now = T0 + 30 * D;
    s = st.evaluate();
    check(s.status == LicenseStatus::Absent && s.evaluationOver, "a la fin exacte : finie");
    g_now = T0 + 31 * D;
    s = st.evaluate();
    check(s.status == LicenseStatus::Absent && s.evaluationOver && s.evaluationDaysLeft == 0 && s.evaluationStartedAt == T0 && s.evaluationEndsAt == T0 + 30 * D
              && s.evaluationEdition == "EVALUATION",
          "jour 31 : Absente, evaluationOver, ses dates");
    check(s.edition == "FREE" && s.hasFeature("api_editor") && s.hasFeature("hmi_runtime") && !s.hasFeature("ai_generation") && !s.hasFeature("api_compiler")
              && s.maxDevices == 1 && s.expiresAt == 0,
          "jour 31 : l'edition FREE (l'editeur, le poste ; ni la generation IA, ni la compilation)");
    check(s.message
              == "Ton \xC3\xA9valuation de 30 jours est finie depuis le 07/12/2026 : demande une licence (menu Licence\xE2\x80\xA6). En attendant, l'\xC3\xA9"
                 "dition FREE s'applique.",
          "la phrase : " + s.message);
    {
        const auto h = st.history();
        check(!h.empty() && h[0].kind == "etat" && h[0].ok && has(h[0].text, "\xC3\x89valuation \xE2\x86\x92 Absente : Ton \xC3\xA9valuation de 30 jours"),
              "historique : << Evaluation -> Absente : Ton evaluation de 30 jours est finie... >> (" + (h.empty() ? std::string() : h[0].text) + ")");
    }
    // Finie : effacer une copie, ou deux, ne la fait pas recommencer.
    fs::remove(etat, ec);
    fs::remove(copy, ec);
    s = lic::LicenseStore(optionsFor(pc)).evaluate();
    check(s.status == LicenseStatus::Absent && s.evaluationOver && s.evaluationStartedAt == T0, "finie, etat.bin et evaluation.bin effaces : toujours finie (l'historique)");
    fs::remove(hist, ec);
    s = lic::LicenseStore(optionsFor(pc)).evaluate();
    check(s.status == LicenseStatus::Absent && s.evaluationOver && s.evaluationStartedAt == T0, "finie, puis l'historique efface : toujours finie (les copies remises)");
}

// =============================================================== jamais prolongee ==
void testNeverExtended() {
    std::printf("-- jamais prolongee : un registre qui donne 60 jours, puis 14\n");
    g_now = T0;
    const fs::path pc = g_root / "pc-b";
    lic::LicenseStore st(optionsFor(pc));
    check(sameEvaluation(st.evaluate(), T0, 30), "pc-b : l'evaluation commence");
    g_now = T0 + 20 * D;
    auto rep = st.importBytes(registryWith(319, 60));
    check(rep.ok && rep.state.registryNumber == 319 && sameEvaluation(rep.state, T0, 10), "registre n. 319 (EVALUATION : 60 jours) : toujours 30 jours, 10 restent");
    rep = st.importBytes(registryWith(320, 14));
    check(rep.ok && rep.state.status == LicenseStatus::Absent && rep.state.evaluationOver && rep.state.evaluationEndsAt == T0 + 14 * D
              && has(rep.state.message, "de 14 jours est finie depuis le 21/11/2026"),
          "registre n. 320 (14 jours) : raccourcie, finie (" + rep.state.message + ")");
    rep = st.importBytes(registryWith(321, 30));
    check(rep.ok && rep.state.status == LicenseStatus::Absent && rep.state.evaluationOver && rep.state.evaluationEndsAt == T0 + 14 * D,
          "registre n. 321 (30 jours de nouveau) : toujours finie (jamais prolongee)");
    check(countKind(st, "evaluation") == 1, "une seule ligne << evaluation >>");
}

// ======================================================= l'edition d'evaluation ==
void testEditionChoice() {
    std::printf("-- l'edition d'evaluation : une autre que EVALUATION, retiree, absente\n");
    g_now = T0;
    // EVALUATION retiree, une edition ESSAI de type evaluation sans duree (30 jours).
    std::string j = g_fixture;
    const auto ev = j.find("\"id\": \"EVALUATION\",");
    j.insert(ev + std::string("\"id\": \"EVALUATION\",").size(), " \"retired\": true,");
    const auto eds = j.find("\"editions\": [");
    j.insert(eds + 13, "{\"id\": \"ESSAI\", \"label\": \"Essai\", \"type\": \"evaluation\", \"max_devices\": 2, \"duration_days\": 0, \"offline_grace_days\": 0, "
                       "\"features\": [{\"id\": \"api_editor\"}, {\"id\": \"api_compiler\"}]}, ");
    const std::string reg = signRegistryJson(j);
    check(!reg.empty(), "un registre : EVALUATION retiree, ESSAI (type evaluation, sans duree)");
    const fs::path pc = g_root / "pc-essai";
    lic::LicenseStore st(optionsFor(pc, false));
    (void)st.importBytes(reg);
    auto s = st.evaluate();
    check(s.status == LicenseStatus::Evaluation && s.edition == "ESSAI" && s.evaluationEdition == "ESSAI" && s.evaluationDaysLeft == 30 && s.maxDevices == 2
              && s.hasFeature("api_compiler") && !s.hasFeature("ai_generation"),
          "l'edition ESSAI (la premiere de type evaluation), 0 jour -> 30 jours, ses fonctionnalites et son maxDevices");
    // Sans edition d'evaluation : Absente, comme avant.
    std::string k = g_fixture;
    const auto evk = k.find("\"id\": \"EVALUATION\"");
    const auto tk = k.find("\"type\": \"evaluation\"", evk);
    k.replace(tk, 20, "\"type\": \"temporary\"");
    const fs::path pn = g_root / "pc-sans-edition";
    lic::LicenseStore sn(optionsFor(pn, false));
    auto rep = sn.importBytes(signRegistryJson(k));
    std::error_code ec;
    check(rep.ok && rep.state.status == LicenseStatus::Absent && rep.state.edition == "FREE" && !rep.state.evaluationOver && rep.state.evaluationStartedAt == 0
              && rep.state.message == "Aucune licence sur ce PC : l'\xC3\xA9" "dition FREE s'applique." && !fs::exists(pn / lic::LicenseStore::kEvaluationFile, ec),
          "aucune edition d'evaluation au registre : Absente, FREE, comme avant (" + rep.state.message + ")");
}

// ======================================================= une licence, puis retiree ==
void testLicense() {
    std::printf("-- une licence importee passe avant ; retiree, expiree, revoquee : pas de nouvelle evaluation\n");
    g_now = T0;
    const fs::path pc = g_root / "pc-c";
    lic::LicenseStore st(optionsFor(pc));
    check(sameEvaluation(st.evaluate(), T0, 30), "pc-c : l'evaluation commence");
    g_now = T0 + 5 * D;
    const std::string req = requestOf(st, pc, "Poste essai C");
    const std::string licFile = signLicense("{\"license_id\": \"XPG-EVAL-0001\", \"customer\": \"Client d'essai\", \"edition\": \"PERSONAL\", \"duration_days\": 365}", req);
    auto rep = st.importBytes(licFile);
    auto s = rep.state;
    check(rep.ok && rep.licenseInstalled && s.status == LicenseStatus::Valid && s.edition == "PERSONAL" && s.licenseId == "XPG-EVAL-0001",
          "la licence PERSONAL importee : Valide (" + rep.why + ")");
    check(s.evaluationStartedAt == 0 && s.evaluationEndsAt == 0 && s.evaluationDaysLeft == 0 && !s.evaluationOver && s.evaluationEdition.empty(),
          "avec une licence : l'evaluation ne compte plus (ses champs vides)");
    {
        const auto h = st.history();
        check(!h.empty() && h[0].kind == "etat" && has(h[0].text, "\xC3\x89valuation \xE2\x86\x92 Valide"), "historique : << Evaluation -> Valide >>");
    }
    std::error_code ec;
    check(readProtected(pc / lic::LicenseStore::kStateFile).str("evaluation_started") == "2026-11-07T10:00:00Z", "etat.bin garde le debut de l'evaluation");
    // La licence retiree (le fichier efface) : l'evaluation d'avant reprend, pas une nouvelle.
    fs::remove(pc / lic::LicenseStore::kLicenseFile, ec);
    s = st.evaluate();
    check(sameEvaluation(s, T0, 25), "licence effacee au jour 5 : l'evaluation commencee le 07/11 reprend, 25 jours (pas 30)");
    g_now = T0 + 40 * D;
    s = st.evaluate();
    check(s.status == LicenseStatus::Absent && s.evaluationOver, "jour 40 sans licence : finie");
    // Remise, puis revoquee : Revoquee, comme avant.
    g_now = T0 + 41 * D;
    rep = st.importBytes(licFile);
    check(rep.ok && rep.state.status == LicenseStatus::Valid && !rep.state.evaluationOver, "la licence remise : Valide");
    std::string rv, why;
    check(adm::signRevocations(*g_key, "{\"number\": 1, \"revoked\": [{\"license_id\": \"XPG-EVAL-0001\", \"reason\": \"Essai\"}]}", g_now, rv, &why),
          "une revocation signee (" + why + ")");
    rep = st.importBytes(rv);
    check(rep.ok && rep.state.status == LicenseStatus::Revoked && rep.state.edition == "FREE" && !rep.state.evaluationOver && rep.state.evaluationStartedAt == 0,
          "revoquee : Revoquee, FREE, comme avant (pas d'evaluation)");
    fs::remove(pc / lic::LicenseStore::kLicenseFile, ec);
    s = st.evaluate();
    check(s.status == LicenseStatus::Absent && s.evaluationOver && s.evaluationStartedAt == T0, "revoquee puis effacee : Absente, l'evaluation finie (pas de nouvelle)");

    // Une licence datee qui prend fin : Expiree, comme avant ; effacee : l'evaluation (finie) ne recommence pas.
    g_now = T0;
    const fs::path pd = g_root / "pc-d";
    lic::LicenseStore sd(optionsFor(pd));
    (void)sd.evaluate();
    g_now = T0 + 2 * D;
    const std::string reqD = requestOf(sd, pd, "Poste essai D");
    rep = sd.importBytes(signLicense("{\"license_id\": \"XPG-EVAL-0002\", \"customer\": \"Client d'essai\", \"edition\": \"PERSONAL\", "
                                     "\"expires_at\": \"2026-11-17T10:00:00Z\", \"offline_grace_days\": 0}",
                                     reqD));
    check(rep.ok && rep.state.status == LicenseStatus::Valid, "pc-d : une licence de 10 jours (" + rep.why + ")");
    g_now = T0 + 45 * D;
    s = sd.evaluate();
    check(s.status == LicenseStatus::Expired && s.evaluationStartedAt == 0 && !s.evaluationOver, "jour 45 : Expiree, comme avant");
    fs::remove(pd / lic::LicenseStore::kLicenseFile, ec);
    s = sd.evaluate();
    check(s.status == LicenseStatus::Absent && s.evaluationOver && s.evaluationStartedAt == T0, "expiree puis effacee : l'evaluation (finie) ne recommence pas");

    // Un PC qui a eu une licence avant toute evaluation : pas de periode d'evaluation, meme sans elle.
    g_now = T0;
    const fs::path pe = g_root / "pc-e";
    lic::LicenseStore se(optionsFor(pe, false));
    const std::string reqE = requestOf(se, pe, "Poste essai E");
    rep = se.importBytes(signLicense("{\"license_id\": \"XPG-EVAL-0003\", \"customer\": \"Client d'essai\", \"edition\": \"PROFESSIONAL\", \"duration_days\": 365}", reqE));
    check(rep.ok && rep.state.status == LicenseStatus::Valid && rep.state.evaluationStartedAt == 0 && !fs::exists(pe / lic::LicenseStore::kEvaluationFile, ec),
          "pc-e : la licence d'abord - Valide, aucune evaluation commencee");
    fs::remove(pe / lic::LicenseStore::kLicenseFile, ec);
    s = se.evaluate();
    check(s.status == LicenseStatus::Absent && s.evaluationStartedAt == 0 && !s.evaluationOver && s.evaluationEdition.empty() && s.edition == "FREE"
              && has(s.detail, "une licence a d\xC3\xA9j\xC3\xA0"),
          "pc-e, la licence effacee : Absente (FREE), pas de nouvelle evaluation ; detail le dit (" + s.detail + ")");
    check(!fs::exists(pe / lic::LicenseStore::kEvaluationFile, ec) && countKind(se, "evaluation") == 0, "pc-e : rien de l'evaluation n'est ecrit");
}

// ====================================================== sans cle, sans registre ==
void testNotApplicable() {
    std::printf("-- sans cle de l'editeur, sans registre, l'option laissee vide : comme avant\n");
    g_now = T0;
    std::error_code ec;
    // NotConfigured : rien ne commence, rien n'est ecrit.
    const fs::path pn = g_root / "pc-sans-cle";
    lic::StoreOptions o = optionsFor(pn);
    o.trustedKeys.clear();
    o.compiledKeys = false;
    lic::LicenseStore sn(o);
    auto s = sn.evaluate();
    check(s.status == LicenseStatus::NotConfigured && s.evaluationStartedAt == 0 && !s.evaluationOver && s.hasFeature("multi_user"),
          "sans cle de l'editeur : Non activees, tout ouvert, pas d'evaluation");
    check(!fs::exists(pn, ec), "sans cle : rien n'est ecrit (pas meme le dossier)");
    g_now = T0 + 40 * D;
    check(sn.evaluate().status == LicenseStatus::NotConfigured && !fs::exists(pn, ec), "sans cle, 40 jours plus tard : toujours Non activees, rien d'ecrit");
    g_now = T0;

    // Sans registre : Absente, comme avant ; rien de l'evaluation.
    const fs::path pr = g_root / "pc-sans-registre";
    lic::LicenseStore sr(optionsFor(pr, false));
    s = sr.evaluate();
    check(s.status == LicenseStatus::Absent && s.edition.empty() && has(s.message, "registre") && s.evaluationStartedAt == 0 && s.evaluationEdition.empty()
              && !fs::exists(pr / lic::LicenseStore::kEvaluationFile, ec) && countKind(sr, "evaluation") == 0,
          "sans registre : Absente (la phrase dit que le registre manque), aucune evaluation");
    // Le registre arrive plus tard : l'evaluation commence alors.
    g_now = T0 + 3 * D;
    auto rep = sr.importBytes(g_registry);
    check(rep.ok && sameEvaluation(rep.state, T0 + 3 * D, 30), "le registre importe 3 jours plus tard : l'evaluation commence alors (30 jours)");
    {
        const auto h = sr.history();
        // le plus recent d'abord : l'etat (apres l'import), l'import, le debut de l'evaluation, l'etat d'avant
        check(h.size() == 4 && h[0].kind == "etat" && has(h[0].text, "Absente \xE2\x86\x92 \xC3\x89valuation : il te reste 30 jours") && h[1].kind == "import"
                  && h[2].kind == "evaluation" && h[3].kind == "etat",
              "historique : l'etat de depart, l'evaluation commencee, l'import, << Absente -> Evaluation >> (" + std::to_string(h.size()) + ")");
    }
    g_now = T0;

    // L'option laissee vide avec des cles donnees (les essais d'avant, les outils) : pas d'evaluation.
    const fs::path pv = g_root / "pc-option-vide";
    lic::StoreOptions ov = optionsFor(pv);
    ov.evaluationPeriod.reset();
    lic::LicenseStore sv(ov);
    s = sv.evaluate();
    check(!sv.evaluationPeriod() && s.status == LicenseStatus::Absent && s.edition == "FREE" && s.evaluationStartedAt == 0
              && !fs::exists(pv / lic::LicenseStore::kEvaluationFile, ec),
          "option vide, cles donnees : Absente (FREE), comme avant");
    lic::StoreOptions oc;
    oc.folder = U(g_root / "pc-compile");
    oc.clock = [] { return g_now; };
    check(lic::LicenseStore(oc).evaluationPeriod(), "option vide, les cles compilees (le Studio livre) : l'evaluation est donnee");
    lic::StoreOptions of = ov;
    of.evaluationPeriod = false;
    check(!lic::LicenseStore(of).evaluationPeriod(), "option a faux : pas d'evaluation");
}

// ================================================== un etat.bin d'avant (1.33) ==
void testOldState() {
    std::printf("-- un etat.bin d'avant (1.33) ; l'horloge en arriere avant le debut\n");
    const fs::path pc = g_root / "pc-ancien";
    fs::create_directories(pc);
    writeProtected(pc / lic::LicenseStore::kStateFile,
                   "{\"format\": \"xpg-etat-licences\", \"version\": 1, \"latest_seen\": \"2026-11-12T10:00:00Z\", \"registry\": 318, \"revocations\": 0}");
    g_now = T0;   // 5 jours avant l'heure la plus tardive deja vue
    lic::LicenseStore st(optionsFor(pc));
    auto s = st.evaluate();
    std::error_code ec;
    check(s.status == LicenseStatus::ClockRolledBack && s.evaluationStartedAt == 0 && s.evaluationEdition == "EVALUATION" && has(s.detail, "commencera")
              && !fs::exists(pc / lic::LicenseStore::kEvaluationFile, ec),
          "horloge en arriere avant le debut : Horloge reculee, l'evaluation ne commence pas encore (" + s.detail + ")");
    g_now = T0 + 5 * D;
    s = st.evaluate();
    check(sameEvaluation(s, T0 + 5 * D, 30), "horloge remise : l'evaluation commence (30 jours)");
    const auto etat = readProtected(pc / lic::LicenseStore::kStateFile);
    check(etat.integer("registry", 0) == 318 && etat.str("evaluation_started") == "2026-11-12T10:00:00Z" && s.registryProblem.empty(),
          "l'etat.bin d'avant se lit (le registre n. 318 garde) et recoit le debut de l'evaluation");
}

// ================================================== un dossier non inscriptible ==
void testReadOnly() {
    std::printf("-- un dossier ou ce compte ne peut pas ecrire\n");
    std::error_code ec;
    // Un debut deja note : l'evaluation se calcule, detail le dit.
    g_now = T0;
    const fs::path pc = g_root / "pc-lecture";
    {
        lic::LicenseStore first(optionsFor(pc));
        (void)first.evaluate();
    }
    fs::remove(pc / lic::LicenseStore::kStateFile, ec);
    fs::remove(pc / lic::LicenseStore::kHistoryFile, ec);
    writeFile(pc / lic::LicenseStore::kStateFile / "occupe.txt", "x");     // des dossiers a la place : ils ne se remplacent plus
    writeFile(pc / lic::LicenseStore::kHistoryFile / "occupe.txt", "x");
    g_now = T0 + 4 * D;
    lic::LicenseStore second(optionsFor(pc));
    auto s = second.evaluate();
    check(sameEvaluation(s, T0, 26) && has(s.detail, "ne peut pas \xC3\xA9" "crire") && !has(s.detail, "pas pu \xC3\xAA" "tre not\xC3\xA9"),
          "un debut note (evaluation.bin) : l'evaluation se calcule (26 jours), detail dit le dossier (" + s.detail + ")");

    // Rien ne peut noter le debut : l'evaluation est donnee depuis ce lancement, et detail le dit.
    g_now = T0;
    const fs::path pn = g_root / "pc-rien";
    for (const char* f : {lic::LicenseStore::kStateFile, lic::LicenseStore::kEvaluationFile, lic::LicenseStore::kHistoryFile})
        writeFile(pn / f / "occupe.txt", "x");
    lic::LicenseStore st(optionsFor(pn));
    s = st.evaluate();
    check(sameEvaluation(s, T0, 30) && has(s.detail, "pas pu \xC3\xAA" "tre not\xC3\xA9") && has(s.detail, "d'un lancement \xC3\xA0 l'autre"),
          "rien ne s'ecrit : Evaluation depuis ce lancement, detail le dit (" + s.detail + ")");
    g_now = T0 + 3 * D;
    s = st.evaluate();
    check(sameEvaluation(s, T0, 27), "le meme programme, 3 jours plus tard : le debut de ce lancement tient (27 jours)");
    s = lic::LicenseStore(optionsFor(pn)).evaluate();
    check(s.status == LicenseStatus::Evaluation && s.evaluationStartedAt == T0 + 3 * D && has(s.detail, "pas pu \xC3\xAA" "tre not\xC3\xA9"),
          "un nouveau lancement : rien n'avait pu etre note, elle repart - et detail le dit toujours");
}

// ============================================================================ l'API ==
void testApi() {
    std::printf("-- l'API (Licensing.hpp) pendant l'evaluation\n");
    g_now = T0;
    const fs::path pc = g_root / "pc-api";
    lic::initialize(optionsFor(pc));
    check(lic::getLicenseStatus() == LicenseStatus::Evaluation && lic::getEdition() == "EVALUATION" && lic::isFeatureEnabled("ai_generation")
              && !lic::isFeatureEnabled("multi_user") && lic::getMaxDevices() == 1 && lic::getExpirationDate() == "2026-12-07" && lic::getLicenseId().empty()
              && lic::getStatusLabel() == "\xC3\x89valuation",
          "API : Evaluation, EVALUATION, ses fonctionnalites, maxDevices 1, la fin, pas de licence");
    check(has(lic::getStatusMessage(), "il te reste 30 jours"), "API : la phrase (" + lic::getStatusMessage() + ")");
    const std::string locked = lic::lockedMessage("multi_user");
    check(has(locked, "n'est pas incluse dans l'\xC3\xA9valuation (EVALUATION)") && has(locked, "Enregistrer") && !has(locked, "ta licence"),
          "API : la phrase d'une entree fermee pendant l'evaluation (" + locked + ")");
    const auto st = lic::getLicenseState();
    check(st.evaluationStartedAt == T0 && st.evaluationDaysLeft == 30 && st.evaluationEdition == "EVALUATION", "API : getLicenseState porte l'evaluation");
    bool line = false;
    for (const auto& e : lic::getHistory(0)) line = line || e.kind == "evaluation";
    check(line, "API : getHistory a la ligne << evaluation >>");
    g_now = T0 + 12 * D;
    check(lic::refresh().evaluationDaysLeft == 18 && has(lic::getStatusMessage(), "il te reste 18 jours"), "API : refresh au jour 12 -> 18 jours");
    lic::resetForTests();
}

} // namespace

int main(int argc, char** argv) {
    g_verbose = std::getenv("XPG_VERBOSE") != nullptr;
    if (argc < 2) {
        std::printf("usage : licensing_evaluation_test <racine du depot>\n");
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
    g_root = fs::temp_directory_path() / ("xpg-evaluation-test-" + deploy::crypto::hex(rnd, sizeof rnd));
    fs::create_directories(g_root);
    g_now = T0 - D;
    std::string why;
    g_key = adm::createPublisherKey(U(g_root / "admin"), "essai licensing_evaluation_test", /*test=*/true, &why);
    if (!g_key) {
        std::printf("la cle d'editeur d'essai ne se cree pas : %s\n", why.c_str());
        return 1;
    }
    g_registry = signRegistryJson(g_fixture);
    g_bundled = g_root / "livre" / "registre.xpgreg";
    writeFile(g_bundled, g_registry);
    check(!g_registry.empty(), "le registre d'exemple signe (n. 318, emis la veille)");

    testFirstLaunch();
    testDaysAndCopies();
    testNeverExtended();
    testEditionChoice();
    testLicense();
    testNotApplicable();
    testOldState();
    testReadOnly();
    testApi();

    lic::resetForTests();
    g_key.reset();
    std::error_code ec;
    fs::remove_all(g_root, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
