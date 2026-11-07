// =============================================================================
//  licensing/Store.cpp - 1.30.0 : les licences de ce PC (voir Store.hpp)
// =============================================================================
#include "Store.hpp"

#include "KeyFile.hpp"
#include "PublisherKey.hpp"
#include "TestPublisherKey.hpp"   // vide sans XPG_LICENCES_CLE_ESSAI (l'option CMake) : la cle d'essai n'entre pas

#include "../core/Json.hpp"
#include "../deploy/Paths.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>   // 1.35.0 (revue 9) : le proprietaire d'un fichier (GetNamedSecurityInfoW)
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace licensing {

namespace {

std::filesystem::path P(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(P(path), ec);
}

bool readAll(const std::string& path, std::string& out, std::string* why) {
    crypto::Bytes raw;
    if (!crypto::readFile(path, raw, why, kMaxSignedFileBytes)) return false;
    out.assign(raw.begin(), raw.end());
    return true;
}

bool writeAll(const std::string& path, std::string_view text, std::string* why) {
    crypto::Bytes bytes(text.begin(), text.end());
    return crypto::writePrivateFile(path, bytes, why);
}

std::string plural(long long n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

std::string baseName(const std::string& path) {
    const auto cut = path.find_last_of("/\\");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

// Une note de plus dans `detail` (une seule fois).
void addDetail(LicenseState& s, const std::string& note) {
    if (note.empty() || s.detail.find(note) != std::string::npos) return;
    s.detail = s.detail.empty() ? note : s.detail + " ; " + note;
}

constexpr const char* kNotConfiguredMessage =
    "Les licences ne sont pas encore actives dans ce programme : il ne contient pas encore la cl\xC3\xA9 de l'\xC3\xA9" "diteur. "
    "Toutes les fonctionnalit\xC3\xA9s sont ouvertes.";
constexpr const char* kNotConfiguredImport =
    "Ce programme ne peut pas encore v\xC3\xA9rifier une licence : il ne contient pas la cl\xC3\xA9 de l'\xC3\xA9" "diteur ; mets-le \xC3\xA0 jour.";

// ---- l'historique (historique.jsonl) : les lignes lisibles, dans l'ordre du fichier ----
std::vector<core::json::Value> readHistoryLines(const std::string& path, std::vector<std::string>* raw) {
    std::vector<core::json::Value> out;
    if (!fileExists(path)) return out;
    crypto::Bytes bytes;
    std::string why;
    if (!crypto::readFile(path, bytes, &why, 8u * 1024u * 1024u)) return out;
    const std::string text(bytes.begin(), bytes.end());
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        core::json::Value v;
        if (line.empty() || !core::json::parse(line, v) || !v.isObject() || v.str("kind").empty()) continue;   // illisible : ignoree
        Seconds t = 0;
        if (!parseIsoTime(v.str("at"), t)) continue;
        out.push_back(std::move(v));
        if (raw) raw->push_back(std::move(line));
    }
    return out;
}

// ---- l'etat protege (etat.bin) ----
struct State {
    Seconds       latestSeen{0};
    std::uint64_t registryMax{0};
    std::uint64_t revocationsMax{0};
    std::map<std::string, Seconds> issued;   // 1.32.0 : par licence, l'emission la plus recente installee (M2)
    Seconds       evaluationStarted{0};      // 1.34.0 : le debut de l'evaluation de ce PC (0 : pas commencee)
    int           evaluationDays{0};         // 1.34.0 : ses jours, notes a son debut (0 : pas dits)
    // 1.35.0 : le compteur du bail (voir Store.hpp) - 0 : aucune heure signee recue
    Seconds       serviceTime{0};            // la derniere heure signee du service
    Seconds       serviceElapsed{0};         // le temps ecoule depuis (ne fait qu'avancer)
    Seconds       serviceWall{0};            // l'horloge du PC la plus avancee deja comptee
    bool          found{false};
};

constexpr std::size_t kIssuedMax = 256;   // borne : les plus anciennes emissions partent d'abord

constexpr std::string_view kStateFormat = "xpg-etat-licences";

State readState(const std::string& path, std::string* note) {
    State s;
    if (!fileExists(path)) return s;
    crypto::Bytes sealed, plain;
    std::string why;
    if (!crypto::readFile(path, sealed, &why, 64u * 1024u) || !crypto::unprotect(sealed, plain, &why)) {
        if (note) *note = "l'\xC3\xA9tat prot\xC3\xA9g\xC3\xA9 de ce PC est illisible (" + why + ") : il est refait";
        return s;
    }
    core::json::Value v;
    const std::string text(plain.begin(), plain.end());
    if (!core::json::parse(text, v) || !v.isObject() || v.str("format") != kStateFormat) {
        if (note) *note = "l'\xC3\xA9tat prot\xC3\xA9g\xC3\xA9 de ce PC est ab\xC3\xAEm\xC3\xA9 : il est refait";
        return s;
    }
    Seconds t = 0;
    if (parseIsoTime(v.str("latest_seen"), t)) s.latestSeen = t;
    s.registryMax = static_cast<std::uint64_t>(std::max<std::int64_t>(0, v.integer("registry", 0)));
    s.revocationsMax = static_cast<std::uint64_t>(std::max<std::int64_t>(0, v.integer("revocations", 0)));
    if (const core::json::Value* m = v.find("issued"); m && m->isObject())
        for (const auto& [id, x] : m->members()) {
            Seconds at = 0;
            if (isLicenseId(id) && x.isString() && parseIsoTime(x.asString(), at)) s.issued[id] = std::max(s.issued[id], at);
        }
    // 1.34.0 : l'evaluation (un etat d'avant n'en a pas : 0)
    if (parseIsoTime(v.str("evaluation_started"), t) && t > 0) {
        s.evaluationStarted = t;
        s.evaluationDays = static_cast<int>(std::clamp<std::int64_t>(v.integer("evaluation_days", 0), 0, 3650));
    }
    // 1.35.0 : le compteur du bail (un etat d'avant n'en a pas : 0)
    if (parseIsoTime(v.str("service_time"), t) && t > 0) {
        s.serviceTime = t;
        s.serviceElapsed = std::max<std::int64_t>(0, v.integer("service_elapsed", 0));
        if (parseIsoTime(v.str("service_wall"), t)) s.serviceWall = t;
    }
    s.found = true;
    return s;
}

bool writeState(const std::string& path, const State& s, std::string* why) {
    core::json::Value v = core::json::Value::object();
    v.set("format", std::string(kStateFormat));
    v.set("version", 1);
    v.set("latest_seen", isoTime(s.latestSeen));
    v.set("registry", static_cast<std::uint64_t>(s.registryMax));
    v.set("revocations", static_cast<std::uint64_t>(s.revocationsMax));
    if (!s.issued.empty()) {
        // la 1.31 ignore ce champ : l'etat reste lisible dans les deux sens
        std::vector<std::pair<Seconds, std::string>> keep;
        for (const auto& [id, at] : s.issued) keep.emplace_back(at, id);
        std::sort(keep.begin(), keep.end(), std::greater<>());
        if (keep.size() > kIssuedMax) keep.resize(kIssuedMax);
        core::json::Value m = core::json::Value::object();
        for (const auto& [at, id] : keep) m.set(id, isoTime(at));
        v.set("issued", std::move(m));
    }
    if (s.evaluationStarted > 0) {
        // 1.34.0 : deux champs de plus, ignores par la 1.33 (l'etat reste lisible dans les deux sens)
        v.set("evaluation_started", isoTime(s.evaluationStarted));
        v.set("evaluation_days", s.evaluationDays);
    }
    if (s.serviceTime > 0) {
        // 1.35.0 : le compteur du bail, trois champs ignores par la 1.34
        v.set("service_time", isoTime(s.serviceTime));
        v.set("service_elapsed", static_cast<std::int64_t>(s.serviceElapsed));
        v.set("service_wall", isoTime(s.serviceWall));
    }
    const std::string text = core::json::write(v, true);
    crypto::Bytes plain(text.begin(), text.end()), sealed;
    if (!crypto::protect(plain, sealed, /*machine=*/true, why)) return false;
    return crypto::writePrivateFile(path, sealed, why);
}

// L'heure de modification d'un fichier, en secondes UTC (0 : illisible).
Seconds fileModified(const std::string& path) {
    std::error_code ec;
    const auto ft = std::filesystem::last_write_time(P(path), ec);
    if (ec) return 0;
    // file_clock -> system_clock par l'ecart des deux maintenant (portable, a la seconde pres)
    const auto sys = std::chrono::system_clock::now()
                     + std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - std::filesystem::file_time_type::clock::now());
    return static_cast<Seconds>(std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count());
}

// Ajouter une ligne a l'historique (les lignes illisibles ne sont pas recopiees ; 500 au plus).
bool appendHistoryLine(const std::string& path, const core::json::Value& v, std::string* why) {
    std::vector<std::string> raw;
    (void)readHistoryLines(path, &raw);
    raw.push_back(core::json::write(v, false));
    if (raw.size() > LicenseStore::kHistoryMaxLines)
        raw.erase(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(raw.size() - LicenseStore::kHistoryMaxLines));
    std::string text;
    for (const auto& l : raw) text += l + "\n";
    return writeAll(path, text, why);
}

// ---- 1.34.0 : l'evaluation de ce PC ----
constexpr int kEvaluationDefaultDays = 30;
constexpr std::string_view kEvaluationFormat = "xpg-evaluation";

// L'edition d'evaluation du registre : de type evaluation, pas retiree ; "EVALUATION" d'abord, sinon la premiere.
const EditionDef* evaluationEditionOf(const Registry& r) {
    const EditionDef* first = nullptr;
    for (const auto& e : r.editions) {
        if (e.type != LicenseType::Evaluation || e.retired) continue;
        if (e.id == "EVALUATION") return &e;
        if (!first) first = &e;
    }
    return first;
}

int evaluationDaysOf(const EditionDef& e) { return e.durationDays > 0 ? e.durationDays : kEvaluationDefaultDays; }

// "07/12/2026" : les phrases de l'evaluation (JJ/MM/AAAA, UTC).
std::string frenchDate(Seconds t) {
    const std::string d = isoDate(t);
    return d.size() == 10 ? d.substr(8, 2) + "/" + d.substr(5, 2) + "/" + d.substr(0, 4) : d;
}

// Une copie du debut de l'evaluation (evaluation.bin ; sous Windows, aussi la valeur du registre de ce compte),
// protegee comme etat.bin (DPAPI machine) ; elle garde aussi l'heure la plus tardive vue.
struct EvaluationCopy {
    Seconds started{0};
    int     days{0};
    Seconds latestSeen{0};
    bool    found{false};
};

bool sealEvaluation(const EvaluationCopy& c, crypto::Bytes& out, std::string* why) {
    core::json::Value v = core::json::Value::object();
    v.set("format", std::string(kEvaluationFormat));
    v.set("version", 1);
    v.set("started", isoTime(c.started));
    v.set("days", c.days);
    if (c.latestSeen > 0) v.set("latest_seen", isoTime(c.latestSeen));
    const std::string text = core::json::write(v, true);
    const crypto::Bytes plain(text.begin(), text.end());
    return crypto::protect(plain, out, /*machine=*/true, why);
}

EvaluationCopy openEvaluation(const crypto::Bytes& sealed) {
    EvaluationCopy c;
    crypto::Bytes plain;
    std::string why;
    if (sealed.empty() || !crypto::unprotect(sealed, plain, &why)) return c;
    core::json::Value v;
    if (!core::json::parse(std::string(plain.begin(), plain.end()), v) || !v.isObject() || v.str("format") != kEvaluationFormat) return c;
    Seconds t = 0;
    if (!parseIsoTime(v.str("started"), t) || t <= 0) return c;
    c.started = t;
    c.days = static_cast<int>(std::clamp<std::int64_t>(v.integer("days", 0), 0, 3650));
    if (parseIsoTime(v.str("latest_seen"), t)) c.latestSeen = t;
    c.found = true;
    return c;
}

EvaluationCopy readEvaluationFile(const std::string& path) {
    if (!fileExists(path)) return {};
    crypto::Bytes sealed;
    std::string why;
    if (!crypto::readFile(path, sealed, &why, 64u * 1024u)) return {};
    return openEvaluation(sealed);
}

bool writeEvaluationFile(const std::string& path, const EvaluationCopy& c, std::string* why) {
    crypto::Bytes sealed;
    return sealEvaluation(c, sealed, why) && crypto::writePrivateFile(path, sealed, why);
}

#ifdef _WIN32
// La troisieme copie : HKCU\Software\XPG\Licences, valeur "evaluation" (REG_BINARY, le meme contenu protege).
// Elle survit a une desinstallation et au dossier efface ; ce compte peut toujours l'ecrire. 1.35.0 : la valeur
// "horloge" y garde de meme une copie du compteur du bail.
constexpr const wchar_t* kEvaluationRegKey = L"Software\\XPG\\Licences";
constexpr const wchar_t* kEvaluationRegValue = L"evaluation";
constexpr const wchar_t* kClockRegValue = L"horloge";

crypto::Bytes readSystemValue(const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kEvaluationRegKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return {};
    DWORD type = 0, size = 0;
    crypto::Bytes data;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && type == REG_BINARY && size > 0 && size <= 64u * 1024u) {
        data.resize(size);
        if (RegQueryValueExW(key, name, nullptr, &type, data.data(), &size) != ERROR_SUCCESS || type != REG_BINARY) data.clear();
        else data.resize(size);
    }
    RegCloseKey(key);
    return data;
}

bool writeSystemValue(const wchar_t* name, const crypto::Bytes& sealed, std::string* why) {
    HKEY key = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kEvaluationRegKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (rc == ERROR_SUCCESS) {
        rc = RegSetValueExW(key, name, 0, REG_BINARY, sealed.data(), static_cast<DWORD>(sealed.size()));
        RegCloseKey(key);
    }
    if (rc != ERROR_SUCCESS) {
        if (why) *why = "le registre de Windows refuse la valeur (erreur " + std::to_string(rc) + ")";
        return false;
    }
    return true;
}

EvaluationCopy readEvaluationSystem() { return openEvaluation(readSystemValue(kEvaluationRegValue)); }

bool writeEvaluationSystem(const EvaluationCopy& c, std::string* why) {
    crypto::Bytes sealed;
    return sealEvaluation(c, sealed, why) && writeSystemValue(kEvaluationRegValue, sealed, why);
}
#endif

// ---- 1.35.0 : une copie du compteur du bail (horloge.bin ; sous Windows, la valeur "horloge" de HKCU) ----
constexpr std::string_view kClockFormat = "xpg-horloge";

struct ClockCopy {
    Seconds serviceTime{0};
    Seconds elapsed{0};
    Seconds wall{0};
};

bool sealClock(const ClockCopy& c, crypto::Bytes& out, std::string* why) {
    core::json::Value v = core::json::Value::object();
    v.set("format", std::string(kClockFormat));
    v.set("version", 1);
    v.set("service_time", isoTime(c.serviceTime));
    v.set("elapsed", static_cast<std::int64_t>(c.elapsed));
    v.set("wall", isoTime(c.wall));
    const std::string text = core::json::write(v, true);
    const crypto::Bytes plain(text.begin(), text.end());
    return crypto::protect(plain, out, /*machine=*/true, why);
}

ClockCopy openClock(const crypto::Bytes& sealed) {
    ClockCopy c;
    crypto::Bytes plain;
    std::string why;
    if (sealed.empty() || !crypto::unprotect(sealed, plain, &why)) return c;
    core::json::Value v;
    if (!core::json::parse(std::string(plain.begin(), plain.end()), v) || !v.isObject() || v.str("format") != kClockFormat) return c;
    Seconds t = 0;
    if (!parseIsoTime(v.str("service_time"), t) || t <= 0) return c;
    c.serviceTime = t;
    c.elapsed = std::max<std::int64_t>(0, v.integer("elapsed", 0));
    if (parseIsoTime(v.str("wall"), t)) c.wall = t;
    return c;
}

ClockCopy readClockFile(const std::string& path) {
    if (!fileExists(path)) return {};
    crypto::Bytes sealed;
    std::string why;
    if (!crypto::readFile(path, sealed, &why, 64u * 1024u)) return {};
    return openClock(sealed);
}

bool writeClockFile(const std::string& path, const ClockCopy& c, std::string* why) {
    crypto::Bytes sealed;
    return sealClock(c, sealed, why) && crypto::writePrivateFile(path, sealed, why);
}

// ---- 1.35.0 : verifier ce que le service a signe ----
// Ce qui manque a une activation pour valoir sur ce PC.
enum class ActivationFault : std::uint8_t { None, Missing, OtherLicense, Invalid, OtherDevice, CertExpired };

struct ActivationCheck {
    ActivationFault      fault{ActivationFault::Invalid};
    std::string          problem;        // une phrase (sans majuscule ni point final)
    service::Activation  activation;
    service::ServiceCert cert;
    SignedBlock          activationBlock, certBlock;
};

// 1.35.0 (revue 6) : le certificat nomme-t-il le service de CE programme ? `expected` vide : ce programme n'a pas de
// service, il ne croit aucun document du service.
bool certForService(const service::ServiceCert& c, const std::string& expected, std::string* why) {
    if (expected.empty()) {
        if (why) *why = "ce programme n'a pas de service des licences : il ne croit aucun document du service";
        return false;
    }
    if (!service::sameUrl(c.url, expected)) {
        if (why) *why = "le certificat du service est pour une autre adresse (" + c.url + ") que celle du service de ce programme (" + expected + ")";
        return false;
    }
    return true;
}

std::string certExpiredText(Seconds expiresAt) { return "le certificat du service a expir\xC3\xA9 le " + frenchDate(expiresAt); }

// L'activation (et son certificat) pour cette licence et ce PC. Ni le bail (jusqu_a) ni la fin du certificat ne sont
// compares ici : l'appelant les compare a l'heure retenue (au moins l'emission de cette activation, revue 1).
ActivationCheck checkActivation(std::string_view activationText, std::string_view certText, const std::vector<crypto::PublicKey>& keys,
                                const std::string& expectedUrl, const License& lic, const std::string& deviceKeyHex, const std::string& station) {
    ActivationCheck r;
    std::string why;
    const auto fail = [&](ActivationFault f, std::string text) {
        r.fault = f;
        r.problem = std::move(text);
        return r;
    };
    if (!service::findBlock(certText, DocKind::ServiceCert, r.certBlock, &why)) return fail(ActivationFault::Invalid, "le certificat du service manque (" + why + ")");
    if (!service::verifyCert(certText, keys, 0, r.cert, &why)) return fail(ActivationFault::Invalid, why);
    if (!certForService(r.cert, expectedUrl, &why)) return fail(ActivationFault::Invalid, why);
    if (!service::findBlock(activationText, DocKind::Activation, r.activationBlock, &why)) return fail(ActivationFault::Invalid, "l'activation est illisible (" + why + ")");
    if (!service::verifyServiceBlock(r.activationBlock, DocKind::Activation, r.cert, service::kRightActivation, &why)) return fail(ActivationFault::Invalid, why);
    service::Activation& a = r.activation;
    if (!service::parseActivation(r.activationBlock.document, a, &why)) return fail(ActivationFault::Invalid, why);
    if (a.licenseId != lic.licenseId) return fail(ActivationFault::OtherLicense, "cette activation est pour une autre licence (" + a.licenseId + ")");
    if (a.licenseIssuedAt != lic.issuedAt)
        return fail(ActivationFault::Invalid, "cette activation est pour une autre \xC3\xA9mission de la licence " + lic.licenseId + " (\xC3\xA9mise le "
                                                  + isoDate(a.licenseIssuedAt) + ", celle de ce PC le " + isoDate(lic.issuedAt) + ")");
    if (deviceKeyHex.empty()) return fail(ActivationFault::OtherDevice, "ce PC n'a pas encore de cl\xC3\xA9 d'identit\xC3\xA9");
    if (a.deviceKey != deviceKeyHex) return fail(ActivationFault::OtherDevice, "cette activation est pour un autre PC");
    // revue 3 : cette installation a une empreinte du poste (le cas normal) -> l'activation doit porter la meme ; une
    // activation sans empreinte vaudrait sur toute installation ou la cle de ce PC serait recopiee. Une installation
    // sans identifiant lisible (empreinte vide) : rien a comparer, comme avant.
    if (!station.empty() && a.station.empty())
        return fail(ActivationFault::OtherDevice, "cette activation ne porte pas l'empreinte du poste : elle ne peut pas \xC3\xAAtre v\xC3\xA9rifi\xC3\xA9" "e pour cette installation de Windows");
    if (!station.empty() && a.station != station)
        return fail(ActivationFault::OtherDevice, "cette activation est pour une autre installation de Windows (la cl\xC3\xA9 de ce PC a-t-elle \xC3\xA9t\xC3\xA9 recopi\xC3\xA9" "e ?)");
    if (a.floating != (lic.type == LicenseType::Floating))
        return fail(ActivationFault::Invalid, std::string("cette activation est celle d'une licence ") + (a.floating ? "flottante" : "fixe") + ", pas la licence de ce PC");
    r.fault = ActivationFault::None;
    return r;
}

// Le jeton d'une licence flottante pour cette licence et ce PC (sa fin n'est pas comparee ici ; la fin du certificat
// l'est si `trustedNow` > 0, et rendue dans `certExpiresAt`). Faux : `why`.
bool checkLease(std::string_view leaseText, std::string_view certText, const std::vector<crypto::PublicKey>& keys, const std::string& expectedUrl,
                Seconds trustedNow, const std::string& licenseId, const std::string& deviceKeyHex, service::Lease& out, SignedBlock* leaseBlock,
                SignedBlock* certBlock, Seconds* certExpiresAt, std::string* why) {
    service::ServiceCert cert;
    SignedBlock cb, lb;
    std::string w;
    const auto fail = [&](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    if (!service::findBlock(certText, DocKind::ServiceCert, cb, &w)) return fail("le certificat du service manque (" + w + ")");
    if (!service::verifyCert(certText, keys, 0, cert, &w)) return fail(w);
    if (!certForService(cert, expectedUrl, &w)) return fail(w);
    if (trustedNow > 0 && trustedNow > cert.expiresAt) return fail(certExpiredText(cert.expiresAt));
    if (!service::findBlock(leaseText, DocKind::Lease, lb, &w)) return fail("le jeton est illisible (" + w + ")");
    if (!service::verifyServiceBlock(lb, DocKind::Lease, cert, service::kRightFloating, &w)) return fail(w);
    service::Lease l;
    if (!service::parseLease(lb.document, l, &w)) return fail(w);
    if (l.licenseId != licenseId) return fail("ce jeton est pour une autre licence (" + l.licenseId + ")");
    if (deviceKeyHex.empty() || l.deviceKey != deviceKeyHex) return fail("ce jeton est pour un autre PC");
    out = std::move(l);
    if (certExpiresAt) *certExpiresAt = cert.expiresAt;
    if (leaseBlock) *leaseBlock = std::move(lb);
    if (certBlock) *certBlock = std::move(cb);
    return true;
}

// La Decouverte de cette machine selon le service, pour cette machine et ce PC. Faux : `why`.
bool checkTrial(std::string_view trialText, std::string_view certText, const std::vector<crypto::PublicKey>& keys, const std::string& expectedUrl,
                Seconds trustedNow, const std::string& machine, const std::string& deviceKeyHex, service::Trial& out, SignedBlock* trialBlock, SignedBlock* certBlock,
                std::string* why) {
    service::ServiceCert cert;
    SignedBlock cb, tb;
    std::string w;
    const auto fail = [&](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    if (!service::findBlock(certText, DocKind::ServiceCert, cb, &w)) return fail("le certificat du service manque (" + w + ")");
    if (!service::verifyCert(certText, keys, 0, cert, &w)) return fail(w);
    if (!certForService(cert, expectedUrl, &w)) return fail(w);
    if (trustedNow > 0 && trustedNow > cert.expiresAt) return fail(certExpiredText(cert.expiresAt));
    if (!service::findBlock(trialText, DocKind::Trial, tb, &w)) return fail("la D\xC3\xA9" "couverte est illisible (" + w + ")");
    if (!service::verifyServiceBlock(tb, DocKind::Trial, cert, service::kRightTrial, &w)) return fail(w);
    service::Trial t;
    if (!service::parseTrial(tb.document, t, &w)) return fail(w);
    if (machine.empty() || t.machine != machine) return fail("cette D\xC3\xA9" "couverte est pour une autre machine");
    if (deviceKeyHex.empty() || t.deviceKey != deviceKeyHex) return fail("cette D\xC3\xA9" "couverte est pour un autre PC");
    out = std::move(t);
    if (trialBlock) *trialBlock = std::move(tb);
    if (certBlock) *certBlock = std::move(cb);
    return true;
}

// Les lignes "evaluation" de l'historique : le debut le plus ancien (0 : aucun) et le moins de jours notes (0 : pas dits).
void evaluationFromHistory(const std::vector<core::json::Value>& lines, Seconds& started, int& days) {
    for (const auto& h : lines) {
        if (h.str("kind") != "evaluation") continue;
        Seconds t = 0;
        if (!parseIsoTime(h.str("started"), t) || t <= 0) continue;
        if (started == 0 || t < started) started = t;
        const auto d = std::clamp<std::int64_t>(h.integer("days", 0), 0, 3650);
        if (d > 0 && (days == 0 || d < days)) days = static_cast<int>(d);
    }
}

} // namespace

// ==================================================================== les etats ==
const char* statusKey(LicenseStatus s) noexcept {
    switch (s) {
        case LicenseStatus::Valid: return "valid";
        case LicenseStatus::Grace: return "grace";
        case LicenseStatus::Expired: return "expired";
        case LicenseStatus::Revoked: return "revoked";
        case LicenseStatus::Invalid: return "invalid";
        case LicenseStatus::OtherDevice: return "other-device";
        case LicenseStatus::Absent: return "absent";
        case LicenseStatus::ClockRolledBack: return "clock-rolled-back";
        case LicenseStatus::NotConfigured: return "non-configuree";
        case LicenseStatus::Evaluation: return "evaluation";
        case LicenseStatus::LeaseExpired: return "lease-expired";
        case LicenseStatus::NoFloatingSeat: return "no-floating-seat";
    }
    return "?";
}

std::string statusLabel(LicenseStatus s) {
    switch (s) {
        case LicenseStatus::Valid: return "Valide";
        case LicenseStatus::Grace: return "Gr\xC3\xA2" "ce";
        case LicenseStatus::Expired: return "Expir\xC3\xA9" "e";
        case LicenseStatus::Revoked: return "R\xC3\xA9voqu\xC3\xA9" "e";
        case LicenseStatus::Invalid: return "Invalide";
        case LicenseStatus::OtherDevice: return "Autre PC";
        case LicenseStatus::Absent: return "Absente";
        case LicenseStatus::ClockRolledBack: return "Horloge recul\xC3\xA9" "e";
        case LicenseStatus::NotConfigured: return "Non activ\xC3\xA9" "es";
        case LicenseStatus::Evaluation: return "D\xC3\xA9" "couverte";   // 1.35.0 : l'evaluation de ce PC s'appelle la Decouverte
        case LicenseStatus::LeaseExpired: return "Bail expir\xC3\xA9";
        case LicenseStatus::NoFloatingSeat: return "Flottante : aucun poste";
    }
    return "?";
}

bool LicenseState::hasFeature(std::string_view id) const noexcept {
    if (allFeaturesOpen(status)) return true;   // les licences ne sont pas actives : rien n'est verrouille
    return std::any_of(features.begin(), features.end(), [&](const FeatureGrant& g) { return g.id == id; });
}

std::optional<std::int64_t> LicenseState::limit(std::string_view feature, std::string_view name) const noexcept {
    for (const auto& g : features)
        if (g.id == feature)
            for (const auto& [k, n] : g.limits)
                if (k == name) return n;
    return std::nullopt;
}

std::vector<TrustedKey> compiledPublisherKeys() {
    std::vector<TrustedKey> out;
    const auto add = [&](const publisher::KeyEntry& k) {
        TrustedKey t;
        std::copy(std::begin(k.key), std::end(k.key), t.key.begin());
        t.id = keyIdOf(t.key);
        t.name = k.name;
        t.test = k.test;
        out.push_back(std::move(t));
    };
    for (const auto& k : publisher::kKeys) add(k);
#ifdef XPG_LICENCES_CLE_ESSAI
    for (const auto& k : publisher::kTestKeys) add(k);   // la cle d'ESSAI : une construction de captures, jamais livree
#endif
    return out;
}

bool testKeyCompiled() noexcept {
#ifdef XPG_LICENCES_CLE_ESSAI
    return true;
#else
    return false;
#endif
}

std::string testFolderOverride() {
#ifdef XPG_LICENCES_CLE_ESSAI
    // seulement une construction d'essai : un programme livre ne se laisse pas deplacer son dossier (et son etat)
    if (const char* f = std::getenv("XPG_LICENCES_DOSSIER"); f && *f) return f;
#endif
    return {};
}

// ======================================================================= le magasin ==
struct LicenseStore::Loaded {
    State          state;
    std::string    stateNote;
    bool           hasRegistry{false};
    Registry       registry;
    std::string    registryProblem;
    bool           hasRevocations{false};
    RevocationList revocations;
    std::string    revocationsProblem;
    bool           hasLicenseFile{false};
    bool           licenseRead{false};      // signee et lisible
    License        license;
    std::string    licenseProblem;          // pourquoi elle est invalide
    std::string    licenseBytes;
    bool           hasDeviceKey{false};
    std::string    deviceKeyHex;
    std::string    deviceFingerprint;
    std::string    deviceProblem;
    std::string    station;                 // 1.32.0 : l'empreinte de ce poste (vide : pas d'identifiant d'installation)
    Seconds        bundledIssuedAt{0};      // 1.32.0 : les documents livres avec le programme (leur heure d'emission)
    Seconds        anchor{0};               // l'heure la plus tardive vue, et les emissions des documents (l'horloge)
    // 1.34.0 : l'evaluation (sans licence, une edition d'evaluation au registre ; vide : elle ne s'applique pas)
    std::string    evaluationEdition;
    Seconds        evaluationStarted{0};    // le debut le plus ancien de toutes les copies (ou maintenant : elle commence)
    int            evaluationDays{0};
    bool           evaluationRefused{false};   // pas de nouvelle evaluation : une licence a deja ete installee sur ce PC
    bool           evaluationFromService{false};   // 1.35.0 : le debut retenu est celui du service (decouverte.xpgdec)
    // 1.35.0 : le service des licences
    Seconds        serviceTime{0};          // le compteur du bail (apres l'avance de cette evaluation)
    Seconds        serviceElapsed{0};
    Seconds        trustedNow{0};           // l'heure retenue (revue 1 : au moins l'emission de l'activation et du jeton)
    Seconds        serviceNow{0};           // l'heure du service (derniere heure signee + temps ecoule ; le jeton)
    bool           counterFound{false};     // revue 1 : une copie lisible du compteur (une heure signee gardee)
    Seconds        wallSeen{0};             // revue 1 : l'horloge du PC la plus avancee deja comptee (avant celle de maintenant)
    ActivationFault activationFault{ActivationFault::Missing};
    std::string    activationProblem;
    service::Activation activation;
    Seconds        certExpiresAt{0};
    bool           leaseOk{false};          // un jeton verifie pour cette licence et ce PC (sa fin pas regardee)
    service::Lease lease;
    std::string    leaseProblem;
};

// 1.35.0 : le compteur du bail (voir Store.hpp).
struct LicenseStore::Counter {
    Seconds serviceTime{0};   // la derniere heure signee (0 : jamais)
    Seconds elapsed{0};       // le temps ecoule depuis
    Seconds wall{0};          // l'horloge du PC la plus avancee deja comptee
    bool operator==(const Counter&) const = default;
    // La meilleure de deux copies : l'heure signee la plus recente, puis le plus de temps ecoule.
    [[nodiscard]] bool better(const Counter& o) const { return serviceTime != o.serviceTime ? serviceTime > o.serviceTime : elapsed > o.elapsed; }
};

struct LicenseStore::CounterRead {
    Counter counter;          // avance jusqu'a maintenant
    Counter file, system;     // les copies lues (horloge.bin, HKCU)
    Seconds steady{0};        // l'horloge monotone a cette lecture
    Seconds trustedNow{0};
    Seconds serviceNow{0};
    Seconds wallSeen{0};      // revue 1 : l'horloge du PC la plus avancee deja comptee, avant l'heure du PC de maintenant
};

LicenseStore::LicenseStore(StoreOptions options) : opt_(std::move(options)) {
    const bool compiledTrust = opt_.trustedKeys.empty() && opt_.compiledKeys;   // le programme livre : ses cles compilees
    if (compiledTrust) opt_.trustedKeys = compiledPublisherKeys();
    if (opt_.clockTolerance < 0) opt_.clockTolerance = 0;
    if (opt_.machineId.empty()) opt_.machineId = systemMachineId();
    // 1.34.0 : la periode d'evaluation - par defaut celle du programme livre (ses cles compilees), pas d'un essai
    evaluation_ = opt_.evaluationPeriod.value_or(compiledTrust);
    // 1.35.0 (revue 6) : l'adresse du service que ce magasin croit (celle du programme, sauf un essai)
    serviceUrl_ = opt_.serviceUrl ? service::normalizeUrl(*opt_.serviceUrl) : service::compiledAddress().url;
}

// 1.34.0 : la copie de l'evaluation dans le registre de Windows (HKCU) - seulement pour le dossier par defaut (le
// programme livre) : un essai, un dossier remplace, ne touche jamais celle de l'installation de ce PC.
bool LicenseStore::systemEvaluationCopy() const {
#ifdef _WIN32
    return !opt_.folder.empty() && opt_.folder == defaultFolder();
#else
    return false;
#endif
}

Seconds LicenseStore::fileTime(const std::string& path) const {
    if (opt_.fileTime) return opt_.fileTime(path);
    if (opt_.clock) return 0;   // une horloge d'essai : les dates des fichiers ne s'y comparent pas
    return fileModified(path);
}

std::string LicenseStore::defaultFolder() {
#ifdef _WIN32
    const char* pd = std::getenv("ProgramData");
    return deploy::joinPath(deploy::joinPath(pd && *pd ? pd : "C:\\ProgramData", "XPG"), "Licences");
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) return deploy::joinPath(deploy::joinPath(xdg, "xpg"), "licences");
    const char* home = std::getenv("HOME");
    return deploy::joinPath(deploy::joinPath(deploy::joinPath(home && *home ? home : "/tmp", ".local/share"), "xpg"), "licences");
#endif
}

Seconds LicenseStore::now() const { return opt_.clock ? opt_.clock() : systemNow(); }

// 1.35.0 : l'horloge monotone, en secondes (arrondie en dessous : les ecarts se compensent d'une lecture a l'autre).
Seconds LicenseStore::monotonicNow() const {
    if (opt_.monotonicClock) return opt_.monotonicClock();
    return static_cast<Seconds>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::vector<crypto::PublicKey> LicenseStore::publisherKeys() const {
    std::vector<crypto::PublicKey> out;
    for (const auto& k : opt_.trustedKeys) out.push_back(k.key);
    return out;
}

// 1.35.0 : le compteur du bail - la meilleure de ses copies (etat.bin, horloge.bin, HKCU, et celle de ce lancement),
// avancee jusqu'a maintenant : l'horloge du PC la plus avancee deja comptee avance jusqu'a l'heure du PC, ou (dans ce
// lancement, si la meilleure copie est celle qu'il a laissee) du temps mesure par l'horloge monotone ; le temps ecoule
// avance d'autant. Rien n'est ecrit ici.
LicenseStore::CounterRead LicenseStore::readCounter(Seconds t, const Counter& fromState) const {
    CounterRead r;
    r.steady = monotonicNow();
    const ClockCopy f = readClockFile(deploy::joinPath(opt_.folder, kClockFile));
    r.file = Counter{f.serviceTime, f.elapsed, f.wall};
#ifdef _WIN32
    if (systemEvaluationCopy()) {
        const ClockCopy c = openClock(readSystemValue(kClockRegValue));
        r.system = Counter{c.serviceTime, c.elapsed, c.wall};
    }
#endif
    const Counter memo{memoServiceTime_, memoElapsed_, memoWall_};
    Counter best = fromState;
    for (const Counter* c : {static_cast<const Counter*>(&r.file), static_cast<const Counter*>(&r.system), &memo})
        if (c->better(best)) best = *c;
    const bool fromMemo = memoSet_ && best == memo;
    Counter c = best;
    if (c.serviceTime > 0) {
        Seconds w = c.wall;
        if (fromMemo && r.steady > memoSteady_) w = std::max(w, c.wall + (r.steady - memoSteady_));
        r.wallSeen = w;   // revue 1 : l'horloge du PC en arriere de plus que la tolerance -> Horloge reculee (judge)
        w = std::max(w, t);
        c.elapsed += w - c.wall;
        c.wall = w;
    }
    r.counter = c;
    r.serviceNow = c.serviceTime > 0 ? c.serviceTime + c.elapsed : t;
    r.trustedNow = std::max(t, r.serviceNow);
    return r;
}

const TrustedKey* LicenseStore::trusted(std::string_view keyId) const noexcept {
    for (const auto& k : opt_.trustedKeys)
        if (k.id == keyId) return &k;
    return nullptr;
}

bool LicenseStore::verifyTrusted(const SignedBlock& b, std::string* why) const {
    const TrustedKey* k = trusted(b.keyId);
    if (!k) {
        if (why) *why = "la signature de " + docKindLabel(b.kind) + " vient d'une cl\xC3\xA9 inconnue de ce programme (" + b.keyId + ")";
        return false;
    }
    if (!verifyBlock(b, k->key)) {
        if (why) *why = "la signature de " + docKindLabel(b.kind) + " est fausse : le document a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9, ou n'a pas \xC3\xA9t\xC3\xA9 sign\xC3\xA9 par l'\xC3\xA9" "diteur";
        return false;
    }
    return true;
}

namespace {

// Le seul bloc, d'un genre donne, d'un fichier du magasin.
bool readStoredBlock(const std::string& path, DocKind kind, SignedBlock& out, std::string* why) {
    std::string bytes;
    if (!readAll(path, bytes, why)) return false;
    std::vector<SignedBlock> blocks;
    if (!readBlocks(bytes, blocks, why)) return false;
    if (blocks.size() != 1 || blocks[0].kind != kind) {
        if (why) *why = "le fichier ne contient pas seulement " + docKindLabel(kind);
        return false;
    }
    out = std::move(blocks[0]);
    return true;
}

} // namespace

std::string LicenseStore::notWritable(const std::string& why) const {
    return "ce compte ne peut pas \xC3\xA9" "crire dans le dossier des licences de ce PC (" + opt_.folder + ") : " + why
           + " ; une installation pour tous les comptes donne ce droit \xC3\xA0 chaque compte";
}

LicenseState LicenseStore::evaluate() {
    const Seconds t = now();
    const std::string dir = opt_.folder;
    if (!configured()) {
        // Aucune cle de l'editeur : rien ne peut etre verifie, tout est ouvert ; rien n'est ecrit.
        LicenseState s;
        s.status = LicenseStatus::NotConfigured;
        s.evaluatedAt = t;
        s.trustedNow = t;
        s.message = kNotConfiguredMessage;
        s.detail = "aucune cl\xC3\xA9 de l'\xC3\xA9" "diteur n'est compil\xC3\xA9" "e dans ce programme (src/licensing/PublisherKey.hpp est vide)";
        registry_.reset();
        license_.reset();
        deviceKeyHex_.clear();
        lastEvaluated_ = s.status;
        const std::string kp = deploy::joinPath(dir, kDeviceKeyFile);
        if (fileExists(kp)) {
            std::string why;
            if (auto key = SigningKey::load(kp, deviceKeySpec(), &why)) {
                deviceKeyHex_ = key->publicKeyHex();
                s.deviceFingerprint = key->fingerprint();
            }
        }
        return s;
    }
    Loaded in;
    const std::string statePath = deploy::joinPath(dir, kStateFile);
    in.state = readState(statePath, &in.stateNote);
    const State before = in.state;

    // ---- le registre : celui du magasin, ou celui livre avec le programme s'il est plus recent.
    {
        Registry stored, bundled;
        std::string storedWhy, bundledWhy, bundledBytes;
        bool hasStored = false, hasBundled = false;
        const std::string path = deploy::joinPath(dir, kRegistryFile);
        SignedBlock b;
        if (fileExists(path)) {
            hasStored = readStoredBlock(path, DocKind::Registry, b, &storedWhy) && verifyTrusted(b, &storedWhy)
                        && parseRegistry(b.document, stored, &storedWhy);
        }
        if (!opt_.bundledRegistry.empty() && fileExists(opt_.bundledRegistry)) {
            SignedBlock bb;
            hasBundled = readStoredBlock(opt_.bundledRegistry, DocKind::Registry, bb, &bundledWhy) && verifyTrusted(bb, &bundledWhy)
                         && parseRegistry(bb.document, bundled, &bundledWhy);
            if (hasBundled) {
                bundledBytes = writeBlock(bb);
                in.bundledIssuedAt = std::max(in.bundledIssuedAt, bundled.issuedAt);
            }
        }
        if (hasBundled && (!hasStored || bundled.number > stored.number) && bundled.number >= in.state.registryMax) {
            // Le programme apporte un registre plus recent : il entre dans le magasin.
            std::string w;
            (void)writeAll(path, bundledBytes, &w);
            stored = bundled;
            hasStored = true;
            storedWhy.clear();
        }
        if (hasStored && stored.number < in.state.registryMax) {
            in.registryProblem = "le registre de ce PC (n\xC2\xB0 " + std::to_string(stored.number) + ") est plus ancien que le n\xC2\xB0 "
                                 + std::to_string(in.state.registryMax) + " d\xC3\xA9j\xC3\xA0 vu : il a \xC3\xA9t\xC3\xA9 remplac\xC3\xA9, il est refus\xC3\xA9";
        } else if (hasStored) {
            in.hasRegistry = true;
            in.registry = std::move(stored);
            in.state.registryMax = std::max(in.state.registryMax, in.registry.number);
        } else if (!storedWhy.empty()) {
            in.registryProblem = storedWhy;
        } else {
            in.registryProblem = "le registre des \xC3\xA9" "ditions manque sur ce PC";
        }
    }
    // ---- les revocations : celles du magasin, ou (1.32.0) celles livrees avec le programme si elles sont plus recentes.
    {
        const std::string path = deploy::joinPath(dir, kRevocationsFile);
        const bool storedExists = fileExists(path);
        RevocationList stored, bundled;
        std::string storedWhy, bundledWhy, bundledBytes;
        bool hasStored = false, hasBundled = false;
        if (storedExists) {
            SignedBlock b;
            hasStored = readStoredBlock(path, DocKind::Revocations, b, &storedWhy) && verifyTrusted(b, &storedWhy)
                        && parseRevocations(b.document, stored, &storedWhy);
        }
        if (!opt_.bundledRevocations.empty() && fileExists(opt_.bundledRevocations)) {
            SignedBlock bb;
            hasBundled = readStoredBlock(opt_.bundledRevocations, DocKind::Revocations, bb, &bundledWhy) && verifyTrusted(bb, &bundledWhy)
                         && parseRevocations(bb.document, bundled, &bundledWhy);
            if (hasBundled) {
                bundledBytes = writeBlock(bb);
                in.bundledIssuedAt = std::max(in.bundledIssuedAt, bundled.issuedAt);   // elle releve l'heure la plus tardive
            }
        }
        if (hasBundled && (!hasStored || bundled.number > stored.number) && bundled.number >= in.state.revocationsMax) {
            // Le programme apporte une liste plus recente : elle entre dans le magasin (comme le registre).
            std::string w;
            (void)writeAll(path, bundledBytes, &w);
            stored = std::move(bundled);
            hasStored = true;
            storedWhy.clear();
        }
        if (hasStored) {
            if (stored.number < in.state.revocationsMax) {
                in.revocationsProblem = "la liste de r\xC3\xA9vocation de ce PC (n\xC2\xB0 " + std::to_string(stored.number) + ") est plus ancienne que le n\xC2\xB0 "
                                        + std::to_string(in.state.revocationsMax) + " d\xC3\xA9j\xC3\xA0 vu : elle a \xC3\xA9t\xC3\xA9 remplac\xC3\xA9" "e";
            } else {
                in.hasRevocations = true;
                in.revocations = std::move(stored);
                in.state.revocationsMax = std::max(in.state.revocationsMax, in.revocations.number);
            }
        } else if (storedExists) {
            in.revocationsProblem = storedWhy;
        } else if (in.state.revocationsMax > 0) {
            in.revocationsProblem = "la liste de r\xC3\xA9vocation n\xC2\xB0 " + std::to_string(in.state.revocationsMax) + " a disparu de ce PC";
        }
    }
    // ---- la licence.
    {
        const std::string path = deploy::joinPath(dir, kLicenseFile);
        if (fileExists(path)) {
            in.hasLicenseFile = true;
            SignedBlock b;
            std::string why;
            if (readStoredBlock(path, DocKind::License, b, &why) && verifyTrusted(b, &why) && parseLicense(b.document, in.license, &why)) {
                in.licenseRead = true;
                in.licenseBytes = writeBlock(b);
            } else {
                in.licenseProblem = why;
            }
        }
    }
    // ---- la cle de ce PC (relue, jamais creee ici).
    {
        const std::string path = deploy::joinPath(dir, kDeviceKeyFile);
        if (fileExists(path)) {
            std::string why;
            auto key = SigningKey::load(path, deviceKeySpec(), &why);
            if (key) {
                in.hasDeviceKey = true;
                in.deviceKeyHex = key->publicKeyHex();
                in.deviceFingerprint = key->fingerprint();
                in.station = stationHash(opt_.machineId, key->publicKey());
            } else {
                in.deviceProblem = why;
            }
        } else {
            in.deviceProblem = "ce PC n'a pas encore de cl\xC3\xA9 d'identit\xC3\xA9 (elle est cr\xC3\xA9\xC3\xA9" "e avec la demande d'activation)";
        }
    }

    // ---- 1.35.0 : le compteur du bail (l'heure retenue), puis ce que le service a signe pour ce PC.
    const CounterRead clock = readCounter(t, Counter{in.state.serviceTime, in.state.serviceElapsed, in.state.serviceWall});
    in.serviceTime = clock.counter.serviceTime;
    in.serviceElapsed = clock.counter.elapsed;
    in.trustedNow = clock.trustedNow;
    in.serviceNow = clock.serviceNow;
    in.counterFound = clock.counter.serviceTime > 0;
    in.wallSeen = clock.wallSeen;
    Counter counterNow = clock.counter;   // le compteur garde (revue 1 : recale en avant par une emission signee plus recente)
    const std::vector<crypto::PublicKey> pubKeys = publisherKeys();
    if (in.licenseRead && !in.license.service.empty()) {
        // une licence en ligne : l'activation de ce PC (activation.xpgact : l'activation et le certificat)
        const std::string ap = deploy::joinPath(dir, kActivationFile);
        std::string text, why;
        if (!fileExists(ap)) {
            in.activationFault = ActivationFault::Missing;
            in.activationProblem = "ce PC n'a pas d'activation pour cette licence en ligne";
        } else if (!readAll(ap, text, &why)) {
            in.activationFault = ActivationFault::Invalid;
            in.activationProblem = "l'activation de ce PC ne se lit pas (" + why + ")";
        } else {
            ActivationCheck c = checkActivation(text, text, pubKeys, serviceUrl_, in.license, in.deviceKeyHex, in.station);
            in.activationFault = c.fault;
            in.activationProblem = c.problem;
            in.activation = std::move(c.activation);
            in.certExpiresAt = c.cert.expiresAt;
        }
        // revue 1 : l'heure retenue (et celle du service, pour le jeton) est au moins la derniere heure signee gardee et
        // l'emission de l'activation verifiee : une date ecrite dans un document signe ne s'efface pas.
        Seconds signedFloor = in.serviceTime;
        if (in.activationFault == ActivationFault::None) signedFloor = std::max(signedFloor, in.activation.issuedAt);
        Seconds leaseCertExpiresAt = 0;
        if (in.license.type == LicenseType::Floating) {
            const std::string lp = deploy::joinPath(dir, kLeaseFile);
            if (!fileExists(lp)) {
                in.leaseProblem = "ce PC n'a pas de jeton";
            } else if (!readAll(lp, text, &why)) {
                in.leaseProblem = "le jeton ne se lit pas (" + why + ")";
            } else {
                in.leaseOk = checkLease(text, text, pubKeys, serviceUrl_, 0, in.license.licenseId, in.deviceKeyHex, in.lease, nullptr, nullptr,
                                        &leaseCertExpiresAt, &in.leaseProblem);
                // le jeton se compte a l'heure du service ; jamais avant son emission (une heure signee lui aussi)
                if (in.leaseOk) signedFloor = std::max(signedFloor, in.lease.issuedAt);
            }
        }
        // Une emission plus recente que l'heure du compteur est une heure signee de plus : le compteur repart d'elle (en
        // avant seulement ; l'horloge du PC deja comptee reste, l'horloge reculee se voit toujours). Sans compteur : rien
        // (une activation rejouee ne doit pas en recreer un a l'heure du PC).
        if (in.counterFound && signedFloor > clock.serviceNow) {
            counterNow.serviceTime = signedFloor;
            counterNow.elapsed = 0;
            in.serviceTime = signedFloor;
            in.serviceElapsed = 0;
        }
        in.trustedNow = std::max(in.trustedNow, signedFloor);
        in.serviceNow = std::max(in.serviceNow, signedFloor);
        // les certificats, a l'heure retenue
        if (in.activationFault == ActivationFault::None && in.trustedNow > in.certExpiresAt) {
            in.activationFault = ActivationFault::CertExpired;
            in.activationProblem = certExpiredText(in.certExpiresAt);
        }
        if (in.leaseOk && in.trustedNow > leaseCertExpiresAt) {
            in.leaseOk = false;
            in.leaseProblem = certExpiredText(leaseCertExpiresAt);
        }
    }

    // ---- 1.34.0 : l'evaluation de ce PC - son debut LE PLUS ANCIEN parmi toutes ses copies (etat.bin,
    // evaluation.bin, la ligne de l'historique, et sous Windows la valeur de HKCU), jamais avance.
    const std::string historyPath = deploy::joinPath(dir, kHistoryFile);
    std::vector<core::json::Value> historyLines;
    bool historyRead = false;
    const auto historyOnce = [&]() -> const std::vector<core::json::Value>& {
        if (!historyRead) {
            historyLines = readHistoryLines(historyPath, nullptr);
            historyRead = true;
        }
        return historyLines;
    };
    const std::string evaluationPath = deploy::joinPath(dir, kEvaluationFile);
    EvaluationCopy evalFile, evalSystem;
    Seconds evalHistory = 0;
    if (evaluation_) {
        evalFile = readEvaluationFile(evaluationPath);
#ifdef _WIN32
        if (systemEvaluationCopy()) evalSystem = readEvaluationSystem();
#endif
        int historyDays = 0;
        evaluationFromHistory(historyOnce(), evalHistory, historyDays);
        const auto take = [&](Seconds started, int days) {
            if (started > 0 && (in.evaluationStarted == 0 || started < in.evaluationStarted)) in.evaluationStarted = started;
            if (days > 0 && (in.evaluationDays == 0 || days < in.evaluationDays)) in.evaluationDays = days;
        };
        take(in.state.evaluationStarted, in.state.evaluationDays);
        if (evalFile.found) take(evalFile.started, evalFile.days);
        if (evalSystem.found) take(evalSystem.started, evalSystem.days);
        take(evalHistory, historyDays);
        take(evaluationMemo_, 0);
        // 1.35.0 : la Decouverte de cette machine selon le service (decouverte.xpgdec) : une copie de plus de son debut
        // (le plus ancien gagne ; la duree reste celle du registre). Le certificat n'est pas compare a l'heure : un
        // debut signe reste un fait (et il ne peut que raccourcir).
        if (const std::string tp = deploy::joinPath(dir, kTrialFile); fileExists(tp) && in.hasDeviceKey) {
            std::string text, why;
            service::Trial trial;
            if (readAll(tp, text, &why) && checkTrial(text, text, pubKeys, serviceUrl_, 0, service::machineHash(opt_.machineId), in.deviceKeyHex, trial, nullptr, nullptr, &why)
                && trial.start > 0) {
                const bool earlier = in.evaluationStarted == 0 || trial.start < in.evaluationStarted;
                take(trial.start, 0);
                if (earlier) in.evaluationFromService = true;
            }
        }
        // l'heure la plus tardive gardee aussi par les copies (etat.bin efface pendant l'evaluation)
        in.state.latestSeen = std::max({in.state.latestSeen, evalFile.latestSeen, evalSystem.latestSeen});
    }

    // ---- 1.32.0 (M1) : une licence est la (1.34.0 : ou une evaluation a commence), l'etat protege non (efface,
    // illisible) : il n'est pas remis a zero en silence. L'heure la plus tardive repart de la plus recente des traces.
    Seconds rebuiltFloor = -1;
    if (!in.state.found && (in.hasLicenseFile || in.evaluationStarted > 0)) {
        Seconds floor = in.bundledIssuedAt;
        if (in.licenseRead) floor = std::max(floor, in.license.issuedAt);
        if (in.hasRegistry) floor = std::max(floor, in.registry.issuedAt);
        if (in.hasRevocations) floor = std::max(floor, in.revocations.issuedAt);
        for (const auto& h : historyOnce()) {
            Seconds at = 0;
            if (parseIsoTime(h.str("at"), at)) floor = std::max(floor, at);
        }
        for (const char* f : {kLicenseFile, kRegistryFile, kRevocationsFile, kHistoryFile, kDeviceKeyFile, kEvaluationFile, kActivationFile, kTrialFile, kClockFile})
            floor = std::max(floor, fileTime(deploy::joinPath(dir, f)));
        floor = std::max({floor, evalFile.latestSeen, evalSystem.latestSeen});   // 1.34.0 : les copies de l'evaluation
        in.state.latestSeen = std::max(in.state.latestSeen, floor);
        rebuiltFloor = floor;
    }

    // ---- l'horloge : l'heure la plus tardive vue, les emissions des documents signes (1.34.0 : et le debut de
    // l'evaluation).
    in.anchor = std::max(in.state.latestSeen, in.bundledIssuedAt);   // 1.32.0 : et les documents livres avec le programme
    if (in.hasRegistry) in.anchor = std::max(in.anchor, in.registry.issuedAt);
    if (in.hasRevocations) in.anchor = std::max(in.anchor, in.revocations.issuedAt);
    if (in.licenseRead) in.anchor = std::max(in.anchor, in.license.issuedAt);
    if (in.evaluationStarted > 0) in.anchor = std::max(in.anchor, in.evaluationStarted);
    const bool clockBack = in.anchor > 0 && t + opt_.clockTolerance < in.anchor;

    // ---- 1.34.0 : sans licence, l'edition d'evaluation du registre ; la premiere fois, l'evaluation commence.
    bool evalFresh = false;
    if (evaluation_ && !in.hasLicenseFile && in.hasRegistry) {
        if (const EditionDef* ed = evaluationEditionOf(in.registry)) {
            in.evaluationEdition = ed->id;
            const int edDays = evaluationDaysOf(*ed);
            // jamais prolongee : les jours notes a son debut ; un registre qui en donne moins les reduit
            in.evaluationDays = in.evaluationDays > 0 ? std::min(in.evaluationDays, edDays) : edDays;
            if (in.evaluationStarted == 0) {
                if (!in.state.issued.empty()) in.evaluationRefused = true;   // ce PC a deja eu une licence
                else if (!clockBack) {
                    in.evaluationStarted = t;   // pas tant que l'horloge est en arriere (judge : Horloge reculee)
                    evalFresh = true;
                }
            }
        }
    }

    LicenseState s = judge(in, t);
    // Ce que l'ecran Licence montre sans relire de fichier.
    registry_ = in.hasRegistry ? std::optional<Registry>(in.registry) : std::nullopt;
    license_ = in.licenseRead ? std::optional<License>(in.license) : std::nullopt;
    deviceKeyHex_ = in.deviceKeyHex;
    lastEvaluated_ = s.status;   // revue 9 : le code laisse dans le dossier (une licence valable est-elle installee ?)

    // ---- l'heure la plus tardive vue, et les numeros : gardes s'ils avancent (jamais en arriere).
    State after = in.state;
    if (s.status != LicenseStatus::ClockRolledBack) after.latestSeen = std::max(after.latestSeen, t);
    // 1.32.0 (M2) : l'emission de la licence de CE PC (signee, liee a lui) - une plus ancienne ne revient plus
    if (in.licenseRead && s.status != LicenseStatus::Invalid && s.status != LicenseStatus::OtherDevice)
        after.issued[in.license.licenseId] = std::max(after.issued[in.license.licenseId], in.license.issuedAt);
    // 1.34.0 : le debut de l'evaluation (le plus ancien) suit dans l'etat protege, meme une fois la licence la
    if (in.evaluationStarted > 0) {
        after.evaluationStarted = in.evaluationStarted;
        if (in.evaluationDays > 0) after.evaluationDays = in.evaluationDays;
    }
    // 1.35.0 : le compteur du bail, avance a chaque evaluation (garde des qu'il bouge)
    after.serviceTime = counterNow.serviceTime;
    after.serviceElapsed = counterNow.elapsed;
    after.serviceWall = counterNow.wall;
    const bool changed = !before.found || after.registryMax != before.registryMax || after.revocationsMax != before.revocationsMax
                         || after.latestSeen >= before.latestSeen + 60 || after.issued != before.issued
                         || after.evaluationStarted != before.evaluationStarted || after.evaluationDays != before.evaluationDays
                         || after.serviceTime != before.serviceTime || after.serviceElapsed != before.serviceElapsed || after.serviceWall != before.serviceWall;
    std::string unwritten;   // ce qui ne s'ecrit pas (un dossier ou ce compte n'ecrit pas) : dit une fois
    const auto addUnwritten = [&](const std::string& what) {   // 1.34.0 : chaque fichier une seule fois
        if (unwritten.find(what.substr(0, what.find(" ("))) == std::string::npos) unwritten += (unwritten.empty() ? "" : ", ") + what;
    };
    bool stateWritten = false;
    if (changed) {
        std::error_code ec;
        std::filesystem::create_directories(P(dir), ec);
        std::string w;
        if (!writeState(statePath, after, &w)) unwritten = "l'\xC3\xA9tat prot\xC3\xA9g\xC3\xA9 ne s'\xC3\xA9" "crit pas (" + w + ")";
        else {
            stateWritten = true;
            if (rebuiltFloor >= 0) {
                // une seule fois : l'etat est maintenant la
                HistoryEvent e;
                e.at = t;
                e.kind = "controle";
                e.ok = false;
                e.text = "\xC3\x89tat recr\xC3\xA9\xC3\xA9 : l'\xC3\xA9tat prot\xC3\xA9g\xC3\xA9 de ce PC manquait (ou ne se lisait plus) "
                         + std::string(in.hasLicenseFile ? "alors qu'une licence est install\xC3\xA9" "e" : "pendant la D\xC3\xA9" "couverte de ce PC") + " ; "
                         "l'heure la plus tardive est reprise des documents sign\xC3\xA9s, de l'historique et des fichiers du dossier (" + isoTime(rebuiltFloor) + ").";
                if (!addHistory(e, "", &w)) unwritten = "l'historique ne s'\xC3\xA9" "crit pas (" + w + ")";
            }
        }
    }
    // ---- 1.35.0 : les copies du compteur du bail (horloge.bin, HKCU) : une qui manque, ou en retard, est remise ; ce
    // lancement garde le sien en memoire (une copie de plus) avec l'horloge monotone de ce moment.
    if (counterNow.serviceTime > 0) {
        const ClockCopy c{counterNow.serviceTime, counterNow.elapsed, counterNow.wall};
        if (!(clock.file == counterNow)) {
            std::error_code ec;
            std::filesystem::create_directories(P(dir), ec);
            std::string w;
            if (!writeClockFile(deploy::joinPath(dir, kClockFile), c, &w)) addUnwritten(std::string(kClockFile) + " ne s'\xC3\xA9" "crit pas (" + w + ")");
        }
#ifdef _WIN32
        if (systemEvaluationCopy() && !(clock.system == counterNow)) {
            crypto::Bytes sealed;
            std::string w;
            if (sealClock(c, sealed, &w)) (void)writeSystemValue(kClockRegValue, sealed, &w);
        }
#endif
        memoServiceTime_ = counterNow.serviceTime;
        memoElapsed_ = counterNow.elapsed;
        memoWall_ = counterNow.wall;
        memoSteady_ = clock.steady;
        memoSet_ = true;
    }
    // ---- 1.34.0 : les copies du debut de l'evaluation (sans licence) : une qui manque, ou en retard, est remise.
    if (evaluation_ && in.evaluationStarted > 0 && !in.hasLicenseFile && (in.evaluationDays > 0 || after.evaluationDays > 0)) {
        EvaluationCopy c;
        c.started = in.evaluationStarted;
        c.days = in.evaluationDays > 0 ? in.evaluationDays : after.evaluationDays;
        c.latestSeen = after.latestSeen;
        // qui garde ce debut, une fois les ecritures faites (un debut que rien ne garde repart au lancement suivant)
        bool kept = before.evaluationStarted == c.started || stateWritten;
        const bool fileStale = !evalFile.found || evalFile.started != c.started || evalFile.days != c.days;
        if (!fileStale) kept = true;
        if (fileStale || changed) {
            std::error_code ec;
            std::filesystem::create_directories(P(dir), ec);
            std::string w;
            if (writeEvaluationFile(evaluationPath, c, &w)) kept = true;
            else addUnwritten(std::string(kEvaluationFile) + " ne s'\xC3\xA9" "crit pas (" + w + ")");
        }
#ifdef _WIN32
        if (systemEvaluationCopy()) {
            const bool systemStale = !evalSystem.found || evalSystem.started != c.started || evalSystem.days != c.days;
            std::string w;
            if (!systemStale) kept = true;
            if ((systemStale || changed) && writeEvaluationSystem(c, &w)) kept = true;
        }
#endif
        if (evalHistory == c.started) {
            kept = true;
        } else {
            // la ligne de l'historique : au debut ; remise si elle manque (l'historique efface, ou parti au-dela de 500 lignes)
            const Seconds endsAt = c.started + static_cast<Seconds>(c.days) * kDay;
            core::json::Value v = core::json::Value::object();
            v.set("at", isoTime(t));
            v.set("kind", std::string("evaluation"));
            v.set("ok", true);
            // 1.35.0 : << Decouverte >> pour l'utilisateur (la ligne garde son genre "evaluation")
            v.set("text", evalFresh ? "D\xC3\xA9" "couverte commenc\xC3\xA9" "e : " + plural(c.days, "jour", "jours") + ", jusqu'au " + frenchDate(endsAt) + "."
                          : in.evaluationFromService
                              ? "D\xC3\xA9" "couverte commenc\xC3\xA9" "e le " + frenchDate(c.started) + " sur cette machine, selon le service des licences : "
                                    + plural(c.days, "jour", "jours") + ", jusqu'au " + frenchDate(endsAt) + "."
                              : "D\xC3\xA9" "couverte en cours, commenc\xC3\xA9" "e le " + frenchDate(c.started) + " : " + plural(c.days, "jour", "jours")
                                    + ", jusqu'au " + frenchDate(endsAt) + " (sa trace est remise dans l'historique).");
            v.set("started", isoTime(c.started));
            v.set("days", c.days);
            std::string w;
            if (appendHistoryLine(historyPath, v, &w)) kept = true;
            else addUnwritten("l'historique ne s'\xC3\xA9" "crit pas (" + w + ")");
        }
        if (kept) {
            evaluationMemo_ = 0;
        } else {
            // Rien ne garde ce debut : il vaut pour ce lancement (le programme ouvert) ; le suivant repartira.
            evaluationMemo_ = c.started;
            addDetail(s, "le d\xC3\xA9" "but de la D\xC3\xA9" "couverte n'a pas pu \xC3\xAAtre not\xC3\xA9 sur ce PC : elle n'est pas gard\xC3\xA9" "e d'un lancement \xC3\xA0 l'autre "
                         "tant que ce compte ne peut pas \xC3\xA9" "crire dans le dossier des licences");
        }
    }
    if (rebuiltFloor >= 0)
        in.stateNote = (in.stateNote.empty() ? std::string("l'\xC3\xA9tat prot\xC3\xA9g\xC3\xA9 de ce PC manquait") : in.stateNote)
                       + " ; recr\xC3\xA9\xC3\xA9, l'heure la plus tardive reprise au " + isoTime(rebuiltFloor);
    if (!in.stateNote.empty() && s.detail.empty()) s.detail = in.stateNote;
    else if (rebuiltFloor >= 0) addDetail(s, in.stateNote);
    if (const std::string w = noteState(s); !w.empty()) addUnwritten(w);
    if (!unwritten.empty()) addDetail(s, notWritable(unwritten));
    return s;
}

// ==================================================================== l'historique ==
std::vector<HistoryEvent> LicenseStore::history(int limit) const {
    std::vector<HistoryEvent> out;
    const auto lines = readHistoryLines(deploy::joinPath(opt_.folder, kHistoryFile), nullptr);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (limit > 0 && static_cast<int>(out.size()) >= limit) break;
        HistoryEvent e;
        (void)parseIsoTime(it->str("at"), e.at);
        e.kind = it->str("kind");
        e.ok = it->flag("ok");
        e.text = it->str("text");
        out.push_back(std::move(e));
    }
    return out;
}

bool LicenseStore::addHistory(const HistoryEvent& e, std::string_view statusKey, std::string* why) {
    core::json::Value v = core::json::Value::object();
    v.set("at", isoTime(e.at));
    v.set("kind", e.kind);
    v.set("ok", e.ok);
    if (!statusKey.empty()) v.set("status", std::string(statusKey));
    v.set("text", e.text);
    return appendHistoryLine(deploy::joinPath(opt_.folder, kHistoryFile), v, why);   // les lignes illisibles ne sont pas recopiees
}

std::string LicenseStore::lastLoggedStatus() const {
    const auto lines = readHistoryLines(deploy::joinPath(opt_.folder, kHistoryFile), nullptr);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it)
        if (it->str("kind") == "etat") return it->str("status");
    return {};
}

