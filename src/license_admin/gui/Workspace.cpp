// =============================================================================
//  license_admin/gui/Workspace.cpp - 1.33.0 : le poste Admin vu par l'application
//  (voir Workspace.hpp). Les memes fonctions que la console : un fichier signe ici
//  est celui que XPG-License-Admin aurait ecrit.
// =============================================================================
#include "Workspace.hpp"

#include "../../core/Json.hpp"
#include "../../deploy/Crypto.hpp"
#include "../../deploy/Paths.hpp"
#include "../../licensing/Service.hpp"
#include "../../licensing/SignedFile.hpp"
#include "../../licensing/Time.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>

namespace license_admin::gui {

namespace {

namespace fs = std::filesystem;
namespace crypto = deploy::crypto;
using licensing::DocKind;
using licensing::SignedBlock;

constexpr Seconds kDay = 86400;
constexpr const char* kRegistryRel = "registre/registre.xpgreg";
constexpr const char* kRevocationsRel = "revocations/revocations.xpgrev";
constexpr const char* kLicencesDir = "licences";

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

bool readText(const std::string& path, std::string& out, std::string* why) {
    crypto::Bytes raw;
    if (!crypto::readFile(path, raw, why, 16u * 1024u * 1024u)) return false;
    out.assign(raw.begin(), raw.end());
    return true;
}

bool writeText(const std::string& path, const std::string& text, std::string* why) {
    const crypto::Bytes bytes(text.begin(), text.end());
    return crypto::writePrivateFile(path, bytes, why);
}

// Le premier bloc d'un genre dans un fichier signe (sans verifier : l'archive ne range que ce qui est verifie).
bool blockOf(std::string_view bytes, DocKind kind, SignedBlock& out, std::string* why) {
    std::vector<SignedBlock> blocks;
    if (!licensing::readBlocks(bytes, blocks, why)) return false;
    for (auto& b : blocks)
        if (b.kind == kind) {
            out = std::move(b);
            return true;
        }
    if (why) *why = "pas de bloc " + licensing::docKindLabel(kind) + " dans ce fichier";
    return false;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// Une revocation touche-t-elle cette emission (signee a `issuedAt`) ?
bool hits(const licensing::Revocation& r, Seconds issuedAt) { return r.issuedBefore == 0 || issuedAt < r.issuedBefore; }

} // namespace

// ---------------------------------------------------------------------------------------------------- OpResult
OpResult OpResult::fail(std::string why) {
    OpResult r;
    r.ok = false;
    r.message = "Refus\xC3\xA9 : " + std::move(why);
    return r;
}

OpResult OpResult::done(std::string message) {
    OpResult r;
    r.ok = true;
    r.message = std::move(message);
    return r;
}

std::string OpResult::text() const {
    std::string t = message;
    for (const auto& d : details) t += "\n" + d;
    return t;
}

std::string LicenceEntry::stateLabel() const {
    if (revoked) return "r\xC3\xA9voqu\xC3\xA9" "e";
    if (expired) return "expir\xC3\xA9" "e";
    if (revokedDevices > 0) return "active (" + plural(revokedDevices, "PC r\xC3\xA9voqu\xC3\xA9", "PC r\xC3\xA9voqu\xC3\xA9s") + ")";
    return "active";
}

std::string dateFr(Seconds t) {
    if (t <= 0) return {};
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char b[32];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    return b;
}

std::string dateTimeFr(Seconds t) {
    if (t <= 0) return {};
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char b[40];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    return b;
}

// --------------------------------------------------------------------------------------------------- Workspace
Workspace::Workspace(std::string folder) : folder_(folder.empty() ? defaultKeyFolder() : std::move(folder)) {
    // La marque de l'outil Admin dans l'executable : le controle du paquet la cherche (jamais livre a un client).
    static const char* volatile mark = kAdminMarker;
    (void)mark;
    (void)crypto::init();
    reload();
}

Seconds Workspace::now() const { return clock_ ? clock_() : licensing::systemNow(); }

void Workspace::reload() {
    loadKey();
    loadArchive();
    loadActivations();
    loadService();
}

void Workspace::loadKey() {
    key_ = KeyStatus{};
    signing_.reset();
    key_.protectedHere = crypto::protectedHere();
    key_.path = publisherKeyPath(folder_);
    std::error_code ec;
    if (!fs::exists(P(key_.path), ec)) {
        key_.problem = "pas encore de cl\xC3\xA9 de l'\xC3\xA9" "diteur sur ce poste";
        return;
    }
    std::string why;
    signing_ = loadPublisherKey(folder_, &why);
    if (!signing_) {
        key_.problem = why;
        return;
    }
    key_.present = true;
    key_.test = signing_->test();
    key_.id = signing_->keyId();
    key_.name = signing_->name();
    key_.fingerprint = signing_->fingerprint(0);
}

std::string Workspace::archivedText(const std::string& relative) const {
    std::string text;
    const std::string path = U(P(archive::archiveFolder(folder_)) / P(relative));
    std::error_code ec;
    if (!fs::is_regular_file(P(path), ec)) return {};
    std::string why;
    if (!readText(path, text, &why)) return {};
    return text;
}

void Workspace::loadArchive() {
    hasRegistry_ = hasRevocations_ = false;
    registry_ = licensing::Registry{};
    revocations_ = licensing::RevocationList{};
    licences_.clear();
    archiveProblems_.clear();
    std::string why;
    if (const std::string text = archivedText(kRegistryRel); !text.empty()) {
        SignedBlock b;
        if (blockOf(text, DocKind::Registry, b, &why) && licensing::parseRegistry(b.document, registry_, &why))
            hasRegistry_ = true;
        else
            archiveProblems_.push_back("registre : " + why);
    }
    if (const std::string text = archivedText(kRevocationsRel); !text.empty()) {
        SignedBlock b;
        if (blockOf(text, DocKind::Revocations, b, &why) && licensing::parseRevocations(b.document, revocations_, &why))
            hasRevocations_ = true;
        else
            archiveProblems_.push_back("liste de r\xC3\xA9vocation : " + why);
    }
    const fs::path dir = P(archive::archiveFolder(folder_)) / kLicencesDir;
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec) || e.path().extension() != ".xpglic") continue;
            LicenceEntry entry;
            entry.path = U(e.path());
            std::string text;
            SignedBlock b;
            if (!readText(entry.path, text, &why) || !blockOf(text, DocKind::License, b, &why) || !licensing::parseLicense(b.document, entry.licence, &why)) {
                archiveProblems_.push_back(U(e.path().filename()) + " : " + why);
                continue;
            }
            licences_.push_back(std::move(entry));
        }
    }
    const Seconds t = now();
    for (auto& l : licences_) {
        l.expired = l.licence.expiresAt != 0 && t >= l.licence.expiresAt;
        for (const auto& r : revocations_.revoked) {
            if (r.licenseId != l.licence.licenseId || !hits(r, l.licence.issuedAt)) continue;
            if (r.device.empty()) {
                l.revoked = true;
            } else {
                for (const auto& d : l.licence.devices)
                    if (d.key == r.device) {
                        ++l.revokedDevices;
                        break;
                    }
            }
        }
    }
    std::sort(licences_.begin(), licences_.end(), [](const LicenceEntry& a, const LicenceEntry& b) {
        if (a.licence.issuedAt != b.licence.issuedAt) return a.licence.issuedAt > b.licence.issuedAt;
        return a.licence.licenseId < b.licence.licenseId;
    });
}

void Workspace::loadActivations() {
    records_.clear();
    skipped_ = 0;
    std::string why;
    if (!readActivations(folder_, records_, &skipped_, &why)) archiveProblems_.push_back("registre des activations : " + why);
}