// Un changement d'etat constate (de Valide a Grace...) : une ligne "etat" dans l'historique. Le
// dernier etat note est relu dans le fichier (un autre programme de ce PC a pu le noter deja).
// Rend ce qui ne s'est pas ecrit (vide : rien a dire).
std::string LicenseStore::noteState(const LicenseState& s) {
    const std::string key = statusKey(s.status);
    if (key == lastStatus_) return {};
    lastStatus_ = key;
    const std::string prev = lastLoggedStatus();
    if (prev == key) return {};
    std::string prevLabel;
    for (int i = 0; i <= static_cast<int>(kLastLicenseStatus); ++i)
        if (prev == statusKey(static_cast<LicenseStatus>(i))) prevLabel = statusLabel(static_cast<LicenseStatus>(i));
    HistoryEvent e;
    e.at = s.evaluatedAt;
    e.kind = "etat";
    e.ok = statusIsNormal(s.status);
    // 1.34.0 : une phrase qui commence deja par l'etiquette (<< Decouverte : il te reste... >>) ne la repete pas
    const std::string label = statusLabel(s.status) + " : ";
    e.text = (prevLabel.empty() ? std::string() : prevLabel + " \xE2\x86\x92 ") + (s.message.rfind(label, 0) == 0 ? s.message : label + s.message);
    if (deferState_) {
        pendingState_ = e;
        pendingStatus_ = key;
        return {};
    }
    std::string w;
    if (!addHistory(e, key, &w)) return "l'historique ne s'\xC3\xA9" "crit pas (" + w + ")";
    return {};
}

LicenseState LicenseStore::judge(const Loaded& in, Seconds t) const {
    LicenseState s;
    s.evaluatedAt = t;
    s.registryNumber = in.hasRegistry ? in.registry.number : 0;
    s.revocationNumber = in.hasRevocations ? in.revocations.number : 0;
    s.registryProblem = in.registryProblem;
    s.deviceFingerprint = in.deviceFingerprint;
    s.trustedNow = in.trustedNow > 0 ? in.trustedNow : t;   // 1.35.0
    s.lastServiceSync = in.serviceTime;

    // L'edition sans licence (FREE) : ce qui s'applique hors de Valide et Grace.
    const EditionDef* free = in.hasRegistry ? in.registry.edition(in.registry.unlicensedEdition) : nullptr;
    const auto applyFree = [&] {
        if (free) {
            s.edition = free->id;
            s.editionLabel = free->label;
            s.features = free->features;
            if (s.maxDevices == 0) s.maxDevices = free->maxDevices;
        } else {
            s.edition.clear();
            s.editionLabel.clear();
            s.features.clear();
        }
    };
    const std::string freeName = free ? (free->label.empty() ? free->id : free->label) : std::string();
    const std::string fallback = free ? "l'\xC3\xA9" "dition " + freeName + " s'applique"
                                      : "seules les fonctions qui ne demandent aucune licence restent ouvertes (le registre des \xC3\xA9" "ditions manque ou est refus\xC3\xA9)";
    static const std::string kNoLoss = " Ton projet reste ouvert ; Enregistrer et Exporter restent permis.";

    // Une horloge en arriere : l'heure la plus tardive vue, et les dates d'emission des documents signes (evaluate).
    const Seconds anchor = in.anchor;
    const bool clockBack = anchor > 0 && t + opt_.clockTolerance < anchor;
    // `seen` : l'heure deja vue ; `fix` : ce qui la remet (une licence en ligne : aussi la connexion, l'heure signee recale).
    const auto clockRolledBack = [&](Seconds seen, const std::string& fix) {
        s.status = LicenseStatus::ClockRolledBack;
        s.detail = "heure du PC " + isoTime(t) + ", heure la plus tardive d\xC3\xA9j\xC3\xA0 vue " + isoTime(seen);
        applyFree();
        s.message = "L'horloge de ce PC indique le " + isoDate(t) + ", alors que le " + isoDate(seen) + " a d\xC3\xA9j\xC3\xA0 \xC3\xA9t\xC3\xA9 vu : " + fix
                    + ". En attendant, " + fallback + "." + kNoLoss;
        return s;
    };
    static const std::string kFixClock = "remets-la \xC3\xA0 l'heure";

    if (!in.hasLicenseFile) {
        // 1.34.0 : la periode d'evaluation de ce PC (evaluate : son edition, son debut le plus ancien, ses jours).
        if (const EditionDef* ev = in.evaluationEdition.empty() ? nullptr : in.registry.edition(in.evaluationEdition)) {
            const int days = in.evaluationDays > 0 ? in.evaluationDays : evaluationDaysOf(*ev);
            if (in.evaluationStarted == 0 && clockBack) {   // elle ne commence pas tant que l'horloge est en arriere
                clockRolledBack(anchor, kFixClock);
                s.evaluationEdition = ev->id;
                addDetail(s, "la D\xC3\xA9" "couverte commencera quand l'horloge sera remise \xC3\xA0 l'heure");
                return s;
            }
            if (in.evaluationStarted > 0) {
                s.evaluationEdition = ev->id;
                s.evaluationStartedAt = in.evaluationStarted;
                s.evaluationEndsAt = in.evaluationStarted + static_cast<Seconds>(days) * kDay;
                if (clockBack) return clockRolledBack(anchor, kFixClock);
                if (t < s.evaluationEndsAt) {
                    s.status = LicenseStatus::Evaluation;
                    s.evaluationDaysLeft = static_cast<int>(std::min<Seconds>(days, (s.evaluationEndsAt - t + kDay - 1) / kDay));
                    s.edition = ev->id;
                    s.editionLabel = ev->label;
                    s.features = ev->features;
                    s.maxDevices = ev->maxDevices;
                    s.type = LicenseType::Evaluation;
                    s.expiresAt = s.evaluationEndsAt;
                    s.message = "D\xC3\xA9" "couverte : il te reste " + plural(s.evaluationDaysLeft, "jour", "jours") + " (jusqu'au " + frenchDate(s.evaluationEndsAt)
                                + "). Pour continuer ensuite, demande une licence (menu Licence\xE2\x80\xA6).";
                    return s;
                }
                s.status = LicenseStatus::Absent;
                s.evaluationOver = true;
                applyFree();
                s.message = "Ta D\xC3\xA9" "couverte de " + plural(days, "jour", "jours") + " est finie depuis le " + frenchDate(s.evaluationEndsAt)
                            + " : demande une licence (menu Licence\xE2\x80\xA6). En attendant, " + fallback + ".";
                return s;
            }
        }
        s.status = LicenseStatus::Absent;
        applyFree();
        s.message = free ? "Aucune licence sur ce PC : " + fallback + "." : "Aucune licence sur ce PC, et " + fallback + ".";
        if (!in.registryProblem.empty()) s.detail = in.registryProblem;
        if (in.evaluationRefused)
            addDetail(s, "pas de D\xC3\xA9" "couverte : une licence a d\xC3\xA9j\xC3\xA0 \xC3\xA9t\xC3\xA9 install\xC3\xA9" "e sur ce PC");
        return s;
    }
    if (in.licenseRead) {
        const License& l = in.license;
        s.licensedEdition = l.edition;
        s.licenseId = l.licenseId;
        s.customer = l.customer;
        s.type = l.type;
        s.maxDevices = l.maxDevices;
        s.boundDevices = static_cast<int>(l.devices.size());
        s.issuedAt = l.issuedAt;
        s.expiresAt = l.expiresAt;
    }
    const auto invalid = [&](const std::string& detail, const std::string& head = std::string()) {
        s.status = LicenseStatus::Invalid;
        s.detail = detail;
        applyFree();
        s.message = head.empty() ? "Le fichier de licence de ce PC n'est pas valide : " + fallback + "." + kNoLoss
                                       + " Demande un nouveau fichier \xC3\xA0 l'\xC3\xA9" "diteur."
                                 : head + " En attendant, " + fallback + "." + kNoLoss;
        return s;
    };
    if (!in.licenseRead) return invalid(in.licenseProblem);
    const License& l = in.license;
    if (!in.hasRegistry) return invalid(in.registryProblem);
    // Une liste de revocation deja vue qui a disparu, ou remplacee par une plus ancienne : pas de retour a Valide.
    if (!in.hasRevocations && in.state.revocationsMax > 0) return invalid(in.revocationsProblem);
    std::string why;
    if (!checkAgainstRegistry(l, in.registry, &why)) return invalid(why);

    const auto otherDevice = [&](const std::string& detail, const std::string& head = std::string()) {
        s.status = LicenseStatus::OtherDevice;
        s.detail = detail;
        applyFree();
        s.message = head.empty() ? "Cette licence (" + l.licenseId + ") a \xC3\xA9t\xC3\xA9 d\xC3\xA9livr\xC3\xA9" "e pour un autre PC : " + fallback + "." + kNoLoss
                                       + " Envoie la demande d'activation de ce PC \xC3\xA0 l'\xC3\xA9" "diteur pour recevoir la tienne."
                                 : head + " En attendant, " + fallback + "." + kNoLoss;
        return s;
    };
    // 1.35.0 : le bail de l'activation fini (ou plus d'activation valable) : connecter ce PC a Internet.
    const auto leaseExpired = [&](const std::string& detail, const std::string& head) {
        s.status = LicenseStatus::LeaseExpired;
        s.detail = detail;
        applyFree();
        s.message = head + " En attendant, " + fallback + "." + kNoLoss;
        return s;
    };
    const bool online = !l.service.empty();
    s.online = online;
    s.floating = l.type == LicenseType::Floating;
    if (online) {
        // 1.35.0 - UNE LICENCE EN LIGNE : elle ne lie aucun PC ; elle vaut ici avec l'activation de CE PC, signee par
        // le service (certifie par l'editeur), dont le bail court encore a l'heure retenue.
        const Seconds tn = s.trustedNow;
        if (in.activationFault == ActivationFault::None || in.activationFault == ActivationFault::CertExpired) {
            s.activationUntil = in.activation.until;
            s.activationDaysLeft = in.activation.until > tn ? static_cast<int>((in.activation.until - tn + kDay - 1) / kDay) : 0;
        }
        if (!in.counterFound) s.activationDaysLeft = 0;   // revue 1 : l'heure du service n'est pas connue ici
        switch (in.activationFault) {
            case ActivationFault::Missing:
            case ActivationFault::OtherLicense:
                return leaseExpired(in.activationProblem, "Ce PC n'a pas l'activation de la licence " + l.licenseId
                                                              + " : connecte-le \xC3\xA0 Internet pour la renouveler, ou entre ton code d'activation (menu Licence\xE2\x80\xA6).");
            case ActivationFault::Invalid:
                return invalid(in.activationProblem, "L'activation de la licence " + l.licenseId
                                                         + " sur ce PC n'est pas valide : connecte-le \xC3\xA0 Internet pour la renouveler, ou entre ton code d'activation (menu Licence\xE2\x80\xA6).");
            case ActivationFault::OtherDevice:
                return otherDevice(in.activationProblem, "L'activation de la licence " + l.licenseId
                                                             + " est celle d'un autre PC : entre ton code d'activation sur ce PC (menu Licence\xE2\x80\xA6).");
            case ActivationFault::CertExpired:
                return leaseExpired(in.activationProblem, "Connecte ce PC \xC3\xA0 Internet pour renouveler sa licence (le certificat du service a expir\xC3\xA9 le "
                                                              + frenchDate(in.certExpiresAt) + ").");
            case ActivationFault::None: break;
        }
        // revue 1 : aucune copie lisible du compteur (etat.bin, horloge.bin, HKCU effaces) : l'heure du service n'est plus
        // connue sur ce PC, et l'horloge du PC seule ne prouve rien (on la remet ou l'on veut) -> la connexion suivante.
        if (!in.counterFound)
            return leaseExpired("aucune copie lisible du compteur du bail (l'heure sign\xC3\xA9" "e du service) sur ce PC",
                                "Connecte ce PC \xC3\xA0 Internet pour renouveler sa licence : l'heure du service des licences n'est plus connue sur ce PC.");
        if (in.activation.until <= tn)
            return leaseExpired("bail de l'activation fini le " + isoTime(in.activation.until) + ", heure retenue " + isoTime(tn),
                                "Connecte ce PC \xC3\xA0 Internet pour renouveler sa licence (bail fini le " + frenchDate(in.activation.until) + ").");
        s.activationWarning = in.activation.until - tn <= 7 * kDay;
    } else {
        if (!in.hasDeviceKey || !l.boundTo(in.deviceKeyHex))
            return otherDevice(in.hasDeviceKey ? "la cl\xC3\xA9 de ce PC (" + in.deviceFingerprint + ") n'est pas parmi les " + plural(l.devices.size(), "PC li\xC3\xA9", "PC li\xC3\xA9s")
                                               : in.deviceProblem);
        // 1.32.0 (H2) : le PC lie porte l'empreinte de l'installation qui a fait la demande ; une autre (la cle de ce
        // PC recopiee ailleurs) n'est pas ce PC. Sans empreinte (une licence d'avant) : rien a comparer. L'identifiant de
        // Windows illisible sur ce PC (empreinte locale vide) : rien a comparer non plus - jamais une licence valide perdue.
        if (const BoundDevice* d = l.device(in.deviceKeyHex); d && !d->station.empty() && !in.station.empty() && d->station != in.station)
            return otherDevice("la cl\xC3\xA9 de ce PC (" + in.deviceFingerprint + ") a \xC3\xA9t\xC3\xA9 activ\xC3\xA9" "e sur une autre installation de Windows : "
                               "l'empreinte du poste ne correspond pas (la cl\xC3\xA9 a-t-elle \xC3\xA9t\xC3\xA9 recopi\xC3\xA9" "e ?)");
    }
    // 1.32.0 (M2) : une emission plus ancienne que la plus recente deja installee (un vieux .xpglic remis a sa place).
    if (const auto it = in.state.issued.find(l.licenseId); it != in.state.issued.end() && l.issuedAt < it->second)
        return invalid("c'est une \xC3\xA9mission plus ancienne (\xC3\xA9mise le " + isoDate(l.issuedAt) + ") de la licence " + l.licenseId
                       + " que celle d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e sur ce PC (\xC3\xA9mise le " + isoDate(it->second)
                       + ") : remets le dernier fichier re\xC3\xA7u de l'\xC3\xA9" "diteur");
    if (in.hasRevocations) {
        if (const Revocation* r = in.revocations.find(l.licenseId, in.deviceKeyHex, l.issuedAt)) {
            s.status = LicenseStatus::Revoked;
            s.detail = "liste de r\xC3\xA9vocation n\xC2\xB0 " + std::to_string(in.revocations.number) + (r->reason.empty() ? "" : ", motif : " + r->reason);
            applyFree();
            s.message = "Ta licence " + l.licenseId + " a \xC3\xA9t\xC3\xA9 r\xC3\xA9voqu\xC3\xA9" "e par l'\xC3\xA9" "diteur le " + isoDate(r->revokedAt)
                        + (r->reason.empty() ? "" : " (motif : " + r->reason + ")") + " : " + fallback + "." + kNoLoss;
            return s;
        }
    }
    const bool timed = l.expiresAt != 0;
    if (online) {
        // revue 1 : l'horloge du PC en arriere (plus que la tolerance) sur la plus avancee deja comptee depuis la derniere
        // heure signee : Horloge reculee, datee ou non - jusqu'a la connexion suivante (l'heure signee recale le compteur).
        // Pas l'heure la plus tardive de toujours (etat.bin) : une horloge avancee par erreur une fois bloquerait la
        // licence en ligne meme apres la connexion.
        if (in.wallSeen > 0 && t + opt_.clockTolerance < in.wallSeen)
            return clockRolledBack(in.wallSeen, "remets-la \xC3\xA0 l'heure, ou connecte ce PC \xC3\xA0 Internet pour la recaler");
    } else if (timed && clockBack) {
        return clockRolledBack(anchor, kFixClock);
    }
    // revue 10 : la fin (et la grace) d'une licence en ligne se compare a l'heure retenue, pas a l'horloge du PC
    const Seconds tEnd = online ? s.trustedNow : t;
    const std::string edLabel = [&] {
        const EditionDef* e = in.registry.edition(l.edition);
        return e && !e->label.empty() ? e->label : l.edition;
    }();
    // 1.35.0 : une licence flottante ouvre ses fonctionnalites avec un jeton (un poste pris) pour ce PC, pas fini a
    // l'heure du service. Vrai : pas de poste (l'etat est ecrit dans s).
    const auto noSeat = [&]() -> bool {
        if (!s.floating) return false;
        if (in.leaseOk && in.lease.until > in.serviceNow) {
            s.leaseUntil = in.lease.until;
            return false;
        }
        s.status = LicenseStatus::NoFloatingSeat;
        s.detail = in.leaseOk ? "le jeton a pris fin le " + isoTime(in.lease.until) : in.leaseProblem;
        applyFree();
        s.message = "Licence flottante " + edLabel + " : ce PC n'a pas de poste en ce moment. Connecte-le \xC3\xA0 Internet pour en prendre un ; en attendant, "
                    + fallback + "." + kNoLoss;
        return true;
    };
    // 1.35.0 : la phrase du bail d'une licence en ligne (ajoutee a Valide et Grace).
    const std::string bail = !online ? std::string()
                             : s.activationWarning ? " Connecte ce PC \xC3\xA0 Internet avant le " + frenchDate(in.activation.until) + " pour renouveler sa licence."
                             : s.floating ? std::string()
                                          : " Activ\xC3\xA9" "e en ligne jusqu'au " + frenchDate(in.activation.until) + " (renouvel\xC3\xA9" "e \xC3\xA0 chaque connexion).";
    if (timed && tEnd >= l.expiresAt) {
        const Seconds graceEnd = l.type == LicenseType::Evaluation ? l.expiresAt : l.expiresAt + static_cast<Seconds>(l.offlineGraceDays) * kDay;
        if (tEnd < graceEnd) {
            if (noSeat()) return s;
            s.status = LicenseStatus::Grace;
            s.graceEndsAt = graceEnd;
            s.graceDaysLeft = static_cast<int>((graceEnd - tEnd + kDay - 1) / kDay);
            s.edition = l.edition;
            s.editionLabel = edLabel;
            s.features = l.features;
            s.message = "Ta licence " + edLabel + " a pris fin le " + isoDate(l.expiresAt) + " : ses fonctionnalit\xC3\xA9s restent ouvertes encore "
                        + plural(s.graceDaysLeft, "jour", "jours")
                        + (online ? std::string(", le temps que son renouvellement arrive (connecte ce PC \xC3\xA0 Internet). Ensuite, ")
                                  : std::string(", le temps d'importer son renouvellement. Ensuite, "))
                        + (free ? "l'\xC3\xA9" "dition " + freeName + " s'appliquera" : std::string("seules les fonctions de base resteront")) + " ; tes projets restent ouverts et enregistrables."
                        + bail;
            return s;
        }
        s.status = LicenseStatus::Expired;
        s.graceEndsAt = graceEnd > l.expiresAt ? graceEnd : 0;
        applyFree();
        s.message = (l.type == LicenseType::Evaluation ? "Ta licence d'\xC3\xA9valuation " : "Ta licence ") + edLabel + " a pris fin le "
                    + isoDate(l.expiresAt) + " : " + fallback + "." + kNoLoss
                    + (online ? " Pour retrouver tes fonctionnalit\xC3\xA9s, connecte ce PC \xC3\xA0 Internet : son renouvellement arrive par le service des licences."
                              : " Pour retrouver tes fonctionnalit\xC3\xA9s, importe le renouvellement re\xC3\xA7u de l'\xC3\xA9" "diteur.");
        return s;
    }
    if (noSeat()) return s;
    s.status = LicenseStatus::Valid;
    s.edition = l.edition;
    s.editionLabel = edLabel;
    s.features = l.features;
    if (s.floating) s.message = "Licence flottante " + edLabel + (timed ? ", valide jusqu'au " + isoDate(l.expiresAt) : std::string()) + " : ce PC a un poste en ce moment.";
    else if (!timed) s.message = "Licence " + edLabel + " perp\xC3\xA9tuelle, valide sur ce PC.";
    else if (l.type == LicenseType::Evaluation) s.message = "Licence d'\xC3\xA9valuation " + edLabel + ", valide jusqu'au " + isoDate(l.expiresAt) + ".";
    else s.message = "Licence " + edLabel + " valide jusqu'au " + isoDate(l.expiresAt) + ".";
    s.message += bail;
    if (!in.revocationsProblem.empty()) s.detail = in.revocationsProblem;
    return s;
}