void Workspace::loadService() {
    const bool hadState = service_.hasState;
    const online::ServiceState state = service_.state;
    const Seconds stateAt = service_.stateAt;
    service_ = ServiceStatus{};
    service_.configured = online::configured(folder_);
    if (service_.configured) {
        std::string why;
        if (!online::readConfig(folder_, service_.config, &why)) service_.problem = why;
    }
    service_.syncKey = archive::hasSyncKey(folder_);
    if (service_.syncKey) {
        archive::SyncKey k{};
        std::string why;
        if (archive::loadSyncKey(folder_, k, &why)) service_.syncKeyId = archive::syncKeyId(k);
        else if (service_.problem.empty()) service_.problem = why;
        crypto::wipe(k);
    }
    service_.lastSync = archive::lastSync(folder_);
    service_.fetch = online::readFetchState(folder_);
    service_.requests = online::listRequests(folder_);
    for (const auto& e : service_.requests) service_.pending += e.waiting() ? 1 : 0;
    service_.snapshot = online::readSnapshot(folder_);
    // Le releve d'un AUTRE service (l'adresse a change) ne compte plus.
    if (!service_.snapshot.url.empty() && service_.snapshot.url != service_.config.url) service_.snapshot = online::Snapshot{};
    // L'etat lu dans cette seance reste (il ne se relit que par Verifier), s'il est du meme service.
    if (hadState && service_.ready()) {
        service_.hasState = true;
        service_.state = state;
        service_.stateAt = stateAt;
    }
}

const LicenceEntry* Workspace::licence(const std::string& licenseId) const {
    for (const auto& l : licences_)
        if (l.licence.licenseId == licenseId) return &l;
    return nullptr;
}

std::vector<KnownDevice> Workspace::devices() const { return knownDevices(records_, now()); }

std::vector<ClientEntry> Workspace::clients() const {
    std::map<std::string, ClientEntry> byKey;   // le nom sans casse
    std::map<std::string, std::string> customerOf;   // licence -> cle du client
    for (const auto& l : licences_) {
        const std::string k = lower(l.licence.customer);
        auto& c = byKey[k];
        if (c.name.empty()) c.name = l.licence.customer;
        c.licenceIds.push_back(l.licence.licenseId);
        if (!l.revoked && !l.expired) ++c.activeLicences;
        c.devices += static_cast<int>(l.licence.devices.size());
        c.lastActivity = std::max(c.lastActivity, l.licence.issuedAt);
        customerOf[l.licence.licenseId] = k;
    }
    for (const auto& r : records_) {
        std::string k;
        if (!r.customer.empty()) k = lower(r.customer);
        else if (auto it = customerOf.find(r.licenseId); it != customerOf.end()) k = it->second;
        if (k.empty()) continue;
        auto& c = byKey[k];
        if (c.name.empty()) c.name = r.customer;
        c.lastActivity = std::max(c.lastActivity, r.at);
        if (std::find(c.licenceIds.begin(), c.licenceIds.end(), r.licenseId) == c.licenceIds.end() && !r.licenseId.empty())
            c.licenceIds.push_back(r.licenseId);
    }
    for (const auto& d : devices()) {
        if (d.state != "demande") continue;
        std::string k = !d.customer.empty() ? lower(d.customer) : std::string();
        if (k.empty())
            if (auto it = customerOf.find(d.licenseId); it != customerOf.end()) k = it->second;
        if (k.empty()) continue;
        ++byKey[k].pendingRequests;
    }
    std::vector<ClientEntry> out;
    for (auto& [k, c] : byKey) {
        (void)k;
        if (c.name.empty()) continue;
        out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(), [](const ClientEntry& a, const ClientEntry& b) { return lower(a.name) < lower(b.name); });
    return out;
}

Dashboard Workspace::dashboard() const {
    Dashboard d;
    const Seconds t = now();
    std::map<std::string, int> editions;
    for (const auto& l : licences_) {
        ++d.licences;
        if (l.revoked) ++d.revokedLicences;
        else if (l.expired) ++d.expiredLicences;
        else {
            ++d.activeLicences;
            ++editions[l.licence.edition];
            if (l.licence.expiresAt != 0 && l.licence.expiresAt - t <= 30 * kDay) ++d.expiringSoon;
        }
    }
    for (const auto& e : registry_.editions)
        if (editions.count(e.id)) d.byEdition.emplace_back(e.label.empty() ? e.id : e.label, editions[e.id]);
    for (const auto& [id, n] : editions)
        if (!registry_.edition(id)) d.byEdition.emplace_back(id, n);
    d.clients = static_cast<int>(clients().size());
    for (const auto& dev : devices()) {
        if (dev.state == "active") ++d.devices;
        else if (dev.state == "demande") ++d.pendingRequests;
    }
    d.serviceConfigured = service_.ready();
    d.onlineLicences = static_cast<int>(service_.snapshot.licences.size());
    d.onlineDevices = service_.snapshot.devices();
    d.floatingLeases = service_.snapshot.leases();
    for (const auto& l : service_.snapshot.licences) d.floatingSeats += l.floating() ? l.seats : 0;
    d.onlinePending = service_.pending;
    d.snapshotAt = service_.snapshot.at;
    d.onlineFetchedAt = service_.fetch.fetchedAt;
    const std::size_t n = std::min<std::size_t>(records_.size(), 12);
    for (std::size_t i = 0; i < n; ++i) d.recent.push_back(records_[records_.size() - 1 - i]);
    return d;
}

// ------------------------------------------------------------------------------------------------------ la cle
OpResult Workspace::createKey(const std::string& name, bool test) {
    if (key_.present) return OpResult::fail("une cl\xC3\xA9 de l'\xC3\xA9" "diteur existe d\xC3\xA9j\xC3\xA0 sur ce poste (" + key_.id + ") : elle n'est jamais remplac\xC3\xA9" "e");
    std::string why;
    auto k = createPublisherKey(folder_, name, test, &why);
    if (!k) return OpResult::fail(why);
    OpResult r = OpResult::done("Cl\xC3\xA9 de l'\xC3\xA9" "diteur cr\xC3\xA9\xC3\xA9" "e : " + k->keyId() + (test ? " (CL\xC3\x89 D'ESSAI)" : ""));
    r.details.push_back("Empreinte : " + k->fingerprint(0));
    r.details.push_back("Fais-en tout de suite une sauvegarde chiffr\xC3\xA9" "e (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9 \xE2\x80\xBA Sauvegarder la cl\xC3\xA9) : "
                        "sans elle, une panne de ce PC et plus aucune licence ne peut \xC3\xAAtre sign\xC3\xA9" "e.");
    r.output = k->path();
    reload();
    return r;
}