// ======================================================================= l'import ==
ImportReport LicenseStore::importFile(const std::string& path) {
    std::string bytes, why;
    if (!readAll(path, bytes, &why)) {
        deferState_ = true;
        ImportReport r;
        r.why = "Ce fichier ne se lit pas : " + why + ".";
        r.state = evaluate();
        deferState_ = false;
        return finishImport(std::move(r));
    }
    return importBytes(bytes);
}

ImportReport LicenseStore::importBytes(std::string_view bytes) {
    deferState_ = true;
    ImportReport r;
    if (!configured()) {
        r.why = kNotConfiguredImport;
        r.state = evaluate();
    } else {
        r = importChecked(bytes);
    }
    deferState_ = false;
    return finishImport(std::move(r));
}

// L'import dans l'historique (accepte : ce qui a ete installe ; refuse : la raison), puis le
// changement d'etat qu'il a amene.
ImportReport LicenseStore::finishImport(ImportReport rep, bool logImport) {
    if (!logImport) {
        // 1.35.0 : un renouvellement d'activation - seulement le changement d'etat, s'il y en a un
        if (pendingState_) {
            std::string w;
            if (!addHistory(*pendingState_, pendingStatus_, &w)) addDetail(rep.state, notWritable("l'historique ne s'\xC3\xA9" "crit pas (" + w + ")"));
            pendingState_.reset();
        }
        return rep;
    }
    HistoryEvent e;
    e.at = now();
    e.kind = "import";
    e.ok = rep.ok;
    if (rep.ok) {
        for (const auto& l : rep.lines) e.text += (e.text.empty() ? "" : " ") + l;
        if (e.text.empty()) e.text = "Import : rien de nouveau.";
    } else {
        e.text = "Refus\xC3\xA9 : " + rep.why;
    }
    std::string w;
    bool written = addHistory(e, "", &w);
    if (pendingState_) {
        written = addHistory(*pendingState_, pendingStatus_, written ? &w : nullptr) && written;
        pendingState_.reset();
    }
    if (!written) addDetail(rep.state, notWritable("l'historique ne s'\xC3\xA9" "crit pas (" + w + ")"));
    return rep;
}

ImportReport LicenseStore::importChecked(std::string_view bytes) {
    ImportReport rep;
    const auto refuse = [&](const std::string& text) {
        rep.ok = false;
        rep.why = text;
        rep.state = evaluate();
        return rep;
    };
    std::vector<SignedBlock> blocks;
    std::string why;
    if (!readBlocks(bytes, blocks, &why)) return refuse("Ce fichier est refus\xC3\xA9 : " + why + ".");
    const SignedBlock *licB = nullptr, *regB = nullptr, *revB = nullptr;
    for (const auto& b : blocks) {
        const SignedBlock** slot = b.kind == DocKind::License ? &licB : b.kind == DocKind::Registry ? &regB : b.kind == DocKind::Revocations ? &revB : nullptr;
        if (!slot && b.kind != DocKind::Request)   // 1.34.0 / 1.35.0 : les documents en ligne ne s'importent pas en fichier
            return refuse("Ce fichier est refus\xC3\xA9 : c'est un document du service des licences (" + docKindLabel(b.kind)
                          + "), il ne s'importe pas comme un fichier ; ce PC le re\xC3\xA7oit en ligne (menu Licence\xE2\x80\xA6).");
        if (!slot) return refuse("Ce fichier est une demande d'activation : elle est \xC3\xA0 envoyer \xC3\xA0 l'\xC3\xA9" "diteur, pas \xC3\xA0 importer ici.");
        if (*slot) return refuse("Ce fichier est refus\xC3\xA9 : il contient deux fois " + docKindLabel(b.kind) + ".");
        *slot = &b;
        if (!verifyTrusted(b, &why)) return refuse("Ce fichier est refus\xC3\xA9 : " + why + ".");
    }
    Registry reg;
    RevocationList rev;
    License lic;
    if (regB && !parseRegistry(regB->document, reg, &why)) return refuse("Ce fichier est refus\xC3\xA9 : " + why + ".");
    if (revB && !parseRevocations(revB->document, rev, &why)) return refuse("Ce fichier est refus\xC3\xA9 : " + why + ".");
    if (licB && !parseLicense(licB->document, lic, &why)) return refuse("Ce fichier est refus\xC3\xA9 : " + why + ".");

    // Ce que ce PC a deja.
    const std::string dir = opt_.folder;
    const std::string statePath = deploy::joinPath(dir, kStateFile);
    State st = readState(statePath, nullptr);
    // 1.32.0 (M1) : une licence sans etat protege : evaluate le recree (ancre) avant que l'import n'en ecrive un neuf.
    if (!st.found && fileExists(deploy::joinPath(dir, kLicenseFile))) {
        (void)evaluate();
        st = readState(statePath, nullptr);
    }
    Registry curReg;
    bool hasCurReg = false;
    {
        SignedBlock b;
        std::string w;
        hasCurReg = fileExists(deploy::joinPath(dir, kRegistryFile)) && readStoredBlock(deploy::joinPath(dir, kRegistryFile), DocKind::Registry, b, &w)
                    && verifyTrusted(b, &w) && parseRegistry(b.document, curReg, &w) && curReg.number >= st.registryMax;
    }
    RevocationList curRev;
    bool hasCurRev = false;
    {
        SignedBlock b;
        std::string w;
        hasCurRev = fileExists(deploy::joinPath(dir, kRevocationsFile))
                    && readStoredBlock(deploy::joinPath(dir, kRevocationsFile), DocKind::Revocations, b, &w) && verifyTrusted(b, &w)
                    && parseRevocations(b.document, curRev, &w) && curRev.number >= st.revocationsMax;
    }

    // Le registre : plus recent que ce que ce PC a -> installe ; le meme -> a jour ; plus ancien que le plus
    // recent deja vu -> jamais installe (seul : refuse ; joint a une licence : ignore, le plus recent reste).
    bool writeReg = false, writeRev = false;
    const auto judgeDoc = [&](std::uint64_t number, std::uint64_t maxSeen, bool hasCur, std::uint64_t curNumber, const std::string& name,
                              bool feminine, bool& write) -> std::string {
        const std::string n = name + " n\xC2\xB0 " + std::to_string(number);
        const std::uint64_t best = std::max(maxSeen, hasCur ? curNumber : 0);
        if (number < best) {
            if (!licB)
                return std::string(feminine ? "Cette " : "Ce ") + name + " (n\xC2\xB0 " + std::to_string(number) + ") est plus " + (feminine ? "ancienne" : "ancien")
                       + " que le n\xC2\xB0 " + std::to_string(best) + " d\xC3\xA9j\xC3\xA0 vu sur ce PC : " + (feminine ? "elle" : "il") + " est refus\xC3\xA9"
                       + (feminine ? "e." : ".");
            rep.lines.push_back(n + " : ce PC a d\xC3\xA9j\xC3\xA0 le n\xC2\xB0 " + std::to_string(best) + ", plus r\xC3\xA9" "cent ; il est gard\xC3\xA9.");
        } else if (hasCur && curNumber == number) {
            rep.lines.push_back(n + " : d\xC3\xA9j\xC3\xA0 \xC3\xA0 jour.");
        } else {
            write = true;
            rep.lines.push_back(n + std::string(" : install\xC3\xA9") + (feminine ? "e." : "."));
        }
        return {};
    };
    if (regB) {
        const std::string r = judgeDoc(reg.number, st.registryMax, hasCurReg, curReg.number, "registre", false, writeReg);
        if (!r.empty()) return refuse(r);
    }
    if (revB) {
        const std::string r = judgeDoc(rev.number, st.revocationsMax, hasCurRev, curRev.number, "liste de r\xC3\xA9vocation", true, writeRev);
        if (!r.empty()) return refuse(r);
    }
    const Registry* useReg = writeReg ? &reg : hasCurReg ? &curReg : nullptr;
    const RevocationList* useRev = writeRev ? &rev : hasCurRev ? &curRev : nullptr;

    // La licence : seulement si elle vaut pour CE PC, maintenant.
    bool writeLic = false;
    if (licB) {
        const std::string pre = "La licence " + lic.licenseId + " est refus\xC3\xA9" "e : ";
        // 1.35.0 : une licence en ligne ne s'installe qu'avec l'activation de ce PC, recue du service (installActivation).
        if (!lic.service.empty())
            return refuse(pre + "c'est une licence en ligne : elle s'active avec son code d'activation (menu Licence\xE2\x80\xA6), pas par un fichier.");
        if (!useReg) return refuse(pre + "le registre des \xC3\xA9" "ditions manque sur ce PC (importe le fichier complet re\xC3\xA7u de l'\xC3\xA9" "diteur).");
        if (!checkAgainstRegistry(lic, *useReg, &why)) return refuse(pre + why + ".");
        std::string keyHex, station;
        {
            const std::string kp = deploy::joinPath(dir, kDeviceKeyFile);
            if (!fileExists(kp)) return refuse(pre + "ce PC n'a pas encore de cl\xC3\xA9 d'identit\xC3\xA9 : \xC3\xA9" "cris d'abord sa demande d'activation, et envoie-la \xC3\xA0 l'\xC3\xA9" "diteur.");
            auto key = SigningKey::load(kp, deviceKeySpec(), &why);
            if (!key) return refuse(pre + why);
            keyHex = key->publicKeyHex();
            station = stationHash(opt_.machineId, key->publicKey());
        }
        if (!lic.boundTo(keyHex)) return refuse(pre + "elle a \xC3\xA9t\xC3\xA9 d\xC3\xA9livr\xC3\xA9" "e pour un autre PC. Envoie la demande d'activation de CE PC \xC3\xA0 l'\xC3\xA9" "diteur.");
        // 1.32.0 (H2) : liee a cette cle, mais pour une autre installation de Windows (la cle recopiee).
        if (const BoundDevice* d = lic.device(keyHex); d && !d->station.empty() && !station.empty() && d->station != station)
            return refuse(pre + "elle a \xC3\xA9t\xC3\xA9 d\xC3\xA9livr\xC3\xA9" "e pour une autre installation de Windows (la cl\xC3\xA9 de ce PC a-t-elle \xC3\xA9t\xC3\xA9 recopi\xC3\xA9" "e ?). "
                                "Envoie la demande d'activation de CE PC \xC3\xA0 l'\xC3\xA9" "diteur.");
        // 1.32.0 (M2) : jamais une emission plus ancienne qu'une deja installee sur ce PC (meme remplacee depuis).
        if (const auto it = st.issued.find(lic.licenseId); it != st.issued.end() && lic.issuedAt < it->second)
            return refuse(pre + "elle est plus ancienne (\xC3\xA9mise le " + isoDate(lic.issuedAt) + ") qu'une \xC3\xA9mission d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e sur ce PC (\xC3\xA9mise le "
                                + isoDate(it->second) + ").");
        if (useRev) {
            if (const Revocation* r = useRev->find(lic.licenseId, keyHex, lic.issuedAt))
                return refuse(pre + "elle a \xC3\xA9t\xC3\xA9 r\xC3\xA9voqu\xC3\xA9" "e par l'\xC3\xA9" "diteur le " + isoDate(r->revokedAt) + (r->reason.empty() ? "" : " (motif : " + r->reason + ")") + ".");
        }
        const Seconds t = now();
        Seconds anchor = std::max({st.latestSeen, lic.issuedAt, useReg->issuedAt, useRev ? useRev->issuedAt : Seconds{0}});
        if (lic.expiresAt != 0 && t + opt_.clockTolerance < anchor)
            return refuse(pre + "l'horloge de ce PC (" + isoDate(t) + ") est en arri\xC3\xA8re sur le " + isoDate(anchor) + " : remets-la \xC3\xA0 l'heure.");
        if (lic.expiresAt != 0) {
            const Seconds graceEnd = lic.type == LicenseType::Evaluation ? lic.expiresAt : lic.expiresAt + static_cast<Seconds>(lic.offlineGraceDays) * kDay;
            if (t >= graceEnd) return refuse(pre + "elle a pris fin le " + isoDate(lic.expiresAt) + ".");
        }
        // Pas de retour a une emission plus ancienne de la meme licence.
        const std::string lp = deploy::joinPath(dir, kLicenseFile);
        if (fileExists(lp)) {
            SignedBlock b;
            License cur;
            std::string w;
            if (readStoredBlock(lp, DocKind::License, b, &w) && verifyTrusted(b, &w) && parseLicense(b.document, cur, &w) && cur.licenseId == lic.licenseId) {
                if (b.document == licB->document) {
                    rep.lines.push_back("Licence " + lic.licenseId + " : d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e sur ce PC.");
                } else if (lic.issuedAt < cur.issuedAt) {
                    return refuse(pre + "elle est plus ancienne (\xC3\xA9mise le " + isoDate(lic.issuedAt) + ") que celle install\xC3\xA9" "e sur ce PC (\xC3\xA9mise le "
                                  + isoDate(cur.issuedAt) + ").");
                } else {
                    writeLic = true;
                }
            } else {
                writeLic = true;
            }
        } else {
            writeLic = true;
        }
        if (writeLic) {
            const std::string edLabel = useReg->edition(lic.edition) && !useReg->edition(lic.edition)->label.empty() ? useReg->edition(lic.edition)->label : lic.edition;
            rep.lines.push_back("Licence " + lic.licenseId + " (" + edLabel + ", " + licenseTypeLabel(lic.type)
                                + (lic.expiresAt ? ", jusqu'au " + isoDate(lic.expiresAt) : std::string()) + ") : install\xC3\xA9" "e sur ce PC.");
        }
    }

    // Tout est accepte : on ecrit (chaque fichier remplace en une fois).
    std::error_code ec;
    std::filesystem::create_directories(P(dir), ec);
    if (writeReg && !writeAll(deploy::joinPath(dir, kRegistryFile), writeBlock(*regB), &why)) return refuse("Le registre ne s'\xC3\xA9" "crit pas : " + notWritable(why) + ".");
    if (writeRev && !writeAll(deploy::joinPath(dir, kRevocationsFile), writeBlock(*revB), &why))
        return refuse("La liste de r\xC3\xA9vocation ne s'\xC3\xA9" "crit pas : " + notWritable(why) + ".");
    if (writeLic && !writeAll(deploy::joinPath(dir, kLicenseFile), writeBlock(*licB), &why)) return refuse("La licence ne s'\xC3\xA9" "crit pas : " + notWritable(why) + ".");
    if (writeReg) st.registryMax = std::max(st.registryMax, reg.number);
    if (writeRev) st.revocationsMax = std::max(st.revocationsMax, rev.number);
    if (writeLic) st.issued[lic.licenseId] = std::max(st.issued[lic.licenseId], lic.issuedAt);   // 1.32.0 (M2)
    if (writeReg || writeRev || writeLic) (void)writeState(statePath, st, &why);
    rep.ok = true;
    rep.licenseInstalled = writeLic;
    rep.state = evaluate();
    return rep;
}