OpResult Workspace::exportPublicKey(const std::string& outPath, bool keep) {
    if (!signing_) return OpResult::fail(key_.problem);
    if (outPath.empty()) return OpResult::fail("quel fichier ? (PublisherKey.hpp)");
    std::string existing, why;
    std::error_code ec;
    if (fs::is_regular_file(P(outPath), ec) && !readText(outPath, existing, &why)) return OpResult::fail(why);
    std::string text, summary;
    const std::string name = U(P(outPath).filename());
    if (!exportPublisherKeys(signing_.get(), existing, name, keep, false, now(), text, &why, &summary)) return OpResult::fail(why);
    if (!writeText(outPath, text, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Cl\xC3\xA9 publique \xC3\xA9" "crite : " + outPath);
    for (std::size_t a = 0, b; a < summary.size(); a = b + 1) {
        b = summary.find('\n', a);
        if (b == std::string::npos) b = summary.size();
        if (b > a) r.details.push_back(summary.substr(a, b - a));
    }
    if (signing_->test()) r.details.push_back("CL\xC3\x89 D'ESSAI : compil\xC3\xA9" "e seulement avec l'option CMake XPG_LICENCES_CLE_ESSAI, jamais livr\xC3\xA9" "e.");
    r.output = outPath;
    return r;
}

bool Workspace::acceptablePhrase(const std::string& phrase, std::string* why) { return acceptablePassphrase(phrase, why); }

OpResult Workspace::backupKey(const std::string& phrase, const std::string& outPath) const {
    if (!signing_) return OpResult::fail(key_.problem);
    if (outPath.empty()) return OpResult::fail("o\xC3\xB9 \xC3\xA9" "crire la sauvegarde ?");
    std::string out, why;
    if (!writeKeyBackup(*signing_, phrase, out, &why)) return OpResult::fail(why);
    if (!writeText(outPath, out, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Sauvegarde de la cl\xC3\xA9 \xC3\xA9" "crite : " + outPath);
    r.details.push_back("Range-la HORS de ce PC (cl\xC3\xA9 USB au coffre, un second support) ; note la phrase \xC3\xA0 part.");
    r.details.push_back("Sans la phrase, la sauvegarde ne sert \xC3\xA0 rien ; avec elle, n'importe qui peut signer des licences : garde les deux s\xC3\xA9par\xC3\xA9ment.");
    r.output = outPath;
    return r;
}

OpResult Workspace::checkBackup(const std::string& path, const std::string& phrase) const {
    std::string text, why, id, name;
    if (!readText(path, text, &why)) return OpResult::fail(why);
    if (!license_admin::checkKeyBackup(text, phrase, id, name, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Bonne : la phrase ouvre la sauvegarde de la cl\xC3\xA9 " + id + ".");
    if (!name.empty()) r.details.push_back("Nom de la cl\xC3\xA9 : " + name);
    if (key_.present && id != key_.id) r.details.push_back("Attention : ce n'est pas la cl\xC3\xA9 de ce poste (" + key_.id + ").");
    return r;
}

OpResult Workspace::restoreKey(const std::string& path, const std::string& phrase) {
    std::string text, why;
    if (!readText(path, text, &why)) return OpResult::fail(why);
    auto k = restoreKeyBackup(folder_, text, phrase, &why);
    if (!k) return OpResult::fail(why);
    OpResult r = OpResult::done("Cl\xC3\xA9 restaur\xC3\xA9" "e sur ce PC : " + k->keyId());
    r.details.push_back("Prot\xC3\xA9g\xC3\xA9" "e pour ce compte" + std::string(crypto::protectedHere() ? " Windows (DPAPI)." : "."));
    reload();
    return r;
}

// ---------------------------------------------------------------------------------------------------- l'archive
OpResult Workspace::archiveAndNote(const std::string& bytes, const std::vector<ActivationRecord>& records, OpResult r) {
    if (!records.empty()) {
        std::string why;
        if (appendActivations(folder_, records, &why))
            r.details.push_back("Registre des activations : " + plural(static_cast<int>(records.size()), "ligne ajout\xC3\xA9" "e", "lignes ajout\xC3\xA9" "es") + ".");
        else
            r.details.push_back("ATTENTION : le registre des activations n'a pas \xC3\xA9t\xC3\xA9 mis \xC3\xA0 jour (" + why + ") ; le fichier sign\xC3\xA9 est bien \xC3\xA9" "crit.");
    }
    std::string what, why;
    if (archive::archiveSigned(folder_, bytes, archive::trustedKeys(signing_.get()), &what, &why))
        // Une emission de la meme seconde que celle de l'archive n'y entre pas : c'est dit en clair.
        r.details.push_back((what.rfind("plus ancien", 0) == 0 ? std::string("ATTENTION : archive : ") : std::string("Archive : ")) + what + ".");
    else
        r.details.push_back("ATTENTION : pas rang\xC3\xA9 dans l'archive (" + why + ") ; le fichier sign\xC3\xA9 est bien \xC3\xA9" "crit.");
    reload();
    if (service_.ready() && service_.syncKey) r.details.push_back("\xC3\x80 envoyer dans l'archive en ligne : Service en ligne \xE2\x80\xBA Synchroniser l'archive.");
    return r;
}

OpResult Workspace::importFiles(const std::vector<std::string>& paths) {
    if (paths.empty()) return OpResult::fail("aucun fichier");
    OpResult r = OpResult::done("");
    int stored = 0, requests = 0, refused = 0;
    std::vector<ActivationRecord> notes;
    for (const auto& path : paths) {
        const std::string name = U(P(path).filename());
        std::string text, why;
        if (!readText(path, text, &why)) {
            r.details.push_back(name + " : " + why);
            ++refused;
            continue;
        }
        std::vector<SignedBlock> blocks;
        if (!licensing::readBlocks(text, blocks, &why) || blocks.empty()) {
            r.details.push_back(name + " : pas un fichier sign\xC3\xA9 (" + why + ")");
            ++refused;
            continue;
        }
        if (blocks.front().kind == DocKind::Request) {
            ActivationRecord rec;
            if (!requestRecord(text, now(), rec, &why)) {
                r.details.push_back(name + " : " + why);
                ++refused;
                continue;
            }
            r.details.push_back(name + " : demande du PC " + (rec.deviceName.empty() ? std::string("(sans nom)") : "\xC2\xAB " + rec.deviceName + " \xC2\xBB") + " (" + rec.fingerprint + "), not\xC3\xA9" "e.");
            notes.push_back(std::move(rec));
            ++requests;
            continue;
        }
        std::string what;
        if (archive::archiveSigned(folder_, text, archive::trustedKeys(signing_.get()), &what, &why)) {
            r.details.push_back(name + " : " + what + ".");
            ++stored;
        } else {
            r.details.push_back(name + " : " + why);
            ++refused;
        }
    }
    if (!notes.empty()) {
        std::string why;
        if (!appendActivations(folder_, notes, &why)) {
            r.details.push_back("ATTENTION : le registre des activations n'a pas \xC3\xA9t\xC3\xA9 mis \xC3\xA0 jour (" + why + ").");
            refused += requests;
            requests = 0;
        }
    }
    reload();
    r.ok = refused == 0;
    std::string head;
    if (stored) head += plural(stored, "fichier rang\xC3\xA9", "fichiers rang\xC3\xA9s") + " dans l'archive";
    if (requests) head += std::string(head.empty() ? "" : ", ") + plural(requests, "demande not\xC3\xA9" "e", "demandes not\xC3\xA9" "es");
    if (refused) head += std::string(head.empty() ? "" : ", ") + plural(refused, "fichier refus\xC3\xA9", "fichiers refus\xC3\xA9s");
    r.message = head + ".";
    r.message[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(r.message[0])));
    return r;
}

OpResult Workspace::exportLicence(const std::string& licenseId, const std::string& outPath) const {
    const LicenceEntry* l = licence(licenseId);
    if (!l) return OpResult::fail("la licence " + licenseId + " n'est pas dans l'archive");
    if (outPath.empty()) return OpResult::fail("o\xC3\xB9 \xC3\xA9" "crire le fichier ?");
    std::string text, why;
    if (!readText(l->path, text, &why) || !writeText(outPath, text, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Fichier de licence \xC3\xA9" "crit : " + outPath);
    r.details.push_back("Envoie-le \xC3\xA0 " + l->licence.customer + " : Aide \xE2\x80\xBA Licence\xE2\x80\xA6 \xE2\x80\xBA Activer par fichier de licence.");
    r.output = outPath;
    return r;
}

// ------------------------------------------------------------------------------------------------- l'assistant
RequestPreview Workspace::readRequest(const std::string& path) const {
    RequestPreview p;
    p.path = path;
    std::string text;
    if (!readText(path, text, &p.why)) return p;
    p.ok = requestRecord(text, now(), p.record, &p.why);
    return p;
}

std::string Workspace::suggestLicenseId() const {
    const std::string year = licensing::isoDate(now()).substr(0, 4);
    const std::string prefix = "XPG-" + year + "-";
    long best = 0;
    const auto consider = [&](const std::string& id) {
        if (id.rfind(prefix, 0) != 0) return;
        const std::string tail = id.substr(prefix.size());
        if (tail.empty() || tail.size() > 9 || !std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; })) return;
        best = std::max(best, std::stol(tail));
    };
    for (const auto& l : licences_) consider(l.licence.licenseId);
    for (const auto& r : records_) consider(r.licenseId);
    char b[32];
    std::snprintf(b, sizeof b, "%05ld", best + 1);
    return prefix + b;
}

namespace {

// Les parametres de la console (signer-licence --parametres), depuis le brouillon. Une fonctionnalite choisie
// garde les limites que l'edition lui donne (max_views...) : la liste donnee remplace celle de l'edition.
std::string paramsOf(const LicenceDraft& d, const licensing::Registry& reg, const licensing::License* existing) {
    core::json::Value p = core::json::Value::object();
    p.set("license_id", d.licenseId);
    p.set("customer", d.customer);
    p.set("edition", d.edition);
    if (d.type) p.set("type", licensing::licenseTypeKey(*d.type));
    if (d.maxDevices > 0) p.set("max_devices", d.maxDevices);
    const bool perpetual = d.type && *d.type == licensing::LicenseType::Perpetual;
    if (d.expiresAt > 0 && !perpetual) p.set("expires_at", licensing::isoTime(d.expiresAt));
    else if (d.durationDays >= 0 && !perpetual) p.set("duration_days", d.durationDays);
    if (d.offlineGraceDays >= 0) p.set("offline_grace_days", d.offlineGraceDays);
    if (!d.features.empty()) {
        core::json::Value f = core::json::Value::array();
        const licensing::EditionDef* ed = reg.edition(d.edition);
        for (const auto& id : d.features) {
            const licensing::FeatureGrant* grant = nullptr;
            if (existing)   // une licence etendue garde ses propres limites
                for (const auto& g : existing->features)
                    if (g.id == id) grant = &g;
            if (!grant && ed)
                for (const auto& g : ed->features)
                    if (g.id == id) grant = &g;
            if (!grant || grant->limits.empty()) {
                f.push(id);
                continue;
            }
            core::json::Value item = core::json::Value::object();
            item.set("id", id);
            core::json::Value lim = core::json::Value::object();
            for (const auto& [name, value] : grant->limits) lim.set(name, value);
            item.set("limits", std::move(lim));
            f.push(std::move(item));
        }
        p.set("features", std::move(f));
    }
    return core::json::write(p);
}

} // namespace

namespace {

// Le resume de signLicense -> les lignes du detail.
void addSummary(OpResult& r, const std::string& summary) {
    for (std::size_t a = 0, b; a < summary.size(); a = b + 1) {
        b = summary.find('\n', a);
        if (b == std::string::npos) b = summary.size();
        if (b > a) r.details.push_back(summary.substr(a, b - a));
    }
}

} // namespace

// Les entrees de signLicense, depuis le brouillon (faux : `why`).
static bool inputsOf(const Workspace& w, const LicenceDraft& d, const std::string& registry, const std::string& revocations, LicenseInputs& in,
                     std::string* why) {
    const LicenceEntry* old = d.extend ? w.licence(d.licenseId) : nullptr;
    in.params = paramsOf(d, w.registry(), old ? &old->licence : nullptr);
    in.dropDevices = d.dropDevices;
    in.registry = registry;
    in.revocations = revocations;
    if (d.extend) {
        if (!old) {
            if (why) *why = "la licence " + d.licenseId + " n'est pas dans l'archive";
            return false;
        }
        if (!readText(old->path, in.existing, why)) return false;
    }
    if (d.online) {
        const auto& s = w.service();
        if (!s.ready()) {
            if (why) *why = "le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne) : une licence en ligne le nomme";
            return false;
        }
        if (!d.requestPaths.empty()) {
            if (why) *why = "une licence en ligne ne lie aucun PC : chaque PC l'active avec son code (retire les demandes)";
            return false;
        }
        in.service = s.config.url;
        return true;
    }
    for (const auto& path : d.requestPaths) {
        std::string req;
        if (!readText(path, req, why)) {
            if (why) *why = U(P(path).filename()) + " : " + *why;
            return false;
        }
        in.requests.push_back(std::move(req));
    }
    if (in.requests.empty() && !d.extend) {
        if (why) *why = "il faut au moins une demande d'activation (.xpgreq) du client";
        return false;
    }
    return true;
}

OpResult Workspace::preview(const LicenceDraft& d) const {
    if (!signing_) return OpResult::fail(key_.problem + " (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9 \xE2\x80\xBA Cr\xC3\xA9" "er la cl\xC3\xA9)");
    if (!hasRegistry_) return OpResult::fail("pas de registre sign\xC3\xA9 dans l'archive (\xC3\x89" "ditions \xE2\x80\xBA Signer le registre)");
    LicenseInputs in;
    std::string why;
    if (!inputsOf(*this, d, archivedText(kRegistryRel), archivedText(kRevocationsRel), in, &why)) return OpResult::fail(why);
    std::string out, summary;
    if (!signLicense(*signing_, in, now(), out, &why, &summary)) return OpResult::fail(why);
    OpResult r = OpResult::done("La licence " + d.licenseId + " est pr\xC3\xAAte \xC3\xA0 \xC3\xAAtre sign\xC3\xA9" "e :");
    addSummary(r, summary);
    if (d.online)
        r.details.push_back("En ligne : un code d'activation neuf est tir\xC3\xA9 \xC3\xA0 la signature ; bail de " + std::to_string(d.leaseDays) +
                            " j (un PC qui ne se connecte plus s'arr\xC3\xAAte au bout).");
    return r;
}

OpResult Workspace::signLicence(const LicenceDraft& d, const std::string& outPath) {
    if (!signing_) return OpResult::fail(key_.problem);
    if (!hasRegistry_) return OpResult::fail("pas de registre sign\xC3\xA9 dans l'archive (\xC3\x89" "ditions \xE2\x80\xBA Signer le registre)");
    if (outPath.empty()) return OpResult::fail("o\xC3\xB9 \xC3\xA9" "crire le fichier de licence ?");
    LicenceDraft bound = d;
    LicenseInputs in;
    std::string why;
    if (d.online && !d.extend) return OpResult::fail("une licence en ligne se signe avec son code (Nouvelle licence en ligne)");
    if (!inputsOf(*this, bound, archivedText(kRegistryRel), archivedText(kRevocationsRel), in, &why)) return OpResult::fail(why);
    std::string out, summary;
    std::vector<ActivationRecord> records;
    if (!signLicense(*signing_, in, now(), out, &why, &summary, &records)) return OpResult::fail(why);
    {
        std::error_code ec;   // 1.34.0 : demandes-en-ligne/licences/ (une demande en ligne : le fichier sans rien demander)
        fs::create_directories(P(outPath).parent_path(), ec);
    }
    if (!writeText(outPath, out, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Licence " + d.licenseId + " sign\xC3\xA9" "e : " + outPath);
    // 1.37.0 : UNE SEULE ACCEPTATION - les demandes en ligne auxquelles elle repond sont << acceptees ici >> des maintenant,
    // meme si la reponse ne part pas tout de suite (Demandes en ligne : Renvoyer la reponse).
    for (const auto& p : d.requestPaths)
        if (const online::Entry* e = onlineRequestAt(p); e && e->waiting()) {
            std::string w;
            if (!online::markAcceptedHere(folder_, e->number, d.licenseId, &w)) r.details.push_back("ATTENTION : " + w);
        }
    addSummary(r, summary);
    if (signing_->test()) r.details.push_back("!!! sign\xC3\xA9" "e avec une CL\xC3\x89 D'ESSAI : jamais pour un client !!!");
    licensing::License signedLicence;
    SignedBlock lb;
    const bool online = blockOf(out, DocKind::License, lb, nullptr) && licensing::parseLicense(lb.document, signedLicence, nullptr) && isOnlineLicence(signedLicence);
    if (service_.ready() && online) {
        // Une licence en ligne prolongee : deposee sur le service, le code reste le meme.
        r.details.push_back("Service : la nouvelle \xC3\xA9mission part sur le service ; le code d'activation reste le m\xC3\xAAme.");
        ServiceJob job;
        job.kind = ServiceJob::Kind::OnlineLicence;
        job.label = "Service : nouvelle \xC3\xA9mission de " + d.licenseId;
        job.licenseId = d.licenseId;
        job.licenceText = out;   // leaseDays 0 : le service garde son bail
        queueServiceJob(std::move(job));
    } else if (service_.ready()) {
        r.details.push_back("Service : la licence part \xC3\xA0 chacun de ses PC (ils la r\xC3\xA9" "cup\xC3\xA8rent seuls \xC3\xA0 leur prochaine connexion).");
        const bool allOnline = !d.requestPaths.empty() && std::all_of(d.requestPaths.begin(), d.requestPaths.end(), [this](const std::string& p) {
            return online::isOnlineRequestPath(folder_, p);
        });
        if (!allOnline)
            r.details.push_back("Un logiciel d'avant la 1.35.0 ne lit pas le service : envoie-lui aussi ce fichier (Aide \xE2\x80\xBA Licence\xE2\x80\xA6 \xE2\x80\xBA "
                                "Activer par fichier de licence).");
        ServiceJob job;
        job.kind = ServiceJob::Kind::Licence;
        job.label = "Service : publication de " + d.licenseId;
        job.licenseId = d.licenseId;
        job.licenceText = out;
        queueServiceJob(std::move(job));
    } else {
        r.details.push_back("Envoie ce fichier au client : Aide \xE2\x80\xBA Licence\xE2\x80\xA6 \xE2\x80\xBA Activer par fichier de licence.");
    }
    r.output = outPath;
    return archiveAndNote(out, records, std::move(r));
}

OpResult Workspace::prepareOnlineLicence(const LicenceDraft& draft, const std::string& outPath, ServiceJob& job, std::string& code, int requestNumber) {
    code.clear();
    if (!signing_) return OpResult::fail(key_.problem);
    if (!hasRegistry_) return OpResult::fail("pas de registre sign\xC3\xA9 dans l'archive (\xC3\x89" "ditions \xE2\x80\xBA Signer le registre)");
    if (!draft.online) return OpResult::fail("ce n'est pas une licence en ligne");
    if (draft.leaseDays < 1 || draft.leaseDays > 366) return OpResult::fail("le bail d'une activation va de 1 \xC3\xA0 366 jours");
    if (!draft.extend && licence(draft.licenseId))
        return OpResult::fail("la licence " + draft.licenseId + " est d\xC3\xA9j\xC3\xA0 dans l'archive : choisis un autre identifiant");
    LicenseInputs in;
    std::string why;
    if (!inputsOf(*this, draft, archivedText(kRegistryRel), archivedText(kRevocationsRel), in, &why)) return OpResult::fail(why);
    std::string out, summary;
    std::vector<ActivationRecord> records;
    if (!signLicense(*signing_, in, now(), out, &why, &summary, &records)) return OpResult::fail(why);
    if (!outPath.empty()) {
        std::error_code ec;
        fs::create_directories(P(outPath).parent_path(), ec);
        if (!writeText(outPath, out, &why)) return OpResult::fail(why);
    }
    job = ServiceJob{};
    job.kind = ServiceJob::Kind::OnlineLicence;
    job.label = "Service : d\xC3\xA9p\xC3\xB4t de la licence en ligne " + draft.licenseId;
    job.licenseId = draft.licenseId;
    job.licenceText = out;
    job.leaseDays = draft.extend ? 0 : draft.leaseDays;   // une prolongation : le service garde son bail
    job.requestNumber = requestNumber;
    if (!draft.extend) {
        // Le code : tire ici, son empreinte seule part au service ; le code lui-meme est rendu a l'appelant (montre une fois).
        code = licensing::service::newActivationCode();
        std::string normalized;
        (void)licensing::service::normalizeCode(code, normalized);
        job.codeHash = licensing::service::codeHash(normalized);
        crypto::wipe(normalized.data(), normalized.size());
    }
    OpResult r = OpResult::done(draft.extend ? "Licence en ligne " + draft.licenseId + " prolong\xC3\xA9" "e : la nouvelle \xC3\xA9mission part sur le service (le code reste le m\xC3\xAAme)."
                                             : "Licence en ligne " + draft.licenseId + " sign\xC3\xA9" "e : elle part sur le service avec l'empreinte d'un code d'activation neuf.");
    addSummary(r, summary);
    if (signing_->test()) r.details.push_back("!!! sign\xC3\xA9" "e avec une CL\xC3\x89 D'ESSAI : jamais pour un client !!!");
    if (!outPath.empty()) r.output = outPath;
    return archiveAndNote(out, records, std::move(r));
}

OpResult Workspace::prepareNewCode(const std::string& licenseId, ServiceJob& job, std::string& code) {
    code.clear();
    const LicenceEntry* l = licence(licenseId);
    if (!l) return OpResult::fail("la licence " + licenseId + " n'est pas dans l'archive");
    if (!isOnlineLicence(l->licence)) return OpResult::fail("la licence " + licenseId + " est li\xC3\xA9" "e \xC3\xA0 des PC : elle n'a pas de code d'activation");
    if (!service_.ready()) return OpResult::fail("le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne)");
    std::string text, why;
    if (!readText(l->path, text, &why)) return OpResult::fail(why);
    code = licensing::service::newActivationCode();
    std::string normalized;
    (void)licensing::service::normalizeCode(code, normalized);
    job = ServiceJob{};
    job.kind = ServiceJob::Kind::OnlineLicence;
    job.label = "Service : un code neuf pour " + licenseId;
    job.licenseId = licenseId;
    job.licenceText = std::move(text);
    job.codeHash = licensing::service::codeHash(normalized);   // leaseDays 0 : le bail du service garde
    crypto::wipe(normalized.data(), normalized.size());
    return OpResult::done("Un code neuf pour la licence " + licenseId + " : il remplace l'ancien d\xC3\xA8s qu'il est d\xC3\xA9pos\xC3\xA9.");
}

OpResult Workspace::removeDevice(const std::string& licenseId, const std::string& deviceKey, const std::string& outPath) {
    const LicenceEntry* l = licence(licenseId);
    if (!l) return OpResult::fail("la licence " + licenseId + " n'est pas dans l'archive");
    const auto& lic = l->licence;
    if (std::none_of(lic.devices.begin(), lic.devices.end(), [&](const licensing::BoundDevice& x) { return x.key == deviceKey; }))
        return OpResult::fail("ce PC n'est pas li\xC3\xA9 \xC3\xA0 la licence " + licenseId);
    if (lic.devices.size() < 2) return OpResult::fail("c'est le seul PC de la licence : r\xC3\xA9voque-la plut\xC3\xB4t (R\xC3\xA9voquer...)");
    LicenceDraft d;
    d.extend = true;
    d.licenseId = lic.licenseId;
    d.customer = lic.customer;
    d.edition = lic.edition;
    d.type = lic.type;
    d.maxDevices = lic.maxDevices;
    d.offlineGraceDays = lic.offlineGraceDays;
    d.expiresAt = lic.expiresAt;
    d.durationDays = lic.expiresAt ? -1 : 0;
    for (const auto& g : lic.features) d.features.push_back(g.id);
    d.dropDevices = {deviceKey};
    OpResult r = signLicence(d, outPath);
    if (r.ok && job_ && job_->kind == ServiceJob::Kind::Licence && job_->licenseId == licenseId) {
        job_->kind = ServiceJob::Kind::Removal;
        job_->deviceKey = deviceKey;
        job_->label = "Service : nouvelle \xC3\xA9mission de " + licenseId + " (PC retir\xC3\xA9)";
    }
    if (r.ok) {
        r.message = "PC retir\xC3\xA9 de la licence " + licenseId + " : nouvelle \xC3\xA9mission " + outPath;
        r.details.push_back("Pour qu'il ne serve plus avec l'ancienne \xC3\xA9mission : R\xC3\xA9voquer... ce PC (\xC2\xAB toutes les \xC3\xA9missions \xC2\xBB).");
    }
    return r;
}

OpResult Workspace::recordRequests(const std::vector<std::string>& paths) {
    if (paths.empty()) return OpResult::fail("aucune demande");
    std::vector<ActivationRecord> notes;
    OpResult r = OpResult::done("");
    for (const auto& path : paths) {
        RequestPreview p = readRequest(path);
        if (!p.ok) return OpResult::fail(U(P(path).filename()) + " : " + p.why);
        r.details.push_back("PC " + (p.record.deviceName.empty() ? std::string("(sans nom)") : "\xC2\xAB " + p.record.deviceName + " \xC2\xBB") + " (" + p.record.fingerprint + ")"
                            + (p.record.software.empty() ? std::string() : ", " + p.record.software));
        notes.push_back(std::move(p.record));
    }
    std::string why;
    if (!appendActivations(folder_, notes, &why)) return OpResult::fail(why);
    reload();
    r.message = plural(static_cast<int>(notes.size()), "demande not\xC3\xA9" "e", "demandes not\xC3\xA9" "es") + " dans le registre des activations.";
    r.message[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(r.message[0])));
    return r;
}

// ------------------------------------------------------------------------------------------------ revocations
OpResult Workspace::signRevocationList(licensing::RevocationList list, const std::string& what) {
    if (!signing_) return OpResult::fail(key_.problem);
    core::json::Value root = core::json::Value::object();
    root.set("number", static_cast<std::uint64_t>(hasRevocations_ ? revocations_.number + 1 : 1));
    core::json::Value arr = core::json::Value::array();
    for (const auto& x : list.revoked) {
        core::json::Value e = core::json::Value::object();
        e.set("license_id", x.licenseId);
        e.set("reason", x.reason);
        if (!x.device.empty()) e.set("device", x.device);
        if (x.revokedAt) e.set("revoked_at", licensing::isoTime(x.revokedAt));
        if (x.issuedBefore) e.set("issued_before", licensing::isoTime(x.issuedBefore));
        arr.push(std::move(e));
    }
    root.set("revoked", std::move(arr));
    std::string out, why;
    std::vector<ActivationRecord> records;
    if (!signRevocations(*signing_, core::json::write(root), now(), out, &why, &records)) return OpResult::fail(why);
    OpResult r = OpResult::done(what);
    r.details.push_back("Liste de r\xC3\xA9vocation n\xC2\xB0 " + std::to_string(hasRevocations_ ? revocations_.number + 1 : 1) + " sign\xC3\xA9" "e ("
                        + plural(static_cast<int>(list.revoked.size()), "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + ").");
    r.details.push_back("Elle arrive chez un client avec sa prochaine licence, ou par la liste export\xC3\xA9" "e (R\xC3\xA9vocations \xE2\x80\xBA Exporter la liste).");
    return archiveAndNote(out, records, std::move(r));
}

OpResult Workspace::revoke(const std::string& licenseId, const std::string& deviceKey, const std::string& reason, bool onlyThisIssue) {
    if (!licensing::isLicenseId(licenseId)) return OpResult::fail("identifiant de licence non valide : " + licenseId);
    if (reason.empty()) return OpResult::fail("donne une raison (\xC2\xAB Fin de contrat \xC2\xBB, \xC2\xAB PC remplac\xC3\xA9 \xC2\xBB...)");
    if (!deviceKey.empty() && !licensing::isKeyHex(deviceKey)) return OpResult::fail("la cl\xC3\xA9 du PC n'est pas valide");
    licensing::RevocationList list = hasRevocations_ ? revocations_ : licensing::RevocationList{};
    for (const auto& x : list.revoked)
        if (x.licenseId == licenseId && x.device == deviceKey && x.issuedBefore == 0)
            return OpResult::fail(deviceKey.empty() ? "la licence " + licenseId + " est d\xC3\xA9j\xC3\xA0 r\xC3\xA9voqu\xC3\xA9" "e" : "ce PC est d\xC3\xA9j\xC3\xA0 r\xC3\xA9voqu\xC3\xA9");
    licensing::Revocation x;
    x.licenseId = licenseId;
    x.device = deviceKey;
    x.reason = reason;
    x.revokedAt = now();
    if (onlyThisIssue) x.issuedBefore = now();
    list.revoked.push_back(x);
    const std::string what = deviceKey.empty() ? "Licence " + licenseId + " r\xC3\xA9voqu\xC3\xA9" "e" + (onlyThisIssue ? " (les \xC3\xA9missions sign\xC3\xA9" "es jusqu'ici)." : ".")
                                               : "PC r\xC3\xA9voqu\xC3\xA9 sur la licence " + licenseId + ".";
    // Les PC prevenus (<< revoquee >>) : celui-la, ou tous ceux de la licence - sauf une revocation des seules emissions
    // d'avant (leur emission en cours reste valable : la liste de revocation partira, scellee, avec la prochaine emission).
    std::vector<std::string> keys;
    if (!deviceKey.empty()) {
        if (!onlyThisIssue) keys.push_back(deviceKey);
    } else if (const LicenceEntry* l = licence(licenseId); l && !onlyThisIssue)
        for (const auto& dv : l->licence.devices) keys.push_back(dv.key);   // une licence en ligne n'en a pas : le service suffit
    OpResult r = signRevocationList(std::move(list), what);
    if (r.ok && service_.ready()) {
        ServiceJob job;
        job.kind = ServiceJob::Kind::Revocation;
        job.label = "Service : r\xC3\xA9vocation de " + licenseId;
        job.licenseId = licenseId;
        job.deviceKeys = std::move(keys);
        job.reason = reason;
        queueServiceJob(std::move(job));
    }
    return r;
}

OpResult Workspace::unrevoke(std::size_t index) {
    if (!hasRevocations_ || index >= revocations_.revoked.size()) return OpResult::fail("cette r\xC3\xA9vocation n'est pas dans la liste");
    licensing::RevocationList list = revocations_;
    const auto removed = list.revoked[index];
    list.revoked.erase(list.revoked.begin() + static_cast<std::ptrdiff_t>(index));
    OpResult r = signRevocationList(std::move(list), "R\xC3\xA9vocation retir\xC3\xA9" "e : " + removed.licenseId + (removed.device.empty() ? "" : " (un PC)") + ".");
    if (r.ok && service_.ready()) {
        ServiceJob job;
        job.kind = ServiceJob::Kind::Unrevoke;
        job.label = "Service : r\xC3\xA9vocation retir\xC3\xA9" "e (" + removed.licenseId + ")";
        job.licenseId = removed.licenseId;
        if (const LicenceEntry* l = licence(removed.licenseId); l && !isOnlineLicence(l->licence)) {
            std::string why;
            (void)readText(l->path, job.licenceText, &why);
        }
        queueServiceJob(std::move(job));
    }
    if (r.ok) r.details.push_back("Un client qui a d\xC3\xA9j\xC3\xA0 import\xC3\xA9 l'ancienne liste la garde jusqu'\xC3\xA0 ce qu'il re\xC3\xA7oive celle-ci.");
    return r;
}

OpResult Workspace::exportRevocations(const std::string& outPath) const {
    if (!hasRevocations_) return OpResult::fail("pas de liste de r\xC3\xA9vocation dans l'archive");
    if (outPath.empty()) return OpResult::fail("o\xC3\xB9 \xC3\xA9" "crire la liste ?");
    std::string why;
    const std::string text = archivedText(kRevocationsRel);
    if (text.empty() || !writeText(outPath, text, &why)) return OpResult::fail(why.empty() ? "la liste ne se lit pas" : why);
    OpResult r = OpResult::done("Liste de r\xC3\xA9vocation \xC3\xA9" "crite : " + outPath);
    r.details.push_back("Le client l'importe comme une licence : Aide \xE2\x80\xBA Licence\xE2\x80\xA6 \xE2\x80\xBA Activer par fichier de licence.");
    r.output = outPath;
    return r;
}

// --------------------------------------------------------------------------------------------------- registre
OpResult Workspace::signRegistry(const licensing::Registry& edited) {
    if (!signing_) return OpResult::fail(key_.problem);
    licensing::Registry reg = edited;
    reg.number = hasRegistry_ ? registry_.number + 1 : std::max<std::uint64_t>(1, edited.number);
    reg.issuedAt = now();
    std::string out, why;
    if (!license_admin::signRegistry(*signing_, licensing::writeRegistry(reg), now(), out, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Registre n\xC2\xB0 " + std::to_string(reg.number) + " sign\xC3\xA9 (" + plural(static_cast<int>(reg.editions.size()), "\xC3\xA9" "dition", "\xC3\xA9" "ditions")
                                + ", " + plural(static_cast<int>(reg.features.size()), "fonctionnalit\xC3\xA9", "fonctionnalit\xC3\xA9s") + ").");
    r.details.push_back("Il part avec chaque licence sign\xC3\xA9" "e ensuite ; un client l'a \xC3\xA0 sa prochaine licence.");
    OpResult done = archiveAndNote(out, {}, std::move(r));
    if (done.ok && service_.ready()) {
        ServiceJob job;
        job.kind = ServiceJob::Kind::Registry;
        job.label = "Service : d\xC3\xA9p\xC3\xB4t du registre n\xC2\xB0 " + std::to_string(reg.number);
        queueServiceJob(std::move(job));
    }
    return done;
}

OpResult Workspace::signRegistryJson(const std::string& path) {
    if (!signing_) return OpResult::fail(key_.problem);
    std::string text, out, why;
    if (!readText(path, text, &why)) return OpResult::fail(why);
    if (!license_admin::signRegistry(*signing_, text, now(), out, &why)) return OpResult::fail(why);
    SignedBlock b;
    licensing::Registry reg;
    if (blockOf(out, DocKind::Registry, b, &why)) (void)licensing::parseRegistry(b.document, reg, &why);
    if (hasRegistry_ && reg.number <= registry_.number)
        return OpResult::fail("ce registre porte le n\xC2\xB0 " + std::to_string(reg.number) + ", pas plus que celui de l'archive (n\xC2\xB0 " + std::to_string(registry_.number)
                              + ") : augmente \"number\"");
    OpResult r = OpResult::done("Registre n\xC2\xB0 " + std::to_string(reg.number) + " sign\xC3\xA9 depuis " + U(P(path).filename()) + ".");
    OpResult done = archiveAndNote(out, {}, std::move(r));
    if (done.ok && service_.ready()) {
        ServiceJob job;
        job.kind = ServiceJob::Kind::Registry;
        job.label = "Service : d\xC3\xA9p\xC3\xB4t du registre n\xC2\xB0 " + std::to_string(reg.number);
        queueServiceJob(std::move(job));
    }
    return done;
}

// ------------------------------------------------------------------------------------------------- activations
OpResult Workspace::exportCsv(const std::vector<KnownDevice>& list, const std::string& path) const {
    if (path.empty()) return OpResult::fail("o\xC3\xB9 \xC3\xA9" "crire le tableau ?");
    std::string why;
    if (!writeText(path, devicesCsv(list), &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Tableau \xC3\xA9" "crit pour Excel : " + path + " (" + plural(static_cast<int>(list.size()), "PC", "PC") + ").");
    r.output = path;
    return r;
}

// ------------------------------------------------------------------------------------------------- le service
namespace {

OpResult fromResult(const online::Result& x) {
    OpResult r;
    r.ok = x.ok;
    r.message = x.message;
    r.details = x.details;
    return r;
}

online::Options optionsAt(Seconds now) {
    online::Options o;
    o.now = now;
    o.tool = std::string("XPG-License-Manager-Admin/") + kAppVersion;
    return o;
}

} // namespace

OpResult Workspace::serviceConfigure(const std::string& url) {
    online::Config c;
    c.url = url;
    std::string why;
    const bool changed = !service_.config.url.empty() && online::normalizeUrl(url) != service_.config.url;
    if (!online::writeConfig(folder_, c, &why)) return OpResult::fail(why);
    if (changed) service_.hasState = false;   // l'etat lu etait celui de l'autre adresse
    reload();
    OpResult r = OpResult::done("Service des licences r\xC3\xA9gl\xC3\xA9 : " + service_.config.url + ".");
    r.details.push_back("Ensuite : V\xC3\xA9rifier, puis Certifier la cl\xC3\xA9 du service (une fois par an).");
    if (service_.config.url.rfind("http://", 0) == 0) r.details.push_back("Une adresse http:// sur ce poste : un service d'ESSAI, jamais pour les Studios livr\xC3\xA9s.");
    return r;
}

OpResult Workspace::syncKeyCreate(std::string* recoveryCodeOut) {
    if (recoveryCodeOut) recoveryCodeOut->clear();
    archive::SyncKey sk{};
    std::string why;
    if (!archive::createSyncKey(folder_, sk, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Cl\xC3\xA9 de synchro de l'archive cr\xC3\xA9\xC3\xA9" "e : " + archive::syncKeyId(sk) + ".");
    r.details.push_back("NOTE SON CODE DE SECOURS hors de ce PC : sans lui, l'archive en ligne est illisible apr\xC3\xA8s une panne.");
    if (recoveryCodeOut) *recoveryCodeOut = archive::recoveryCode(sk);
    crypto::wipe(sk);
    reload();
    return r;
}

OpResult Workspace::syncKeyRestore(const std::string& code, bool replace) {
    archive::SyncKey sk{};
    std::string why, kept;
    if (!archive::restoreSyncKey(folder_, code, sk, &why, replace, &kept)) return OpResult::fail(why);
    OpResult r = OpResult::done("Cl\xC3\xA9 de synchro redonn\xC3\xA9" "e par le code de secours : " + archive::syncKeyId(sk) + ".");
    if (!kept.empty()) r.details.push_back("L'ancienne est gard\xC3\xA9" "e \xC3\xA0 c\xC3\xB4t\xC3\xA9 : " + kept);
    r.details.push_back("Ensuite : Recevoir l'archive (un poste neuf), puis Synchroniser.");
    crypto::wipe(sk);
    reload();
    return r;
}

std::string Workspace::syncRecoveryCode(std::string* why) const {
    archive::SyncKey sk{};
    if (!archive::loadSyncKey(folder_, sk, why)) return {};
    std::string code = archive::recoveryCode(sk);
    crypto::wipe(sk);
    return code;
}

// Les fonctions lentes relisent la cle (une copie a ce fil : le Workspace n'est pas partage).
#define XPG_SERVICE_KEY_OR_FAIL                                  \
    std::string why;                                             \
    auto key = loadPublisherKey(folder_, &why);                  \
    if (!key) return OpResult::fail("il faut la cl\xC3\xA9 de l'\xC3\xA9" "diteur (elle signe chaque appel au service) : " + why)

OpResult Workspace::serviceVerify(online::ServiceState* state) const {
    XPG_SERVICE_KEY_OR_FAIL;
    return fromResult(online::verify(folder_, *key, optionsAt(now()), state));
}

OpResult Workspace::serviceReadKey(std::string& keyHex) const {
    XPG_SERVICE_KEY_OR_FAIL;
    return fromResult(online::readServiceKey(folder_, *key, optionsAt(now()), keyHex));
}

OpResult Workspace::serviceCertify(const std::string& expectedKey) const {
    XPG_SERVICE_KEY_OR_FAIL;
    return fromResult(online::certify(folder_, *key, optionsAt(now()), expectedKey));
}

archive::SyncReport Workspace::archiveSync(bool receiveOnly, bool dryRun) const {
    archive::SyncOptions o;
    o.receiveOnly = receiveOnly;
    o.dryRun = dryRun;
    o.now = now();
    o.tool = std::string(kAppName) + " " + kAppVersion;
    std::string why;
    auto key = loadPublisherKey(folder_, &why);
    if (!key) {
        archive::SyncReport r;
        r.why = "il faut la cl\xC3\xA9 de l'\xC3\xA9" "diteur (elle signe chaque appel au service) : " + why;
        return r;
    }
    return archive::synchronize(folder_, *key, o);
}

OpResult Workspace::serviceListLicences(online::Snapshot& out) const {
    XPG_SERVICE_KEY_OR_FAIL;
    return fromResult(online::listLicences(folder_, *key, optionsAt(now()), out));
}

OpResult Workspace::serviceRetire(const std::string& licenseId, const std::string& deviceKey) const {
    XPG_SERVICE_KEY_OR_FAIL;
    return fromResult(online::retireDevice(folder_, *key, licenseId, deviceKey, optionsAt(now())));
}

OpResult Workspace::serviceSetActive(const std::string& licenseId, bool active) const {
    XPG_SERVICE_KEY_OR_FAIL;
    return fromResult(online::setActive(folder_, *key, licenseId, active, 0, optionsAt(now())));
}

OpResult Workspace::serviceFetch() const {
    XPG_SERVICE_KEY_OR_FAIL;
    return fromResult(online::fetchRequests(folder_, *key, optionsAt(now())));
}

OpResult Workspace::serviceRefuse(const std::string& which, const std::string& reason) const {
    XPG_SERVICE_KEY_OR_FAIL;
    return fromResult(online::refuse(folder_, *key, which, reason, optionsAt(now())));
}

OpResult Workspace::serviceStats() const {
    XPG_SERVICE_KEY_OR_FAIL;
    return fromResult(online::stats(folder_, *key, optionsAt(now())));
}

OpResult Workspace::serviceRun(const ServiceJob& job) const {
    XPG_SERVICE_KEY_OR_FAIL;
    const online::Options o = optionsAt(now());
    switch (job.kind) {
        case ServiceJob::Kind::Licence: return fromResult(online::publishBoundLicence(folder_, *key, job.licenceText, false, o, mailer()));
        case ServiceJob::Kind::Removal: return fromResult(online::publishRemoval(folder_, *key, job.licenceText, job.deviceKey, o));
        case ServiceJob::Kind::Revocation: return fromResult(online::publishRevocation(folder_, *key, job.licenseId, job.deviceKeys, job.reason, o));
        case ServiceJob::Kind::Registry: return fromResult(online::publishRegistry(folder_, *key, o));
        case ServiceJob::Kind::Unrevoke: {
            OpResult r = fromResult(online::publishRevocations(folder_, *key, o));
            if (!r.ok || job.licenceText.empty()) return r;
            // La liste qui ne revoque plus cette licence part scellee a chacun de ses PC lies.
            OpResult l = fromResult(online::publishBoundLicence(folder_, *key, job.licenceText, true, o));
            l.details.insert(l.details.begin(), r.message);
            return l;
        }
        case ServiceJob::Kind::OnlineLicence: return serviceRunOnline(job, {});
    }
    return OpResult::fail("publication inconnue");
}

OpResult Workspace::serviceRunOnline(const ServiceJob& job, const std::string& code) const {
    XPG_SERVICE_KEY_OR_FAIL;
    const online::Options o = optionsAt(now());
    OpResult r = fromResult(online::putLicence(folder_, *key, job.licenceText, job.codeHash, job.leaseDays, true, o));
    if (!r.ok) return r;
    if (!code.empty()) {
        std::string w;
        if (online::storeCode(folder_, job.licenseId, code, &w))
            r.details.push_back("Le code est gard\xC3\xA9 chiffr\xC3\xA9 sur ce poste : Licences \xE2\x80\xBA Voir le code\xE2\x80\xA6 pour le revoir.");
        else
            r.details.push_back("ATTENTION : le code n'a pas pu \xC3\xAAtre gard\xC3\xA9 sur ce poste (" + w + ") : note-le.");
    }
    if (job.requestNumber > 0) {
        std::string w;   // 1.37.0 : une seule acceptation, meme si la reponse ne part pas (Renvoyer la reponse)
        if (!online::markAcceptedHere(folder_, job.requestNumber, job.licenseId, &w)) r.details.push_back("ATTENTION : " + w);
        const OpResult a = fromResult(online::answerOnline(folder_, *key, job.requestNumber, job.licenseId, code, job.licenceText, mailer(), o));
        r.details.push_back(a.message);
        for (const auto& d : a.details) r.details.push_back(d);
        if (!a.ok) r.ok = false;
    }
    return r;
}

OpResult Workspace::serviceResendAnswer(int number) const {
    XPG_SERVICE_KEY_OR_FAIL;
    const online::Entry* e = onlineRequest(number);
    if (!e || !e->acceptedHere())
        return OpResult::fail("la demande n\xC2\xB0 " + std::to_string(number) + " n'a pas de r\xC3\xA9ponse en attente sur ce poste (rel\xC3\xA8ve les demandes)");
    const std::string licenseId = e->answer;
    const LicenceEntry* l = licence(licenseId);
    std::string text;
    why.clear();
    if (!l || !readText(l->path, text, &why))
        return OpResult::fail("la licence " + licenseId + " n'est pas dans l'archive de ce poste" + (why.empty() ? std::string() : " (" + why + ")"));
    const online::Options o = optionsAt(now());
    if (!isOnlineLicence(l->licence)) return fromResult(online::publishBoundLicence(folder_, *key, text, false, o, mailer()));
    std::string code;
    if (!online::readCode(folder_, licenseId, code, &why)) return OpResult::fail(why);
    OpResult r = fromResult(online::answerOnline(folder_, *key, number, licenseId, code, text, mailer(), o));
    std::fill(code.begin(), code.end(), '\0');
    return r;
}

// 1.37.0 : LE MAIL au client a l'acceptation (online::smtpMailer : la boite de l'editeur, si elle est reglee).
online::Mailer Workspace::mailer() const { return online::smtpMailer(folder_, hasRegistry_ ? &registry_ : nullptr); }

#undef XPG_SERVICE_KEY_OR_FAIL

void Workspace::adoptServiceState(const online::ServiceState& state) {
    service_.state = state;
    service_.hasState = true;
    service_.stateAt = now();
}

void Workspace::adoptSnapshot(online::Snapshot snapshot) { service_.snapshot = std::move(snapshot); }

void Workspace::queueServiceJob(ServiceJob job) { job_ = std::move(job); }

std::optional<ServiceJob> Workspace::takeServiceJob() {
    std::optional<ServiceJob> out;
    out.swap(job_);
    return out;
}

bool Workspace::isOnlineRequest(const std::string& path) const { return online::isOnlineRequestPath(folder_, path); }

std::string Workspace::onlineLicencePath(const std::string& licenseId) const {
    std::string stamp;
    for (char c : licensing::isoTime(now()))
        if (c >= '0' && c <= '9') stamp += c;
    if (stamp.size() == 14) stamp.insert(8, "-");
    const std::string dir = deploy::joinPath(online::requestsFolder(folder_), online::kLicencesDir);
    return deploy::joinPath(dir, archive::safeName(licenseId) + "-" + stamp + ".xpglic");
}

const online::Entry* Workspace::onlineRequest(int number) const {
    for (const auto& e : service_.requests)
        if (e.number == number) return &e;
    return nullptr;
}

const online::Entry* Workspace::onlineRequestAt(const std::string& path) const {
    if (path.empty()) return nullptr;
    for (const auto& e : service_.requests)
        if (e.requestPath == path) return &e;
    return nullptr;
}

OpResult Workspace::studioKit(const std::string& outDir) const { return fromResult(online::studioKit(folder_, signing_.get(), outDir, now())); }

OpResult Workspace::writePublicKeyFile(const std::string& outDir) const {
    if (!signing_) return OpResult::fail(key_.problem);
    if (outDir.empty()) return OpResult::fail("dans quel dossier ?");
    const std::string path = U(P(outDir) / "cle-publique.txt");
    std::string why;
    if (!deploy::crypto::replaceFile(path, online::publicKeyText(*signing_), false, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Cl\xC3\xA9 publique \xC3\xA9" "crite : " + path);
    r.details.push_back("Pour la mise en ligne du service (deployer.bat la lit : PUBLISHER_KEYS). Rien de secret dedans.");
    if (signing_->test()) r.details.push_back("CL\xC3\x89 D'ESSAI : un service qui la croit n'est que pour des essais.");
    r.output = path;
    return r;
}

// ---------------------------------------------------------------------------------------------------- Background
Background::~Background() { wait(); }

bool Background::start(std::string label, std::function<OpResult()> work) {
    if (running_.load()) return false;
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        label_ = std::move(label);
        result_.reset();
    }
    running_.store(true);
    thread_ = std::thread([this, w = std::move(work)] {
        OpResult r;
        try {
            r = w();
        } catch (const std::exception& e) {
            r = OpResult::fail(std::string("erreur inattendue : ") + e.what());
        } catch (...) {
            r = OpResult::fail("erreur inattendue");
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_ = std::move(r);
        }
        running_.store(false);
    });
    return true;
}

std::string Background::label() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return label_;
}

std::optional<OpResult> Background::take() {
    if (running_.load()) return std::nullopt;
    std::optional<OpResult> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out.swap(result_);
    }
    if (out && thread_.joinable()) thread_.join();
    return out;
}

void Background::wait() {
    if (thread_.joinable()) thread_.join();
}

} // namespace license_admin::gui