// ======================================================================= la demande ==
bool LicenseStore::deviceKey(std::string& hex, std::string* why) {
    auto key = SigningKey::loadOrCreate(deploy::joinPath(opt_.folder, kDeviceKeyFile), deviceKeySpec(), "", why);
    if (!key) return false;
    hex = key->publicKeyHex();
    return true;
}

bool LicenseStore::writeActivationRequest(const std::string& path, const std::string& deviceName, const std::string& licenseId,
                                          const std::string& software, std::string* why) {
    std::string w;
    const bool ok = writeRequestFile(path, deviceName, licenseId, software, &w);
    HistoryEvent e;
    e.at = now();
    e.kind = "demande";
    e.ok = ok;
    std::string what = baseName(path);
    if (!deviceName.empty()) what += ", PC \xC2\xAB " + deviceName + " \xC2\xBB";
    if (!licenseId.empty()) what += ", licence " + licenseId;
    e.text = ok ? "Demande d'activation \xC3\xA9" "crite (" + what + ") : \xC3\xA0 envoyer \xC3\xA0 l'\xC3\xA9" "diteur."
                : "Demande d'activation non \xC3\xA9" "crite (" + what + ") : " + w;
    std::string hw;
    (void)addHistory(e, "", &hw);   // un dossier ou ce compte n'ecrit pas : la demande l'a deja dit
    if (!ok && why) *why = w;
    return ok;
}

bool LicenseStore::writeRequestFile(const std::string& path, const std::string& deviceName, const std::string& licenseId,
                                    const std::string& software, std::string* why) {
    std::string text;
    if (!buildRequest(deviceName, licenseId, software, text, nullptr, why)) return false;
    return writeAll(path, text, why);
}

bool LicenseStore::activationRequestText(const std::string& deviceName, const std::string& licenseId, const std::string& software,
                                         std::string& blockText, ActivationRequest* parsed, std::string* why) {
    return buildRequest(deviceName, licenseId, software, blockText, parsed, why);
}

bool LicenseStore::deviceOpenSeal(std::string_view sealedBase64, std::string& plain, std::string* why) {
    auto key = SigningKey::load(deploy::joinPath(opt_.folder, kDeviceKeyFile), deviceKeySpec(), why);
    if (!key) return false;
    crypto::Bytes box;
    if (!crypto::unbase64(sealedBase64, box)) {
        if (why) *why = "le contenu scell\xC3\xA9 n'est pas du base64";
        return false;
    }
    if (!key->openSeal(box, plain)) {
        if (why) *why = "le contenu scell\xC3\xA9 ne s'ouvre pas avec la cl\xC3\xA9 de ce PC";
        return false;
    }
    return true;
}

bool LicenseStore::buildRequest(const std::string& deviceName, const std::string& licenseId, const std::string& software, std::string& blockText,
                                ActivationRequest* parsed, std::string* why) {
    auto key = SigningKey::loadOrCreate(deploy::joinPath(opt_.folder, kDeviceKeyFile), deviceKeySpec(), "", why);
    if (!key) return false;
    ActivationRequest q;
    q.deviceKey = key->publicKeyHex();
    q.deviceName = deviceName;
    q.licenseId = licenseId;
    q.software = software;
    q.createdAt = now();
    q.nonce = newNonce();
    q.station = stationHash(opt_.machineId, key->publicKey());   // 1.32.0 (H2) : l'Admin le recopie dans la licence
    // Relue avant d'etre signee : ce qui part se relit (un nom trop long, une licence mal ecrite...).
    const std::string text = writeRequest(q);
    ActivationRequest back;
    if (!parseRequest(text, back, why)) return false;
    const SignedBlock b = signBlock(DocKind::Request, text, key->publicKey(), [&](std::string_view m) { return key->sign(m); });
    blockText = writeBlock(b);
    if (parsed) *parsed = back;
    return true;
}

// ================================================================ 1.35.0 : le service ==
bool LicenseStore::deviceSignServiceMessage(std::string_view message, crypto::Signature& sig, crypto::PublicKey& pub, bool* created, std::string* why) {
    static constexpr std::string_view kPrefix = "XPG-SERVICE-1\n";
    if (message.substr(0, kPrefix.size()) != kPrefix) {
        if (why) *why = "la cl\xC3\xA9 de ce PC ne signe que les appels au service des licences";
        return false;
    }
    auto key = SigningKey::loadOrCreate(deploy::joinPath(opt_.folder, kDeviceKeyFile), deviceKeySpec(), "", why);
    if (!key) return false;
    sig = key->sign(message);
    pub = key->publicKey();
    if (created) *created = key->created();
    return true;
}

ImportReport LicenseStore::installActivation(std::string_view licenceText, std::string_view activationText, std::string_view certText) {
    deferState_ = true;
    ImportReport r;
    bool quiet = false;
    if (!configured()) {
        r.why = kNotConfiguredImport;
        r.state = evaluate();
    } else {
        r = activationChecked(licenceText, activationText, certText, quiet);
    }
    deferState_ = false;
    return finishImport(std::move(r), /*logImport=*/!quiet);
}

ImportReport LicenseStore::activationChecked(std::string_view licenceText, std::string_view activationText, std::string_view certText, bool& quiet) {
    ImportReport rep;
    const auto refuse = [&](const std::string& text) {
        rep.ok = false;
        rep.why = text;
        rep.state = evaluate();
        return rep;
    };
    std::string why;
    SignedBlock lb;
    if (!service::findBlock(licenceText, DocKind::License, lb, &why))
        return refuse("L'activation est refus\xC3\xA9" "e : la licence re\xC3\xA7ue est illisible (" + why + ").");
    if (!verifyTrusted(lb, &why)) return refuse("L'activation est refus\xC3\xA9" "e : " + why + ".");
    License lic;
    if (!parseLicense(lb.document, lic, &why)) return refuse("L'activation est refus\xC3\xA9" "e : " + why + ".");
    const std::string pre = "L'activation de la licence " + lic.licenseId + " est refus\xC3\xA9" "e : ";
    if (lic.service.empty()) return refuse(pre + "ce n'est pas une licence en ligne (elle s'importe en fichier, menu Licence\xE2\x80\xA6).");

    // Ce que ce PC a deja (comme l'import d'un fichier).
    const std::string dir = opt_.folder;
    const std::string statePath = deploy::joinPath(dir, kStateFile);
    const std::string lp = deploy::joinPath(dir, kLicenseFile);
    const std::string ap = deploy::joinPath(dir, kActivationFile);
    State st = readState(statePath, nullptr);
    if (!st.found && fileExists(lp)) {
        (void)evaluate();   // 1.32.0 (M1) : l'etat protege recree (son ancre) avant d'en ecrire un neuf
        st = readState(statePath, nullptr);
    }
    Registry reg;
    bool hasReg = false;
    {
        SignedBlock b;
        std::string w;
        const std::string rp = deploy::joinPath(dir, kRegistryFile);
        hasReg = fileExists(rp) && readStoredBlock(rp, DocKind::Registry, b, &w) && verifyTrusted(b, &w) && parseRegistry(b.document, reg, &w)
                 && reg.number >= st.registryMax;
        if (!hasReg && !opt_.bundledRegistry.empty() && fileExists(opt_.bundledRegistry)) {
            hasReg = readStoredBlock(opt_.bundledRegistry, DocKind::Registry, b, &w) && verifyTrusted(b, &w) && parseRegistry(b.document, reg, &w)
                     && reg.number >= st.registryMax;
        }
    }
    RevocationList rev;
    bool hasRev = false;
    {
        SignedBlock b;
        std::string w;
        const std::string vp = deploy::joinPath(dir, kRevocationsFile);
        hasRev = fileExists(vp) && readStoredBlock(vp, DocKind::Revocations, b, &w) && verifyTrusted(b, &w) && parseRevocations(b.document, rev, &w)
                 && rev.number >= st.revocationsMax;
    }
    if (!hasReg) return refuse(pre + "le registre des \xC3\xA9" "ditions manque sur ce PC (mets le programme \xC3\xA0 jour, ou importe le registre re\xC3\xA7u de l'\xC3\xA9" "diteur).");
    if (!checkAgainstRegistry(lic, reg, &why)) return refuse(pre + why + ".");
    const std::string kp = deploy::joinPath(dir, kDeviceKeyFile);
    if (!fileExists(kp)) return refuse(pre + "ce PC n'a pas encore de cl\xC3\xA9 d'identit\xC3\xA9 (elle est cr\xC3\xA9\xC3\xA9" "e avec le premier appel au service).");
    auto key = SigningKey::load(kp, deviceKeySpec(), &why);
    if (!key) return refuse(pre + why + ".");
    const std::string keyHex = key->publicKeyHex();
    const std::string station = stationHash(opt_.machineId, key->publicKey());

    // L'activation : pour cette licence, cette emission, ce PC ; le certificat pas expire et le bail pas fini a l'heure retenue
    // (revue 1 : au moins l'emission de cette activation et la derniere heure signee gardee).
    const Seconds t = now();
    const CounterRead clock = readCounter(t, Counter{st.serviceTime, st.serviceElapsed, st.serviceWall});
    const std::vector<crypto::PublicKey> keys = publisherKeys();
    ActivationCheck c = checkActivation(activationText, certText, keys, serviceUrl_, lic, keyHex, station);
    if (c.fault != ActivationFault::None) return refuse(pre + c.problem + ".");
    const service::Activation& a = c.activation;
    const Seconds tn = std::max({clock.trustedNow, clock.counter.serviceTime, a.issuedAt});
    if (tn > c.cert.expiresAt) return refuse(pre + certExpiredText(c.cert.expiresAt) + ".");
    if (a.until <= tn)
        return refuse(pre + "son bail est d\xC3\xA9j\xC3\xA0 fini (le " + frenchDate(a.until) + ") : connecte ce PC \xC3\xA0 Internet pour le renouveler.");
    // Les regles de toute licence : jamais une emission plus ancienne, pas revoquee, pas finie.
    if (const auto it = st.issued.find(lic.licenseId); it != st.issued.end() && lic.issuedAt < it->second)
        return refuse(pre + "elle est plus ancienne (\xC3\xA9mise le " + isoDate(lic.issuedAt) + ") qu'une \xC3\xA9mission d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e sur ce PC (\xC3\xA9mise le "
                      + isoDate(it->second) + ").");
    if (hasRev) {
        if (const Revocation* r = rev.find(lic.licenseId, keyHex, lic.issuedAt))
            return refuse(pre + "elle a \xC3\xA9t\xC3\xA9 r\xC3\xA9voqu\xC3\xA9" "e par l'\xC3\xA9" "diteur le " + isoDate(r->revokedAt) + (r->reason.empty() ? "" : " (motif : " + r->reason + ")") + ".");
    }
    // revue 10 : la fin d'une licence en ligne se compare a l'heure retenue (l'horloge reculee : l'evaluation le dit, et
    // l'heure signee de la connexion suivante la recale - pas l'heure la plus tardive de toujours).
    if (lic.expiresAt != 0) {
        const Seconds graceEnd = lic.type == LicenseType::Evaluation ? lic.expiresAt : lic.expiresAt + static_cast<Seconds>(lic.offlineGraceDays) * kDay;
        if (tn >= graceEnd) return refuse(pre + "elle a pris fin le " + isoDate(lic.expiresAt) + ".");
    }

    // Un renouvellement (la meme activation : licence, emission, numero) ? Une activation deja gardee qui va plus loin reste.
    bool renewal = false, keepOld = false;
    Seconds until = a.until;
    if (fileExists(ap)) {
        std::string text, w;
        if (readAll(ap, text, &w)) {
            const ActivationCheck old = checkActivation(text, text, keys, serviceUrl_, lic, keyHex, station);
            if (old.fault == ActivationFault::None && old.activation.number == a.number) {
                renewal = true;
                keepOld = old.activation.until > a.until;
                if (keepOld) until = old.activation.until;
            }
        }
    }
    // La licence deja installee (une autre : son jeton part avec elle).
    std::string currentId;
    bool sameLicense = false;
    const std::string licBytes = writeBlock(lb);
    if (fileExists(lp)) {
        std::string cur, w;
        if (readAll(lp, cur, &w)) {
            sameLicense = cur == licBytes;
            License l;
            std::vector<SignedBlock> blocks;
            if (readBlocks(cur, blocks, &w) && blocks.size() == 1 && blocks[0].kind == DocKind::License && verifyTrusted(blocks[0], &w)
                && parseLicense(blocks[0].document, l, &w))
                currentId = l.licenseId;
        }
    }

    // Tout est verifie : on ecrit (l'activation d'abord, puis la licence).
    std::error_code ec;
    std::filesystem::create_directories(P(dir), ec);
    if (!keepOld && !writeAll(ap, writeBlock(c.activationBlock) + writeBlock(c.certBlock), &why))
        return refuse("L'activation ne s'\xC3\xA9" "crit pas : " + notWritable(why) + ".");
    if (!sameLicense && !writeAll(lp, licBytes, &why)) return refuse("La licence ne s'\xC3\xA9" "crit pas : " + notWritable(why) + ".");
    if (currentId != lic.licenseId) std::filesystem::remove(P(deploy::joinPath(dir, kLeaseFile)), ec);
    st.issued[lic.licenseId] = std::max(st.issued[lic.licenseId], lic.issuedAt);   // 1.32.0 (M2)
    (void)writeState(statePath, st, &why);
    const std::string edLabel = reg.edition(lic.edition) && !reg.edition(lic.edition)->label.empty() ? reg.edition(lic.edition)->label : lic.edition;
    if (renewal)
        rep.lines.push_back("Licence " + lic.licenseId + " : activation renouvel\xC3\xA9" "e jusqu'au " + frenchDate(until) + ".");
    else
        rep.lines.push_back("Licence " + lic.licenseId + " (" + edLabel + ", " + licenseTypeLabel(lic.type) + (lic.expiresAt ? ", jusqu'au " + isoDate(lic.expiresAt) : std::string())
                            + ") : activ\xC3\xA9" "e sur ce PC en ligne, bail jusqu'au " + frenchDate(until) + " (renouvel\xC3\xA9 \xC3\xA0 chaque connexion).");
    quiet = renewal && sameLicense;
    rep.ok = true;
    rep.licenseInstalled = !sameLicense;
    rep.state = evaluate();
    return rep;
}

bool LicenseStore::dropActivation(std::string_view licenseId, std::string* why) {
    const std::string dir = opt_.folder;
    const std::string ap = deploy::joinPath(dir, kActivationFile), jp = deploy::joinPath(dir, kLeaseFile), lp = deploy::joinPath(dir, kLicenseFile);
    std::vector<std::string> remove;
    std::string w;
    // l'activation et le jeton : relus sans verifier la signature (ils ne peuvent que partir)
    if (fileExists(ap)) {
        std::string text;
        SignedBlock b;
        service::Activation a;
        if (readAll(ap, text, &w) && service::findBlock(text, DocKind::Activation, b, &w) && service::parseActivation(b.document, a, &w) && a.licenseId == licenseId)
            remove.push_back(ap);
    }
    if (fileExists(jp)) {
        std::string text;
        SignedBlock b;
        service::Lease l;
        if (readAll(jp, text, &w) && service::findBlock(text, DocKind::Lease, b, &w) && service::parseLease(b.document, l, &w) && l.licenseId == licenseId)
            remove.push_back(jp);
    }
    bool licenseGone = false;
    if (fileExists(lp)) {
        SignedBlock b;
        License l;
        if (readStoredBlock(lp, DocKind::License, b, &w) && verifyTrusted(b, &w) && parseLicense(b.document, l, &w) && l.licenseId == licenseId && !l.service.empty()) {
            remove.push_back(lp);
            licenseGone = true;
        }
    }
    if (remove.empty()) return true;   // rien pour cette licence sur ce PC
    std::string failed;
    for (const auto& f : remove) {
        std::error_code ec;
        std::filesystem::remove(P(f), ec);
        if (ec) failed += (failed.empty() ? "" : ", ") + baseName(f) + " (" + ec.message() + ")";
    }
    HistoryEvent e;
    e.at = now();
    e.kind = "service";
    e.ok = failed.empty();
    e.text = "Licence " + std::string(licenseId) + " : son activation sur ce PC est retir\xC3\xA9" "e (le service des licences ne la renouvelle plus : poste lib\xC3\xA9r\xC3\xA9, retir\xC3\xA9 ou licence r\xC3\xA9voqu\xC3\xA9" "e)"
             + std::string(licenseGone ? " ; la licence en ligne est effac\xC3\xA9" "e de ce PC." : ".")
             + (failed.empty() ? std::string() : " Ne s'efface pas : " + failed + ".");
    std::string hw;
    (void)addHistory(e, "", &hw);
    if (!failed.empty()) {
        if (why) *why = notWritable("ne s'efface pas : " + failed);
        return false;
    }
    return true;
}

bool LicenseStore::noteServiceTime(std::string_view timeText, std::string_view expectedNonce, std::string_view certText, std::string* why) {
    const auto fail = [&](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    if (!configured()) return fail("ce programme ne peut pas v\xC3\xA9rifier l'heure du service : il ne contient pas la cl\xC3\xA9 de l'\xC3\xA9" "diteur");
    if (expectedNonce.size() != 32) return fail("le nonce de la demande d'heure est mal \xC3\xA9" "crit");
    std::string w;
    service::ServiceCert cert;
    if (!service::verifyCert(certText, publisherKeys(), 0, cert, &w)) return fail(w);
    if (!certForService(cert, serviceUrl_, &w)) return fail(w);   // revue 6
    SignedBlock tb;
    if (!service::findBlock(timeText, DocKind::Time, tb, &w)) return fail("l'heure du service est illisible (" + w + ")");
    if (!service::verifyServiceBlock(tb, DocKind::Time, cert, service::kRightTime, &w)) return fail(w);
    service::SignedTime st;
    if (!service::parseTime(tb.document, st, &w)) return fail(w);
    if (st.nonce != expectedNonce) return fail("l'heure re\xC3\xA7ue ne r\xC3\xA9pond pas \xC3\xA0 cette demande (le nonce ne correspond pas)");
    // le certificat valait a l'heure signee (pas a l'heure retenue : une horloge avancee par erreur se recale)
    if (st.time > cert.expiresAt) return fail("le certificat du service a expir\xC3\xA9 le " + frenchDate(cert.expiresAt));
    if (st.time < cert.issuedAt) return fail("l'heure re\xC3\xA7ue est ant\xC3\xA9rieure au certificat du service");
    const Seconds t = now();
    const std::string statePath = deploy::joinPath(opt_.folder, kStateFile);
    State state = readState(statePath, nullptr);
    const CounterRead clock = readCounter(t, Counter{state.serviceTime, state.serviceElapsed, state.serviceWall});
    if (st.time <= clock.counter.serviceTime) return true;   // pas plus recente que celle gardee : ignoree
    // Une heure signee plus recente : le compteur repart d'elle (le temps ecoule a zero, l'horloge du PC de maintenant).
    memoServiceTime_ = st.time;
    memoElapsed_ = 0;
    memoWall_ = t;
    memoSteady_ = clock.steady;
    memoSet_ = true;
    const ClockCopy c{st.time, 0, t};
    std::error_code ec;
    std::filesystem::create_directories(P(opt_.folder), ec);
    // etat.bin seulement s'il existe : un etat absent est recree par l'evaluation (son ancre reprise des traces)
    if (state.found) {
        state.serviceTime = c.serviceTime;
        state.serviceElapsed = 0;
        state.serviceWall = t;
        (void)writeState(statePath, state, &w);
    }
    (void)writeClockFile(deploy::joinPath(opt_.folder, kClockFile), c, &w);
#ifdef _WIN32
    if (systemEvaluationCopy()) {
        crypto::Bytes sealed;
        if (sealClock(c, sealed, &w)) (void)writeSystemValue(kClockRegValue, sealed, &w);
    }
#endif
    return true;
}

bool LicenseStore::installLease(std::string_view leaseText, std::string_view certText, std::string* why) {
    const auto fail = [&](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    if (!configured()) return fail("ce programme ne peut pas v\xC3\xA9rifier un jeton : il ne contient pas la cl\xC3\xA9 de l'\xC3\xA9" "diteur");
    const std::string dir = opt_.folder;
    std::string w;
    SignedBlock b;
    License lic;
    const std::string lp = deploy::joinPath(dir, kLicenseFile);
    if (!fileExists(lp) || !readStoredBlock(lp, DocKind::License, b, &w) || !verifyTrusted(b, &w) || !parseLicense(b.document, lic, &w))
        return fail("ce PC n'a pas de licence flottante install\xC3\xA9" "e");
    if (lic.service.empty() || lic.type != LicenseType::Floating) return fail("la licence de ce PC (" + lic.licenseId + ") n'est pas une licence flottante");
    auto key = SigningKey::load(deploy::joinPath(dir, kDeviceKeyFile), deviceKeySpec(), &w);
    if (!key) return fail("ce PC n'a pas encore de cl\xC3\xA9 d'identit\xC3\xA9 (" + w + ")");
    const Seconds t = now();
    const State state = readState(deploy::joinPath(dir, kStateFile), nullptr);
    const CounterRead clock = readCounter(t, Counter{state.serviceTime, state.serviceElapsed, state.serviceWall});
    service::Lease lease;
    SignedBlock leaseBlock, certBlock;
    if (!checkLease(leaseText, certText, publisherKeys(), serviceUrl_, clock.trustedNow, lic.licenseId, key->publicKeyHex(), lease, &leaseBlock, &certBlock, nullptr, &w))
        return fail(w);
    if (lease.until <= std::max(clock.serviceNow, lease.issuedAt)) return fail("ce jeton a d\xC3\xA9j\xC3\xA0 pris fin (" + isoTime(lease.until) + ")");
    std::error_code ec;
    std::filesystem::create_directories(P(dir), ec);
    if (!writeAll(deploy::joinPath(dir, kLeaseFile), writeBlock(leaseBlock) + writeBlock(certBlock), &w)) return fail(notWritable(w));
    return true;
}

void LicenseStore::dropLease() {
    std::error_code ec;
    std::filesystem::remove(P(deploy::joinPath(opt_.folder, kLeaseFile)), ec);
}

bool LicenseStore::installTrialRecord(std::string_view trialText, std::string_view certText, std::string* why) {
    const auto fail = [&](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    if (!configured()) return fail("ce programme ne peut pas v\xC3\xA9rifier la D\xC3\xA9" "couverte : il ne contient pas la cl\xC3\xA9 de l'\xC3\xA9" "diteur");
    const std::string dir = opt_.folder;
    std::string w;
    auto key = SigningKey::load(deploy::joinPath(dir, kDeviceKeyFile), deviceKeySpec(), &w);
    if (!key) return fail("ce PC n'a pas encore de cl\xC3\xA9 d'identit\xC3\xA9 (" + w + ")");
    const std::string keyHex = key->publicKeyHex();
    const Seconds t = now();
    const State state = readState(deploy::joinPath(dir, kStateFile), nullptr);
    const CounterRead clock = readCounter(t, Counter{state.serviceTime, state.serviceElapsed, state.serviceWall});
    const std::vector<crypto::PublicKey> keys = publisherKeys();
    const std::string machine = thisMachineHash();
    service::Trial trial;
    SignedBlock trialBlock, certBlock;
    if (!checkTrial(trialText, certText, keys, serviceUrl_, clock.trustedNow, machine, keyHex, trial, &trialBlock, &certBlock, &w)) return fail(w);
    // Un debut plus ancien deja garde reste (le plus ancien gagne).
    const std::string tp = deploy::joinPath(dir, kTrialFile);
    if (fileExists(tp)) {
        std::string text;
        service::Trial old;
        if (readAll(tp, text, &w) && checkTrial(text, text, keys, serviceUrl_, 0, machine, keyHex, old, nullptr, nullptr, &w) && old.start <= trial.start) return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(P(dir), ec);
    if (!writeAll(tp, writeBlock(trialBlock) + writeBlock(certBlock), &w)) return fail(notWritable(w));
    return true;
}

std::string LicenseStore::thisMachineHash() const { return service::machineHash(opt_.machineId); }

std::string LicenseStore::thisStationHash(bool* created) {
    if (created) *created = false;
    std::string why;
    auto key = SigningKey::loadOrCreate(deploy::joinPath(opt_.folder, kDeviceKeyFile), deviceKeySpec(), "", &why);
    if (!key) return {};
    if (created) *created = key->created();
    return stationHash(opt_.machineId, key->publicKey());
}

// ---- 1.35.0 (revue 9) : qui a ecrit code-activation.txt ? ----
FileOwner fileOwnerOf(const std::string& path) {
#ifdef _WIN32
    std::wstring wide = P(path).wstring();
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(wide.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &sd) != ERROR_SUCCESS
        || !owner) {
        if (sd) LocalFree(sd);
        return FileOwner::Unknown;   // illisible (Wine, un volume sans droits) : comme ce compte
    }
    FileOwner r = FileOwner::Other;
    for (const WELL_KNOWN_SID_TYPE type : {WinBuiltinAdministratorsSid, WinLocalSystemSid}) {
        BYTE sid[SECURITY_MAX_SID_SIZE];
        DWORD size = sizeof sid;
        if (CreateWellKnownSid(type, nullptr, sid, &size) && EqualSid(owner, sid)) r = FileOwner::Admin;
    }
    if (r == FileOwner::Other) {
        HANDLE token = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            DWORD size = 0;
            (void)GetTokenInformation(token, TokenUser, nullptr, 0, &size);
            std::vector<BYTE> info(size > 0 ? size : 1);
            if (size > 0 && GetTokenInformation(token, TokenUser, info.data(), size, &size)
                && EqualSid(owner, reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid))
                r = FileOwner::CurrentUser;
            CloseHandle(token);
        } else {
            r = FileOwner::Unknown;
        }
    }
    LocalFree(sd);
    return r;
#else
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) return FileOwner::Unknown;
    if (st.st_uid == 0) return FileOwner::Admin;   // root : l'installation pour tout le systeme
    if (st.st_uid == ::geteuid()) return FileOwner::CurrentUser;
    return FileOwner::Other;
#endif
}

FileOwner LicenseStore::ownerOf(const std::string& path) const { return opt_.fileOwner ? opt_.fileOwner(path) : fileOwnerOf(path); }

// Une licence valable est-elle installee sur ce PC (la derniere evaluation) ? Une licence en ligne qui attend sa
// connexion (Bail expire, Flottante : aucun poste) ou l'horloge (Horloge reculee) compte : elle est installee. Pas encore
// evalue : un fichier de licence suffit (prudent).
bool LicenseStore::validLicenseInstalled() const {
    if (!lastEvaluated_) return fileExists(deploy::joinPath(opt_.folder, kLicenseFile));
    if (!license_) return false;
    switch (*lastEvaluated_) {
        case LicenseStatus::Valid:
        case LicenseStatus::Grace:
        case LicenseStatus::ClockRolledBack:
        case LicenseStatus::LeaseExpired:
        case LicenseStatus::NoFloatingSeat: return true;
        default: return false;
    }
}

std::string LicenseStore::pendingActivationCode() const {
    const std::string p = deploy::joinPath(opt_.folder, kPendingCodeFile);
    if (!fileExists(p)) return {};
    // revue 9 : n'importe quel compte peut creer un fichier dans ce dossier. Le code n'est lu que s'il vient de
    // l'installateur (les administrateurs, SYSTEM ; ailleurs root), ou de CE compte quand aucune licence valable n'est
    // installee (une installation pour ce seul compte, ou le code pose a la main). Sinon : ignore, laisse tel quel.
    const FileOwner owner = ownerOf(p);
    std::string ignored;
    if (owner == FileOwner::Other)
        ignored = "il a \xC3\xA9t\xC3\xA9 \xC3\xA9" "crit par un autre compte de ce PC, pas par l'installateur (les administrateurs)";
    else if (owner != FileOwner::Admin && validLicenseInstalled())
        ignored = "une licence est d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e sur ce PC, et ce fichier n'a pas \xC3\xA9t\xC3\xA9 \xC3\xA9" "crit par l'installateur (les administrateurs)";
    if (!ignored.empty()) {
        if (!pendingIgnoredNoted_) {
            // une ligne de l'historique (jamais le code), une fois : pas une de plus a chaque connexion
            pendingIgnoredNoted_ = true;
            const std::string hp = deploy::joinPath(opt_.folder, kHistoryFile);
            const std::string text = "Le code d'activation laiss\xC3\xA9 dans " + std::string(kPendingCodeFile) + " est ignor\xC3\xA9 : " + ignored
                                     + ". Le fichier est laiss\xC3\xA9 tel quel ; pour activer une licence, entre ton code (menu Licence\xE2\x80\xA6).";
            const auto lines = readHistoryLines(hp, nullptr);
            if (lines.empty() || lines.back().str("text") != text) {
                core::json::Value v = core::json::Value::object();
                v.set("at", isoTime(now()));
                v.set("kind", std::string("controle"));
                v.set("ok", false);
                v.set("text", text);
                std::string w;
                (void)appendHistoryLine(hp, v, &w);   // un dossier ou ce compte n'ecrit pas : rien de plus a dire
            }
        }
        return {};
    }
    crypto::Bytes raw;
    std::string w;
    if (!crypto::readFile(p, raw, &w, 4096)) return {};
    std::string text;
    if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        // UTF-16 (petit-boutiste) : un code n'a que des caracteres ASCII
        for (std::size_t i = 2; i + 1 < raw.size(); i += 2)
            if (raw[i + 1] == 0) text.push_back(static_cast<char>(raw[i]));
    } else {
        text.assign(raw.begin(), raw.end());
        if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);   // UTF-8 avec BOM
    }
    crypto::wipe(raw.data(), raw.size());
    std::size_t pos = 0;
    std::string code;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
        if (line.size() <= 64) code = line;
        crypto::wipe(line.data(), line.size());
        break;
    }
    crypto::wipe(text.data(), text.size());
    return code;
}

bool LicenseStore::clearPendingActivationCode(std::string* why) {
    const std::string p = deploy::joinPath(opt_.folder, kPendingCodeFile);
    std::error_code ec;
    if (!std::filesystem::exists(P(p), ec)) return true;
    // revue 9 : jamais le fichier d'un autre compte (il n'a pas ete lu : pendingActivationCode l'a ignore)
    if (ownerOf(p) == FileOwner::Other) return true;
    std::filesystem::remove(P(p), ec);
    if (ec) {
        if (why) *why = notWritable(ec.message());
        return false;
    }
    return true;
}

} // namespace licensing
