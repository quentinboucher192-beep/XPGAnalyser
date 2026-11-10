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
    (void)writeAdminToken(false);   // 1.44.0 : le jeton d'admin, a chaque lancement (s'il manque ou date d'hier)
}

Seconds Workspace::now() const { return clock_ ? clock_() : licensing::systemNow(); }

void Workspace::reload() {
    loadKey();
    loadAdmins();   // 1.44.0 : la cle d'admin, la liste des admins, le role
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
    discoverConnection();   // 1.47.0 : aucune adresse et pas l'admin+ : la connexion signee qui voyage avec l'installateur
    service_.configured = online::configured(folder_);
    service_.hasConnection = connexion::readState(folder_, &service_.connection, &service_.connectionRefused);
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

namespace {
std::vector<DeletedClient> readDeletedClients(const std::string& folder);   // 1.42.0 (plus bas)
} // namespace

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
        if (r.kind == "client") continue;   // 1.44.0 : une suppression, une restauration ne sont pas une activite du client
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
    for (const auto& d : readDeletedClients(folder_)) {   // 1.42.0 : un client supprime ne revient que par une activite plus recente
        auto it = byKey.find(lower(d.name));
        if (it != byKey.end() && it->second.lastActivity <= d.deletedAt) byKey.erase(it);
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
    if (adminKey_.present)   // 1.44.0 : un admin (ou en attente) ne devient pas l'admin+ d'une autre cle de l'editeur
        return OpResult::fail("ce poste a d\xC3\xA9j\xC3\xA0 une cl\xC3\xA9 d'admin (" + adminKey_.id +
                              ") : il signe avec elle ce que ses droits permettent ; la cl\xC3\xA9 de l'\xC3\xA9" "diteur reste sur le poste de l'admin+");
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
namespace {
// 1.44.0 : les cles qui rangent un document dans l'archive : celles de l'editeur que ce poste croit, et la cle qui signe.
std::vector<crypto::PublicKey> archiveTrust(const std::vector<crypto::PublicKey>& editors, const licensing::SigningKey* signer) {
    std::vector<crypto::PublicKey> out = editors;
    if (signer && std::find(out.begin(), out.end(), signer->publicKey()) == out.end()) out.push_back(signer->publicKey());
    return out;
}
} // namespace

OpResult Workspace::archiveAndNote(const std::string& bytes, const std::vector<ActivationRecord>& records, OpResult r) {
    if (!records.empty()) {
        std::string why;
        if (appendActivations(folder_, records, &why))
            r.details.push_back("Registre des activations : " + plural(static_cast<int>(records.size()), "ligne ajout\xC3\xA9" "e", "lignes ajout\xC3\xA9" "es") + ".");
        else
            r.details.push_back("ATTENTION : le registre des activations n'a pas \xC3\xA9t\xC3\xA9 mis \xC3\xA0 jour (" + why + ") ; le fichier sign\xC3\xA9 est bien \xC3\xA9" "crit.");
    }
    std::string what, why;
    if (archive::archiveSigned(folder_, bytes, archiveTrust(editorKeys_, signer()), adminList_ ? &*adminList_ : nullptr, &what, &why))
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
        if (archive::archiveSigned(folder_, text, archiveTrust(editorKeys_, signer()), adminList_ ? &*adminList_ : nullptr, &what, &why)) {
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
    if (!readText(l->path, text, &why) || !writeText(outPath, withAdminList(text), &why)) return OpResult::fail(why);   // 1.44.0
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
    if (!signer() && role_ == Role::None) return OpResult::fail(key_.problem + " (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9 \xE2\x80\xBA Cr\xC3\xA9" "er la cl\xC3\xA9)");
    if (const auto deny = denied(licenceRight(d))) return *deny;   // 1.44.0
    if (!hasRegistry_) return OpResult::fail("pas de registre sign\xC3\xA9 dans l'archive (\xC3\x89" "ditions \xE2\x80\xBA Signer le registre)");
    LicenseInputs in;
    std::string why;
    if (!inputsOf(*this, d, archivedText(kRegistryRel), archivedText(kRevocationsRel), in, &why)) return OpResult::fail(why);
    in.options = d.options;            // 1.42.0
    in.purchases = purchasesToWrite(d);
    in.alsoTrusted = alsoTrusted();    // 1.44.0
    std::string out, summary;
    if (!signLicense(*signer(), in, now(), out, &why, &summary)) return OpResult::fail(why);
    OpResult r = OpResult::done("La licence " + d.licenseId + " est pr\xC3\xAAte \xC3\xA0 \xC3\xAAtre sign\xC3\xA9" "e :");
    addSummary(r, summary);
    if (d.online)
        r.details.push_back("En ligne : un code d'activation neuf est tir\xC3\xA9 \xC3\xA0 la signature ; bail de " + std::to_string(d.leaseDays) +
                            " j (un PC qui ne se connecte plus s'arr\xC3\xAAte au bout).");
    return r;
}

namespace {

// 1.44.0 : la ligne du journal des admins d'une licence signee (creee, prolongee, une option, un PC retire), et celle de
// chaque demande en ligne acceptee par elle. Une ligne qui ne s'ecrit pas est dite (la licence, elle, est signee).
void noteLicence(const Workspace& w, OpResult& r, const std::string& action, const LicenceDraft& d, Seconds beforeEnd, Seconds afterEnd,
                 const std::vector<std::pair<int, std::string>>& accepted) {
    std::string summary;
    if (action == "licence_creer") summary = "a sign\xC3\xA9 la licence " + d.licenseId;
    else if (action == "licence_prolonger") summary = "a prolong\xC3\xA9 la licence " + d.licenseId;
    else if (action == "licence_option") summary = "a ajout\xC3\xA9 une option \xC3\xA0 la licence " + d.licenseId;
    else summary = "a retir\xC3\xA9 un PC de la licence " + d.licenseId;
    core::json::Value detail = core::json::Value::object();
    detail.set("client", d.customer);
    detail.set("offre", d.edition);
    if (d.online) detail.set("en_ligne", true);
    if (!d.dropDevices.empty()) detail.set("pc", d.dropDevices.front().substr(0, 16));
    if (d.options && !d.options->empty()) detail.set("option", d.options->back().label);
    core::json::Value before = core::json::Value::object(), after = core::json::Value::object();
    if (action == "licence_prolonger") {
        before.set("fin", beforeEnd ? licensing::isoDate(beforeEnd) : std::string("sans fin"));
        after.set("fin", afterEnd ? licensing::isoDate(afterEnd) : std::string("sans fin"));
    }
    OpResult j = w.journal(action, d.licenseId, summary, core::json::write(detail, false), core::json::write(before, false), core::json::write(after, false));
    if (!j.ok) r.details.push_back("ATTENTION : journal des admins : " + j.message);
    for (const auto& [number, customer] : accepted) {
        core::json::Value a = core::json::Value::object();
        a.set("demande", number);
        a.set("client", customer);
        a.set("licence", d.licenseId);
        j = w.journal("demande_accepter", "demande " + std::to_string(number), "a accept\xC3\xA9 la demande n\xC2\xB0 " + std::to_string(number) + " (" + customer + ")",
                      core::json::write(a, false));
        if (!j.ok) r.details.push_back("ATTENTION : journal des admins : " + j.message);
    }
}

} // namespace

OpResult Workspace::signLicence(const LicenceDraft& d, const std::string& outPath) {
    if (const auto deny = denied(licenceRight(d))) return *deny;   // 1.44.0 : creer, prolonger, une option, un PC retire
    // 1.44.0 : repondre a une demande en ligne demande aussi le droit << demandes >>
    const bool answers = std::any_of(d.requestPaths.begin(), d.requestPaths.end(), [this](const std::string& p) {
        const online::Entry* e = onlineRequestAt(p);
        return e && e->waiting();
    });
    if (answers)
        if (const auto deny = denied("demandes")) return *deny;
    if (!hasRegistry_) return OpResult::fail("pas de registre sign\xC3\xA9 dans l'archive (\xC3\x89" "ditions \xE2\x80\xBA Signer le registre)");
    if (outPath.empty()) return OpResult::fail("o\xC3\xB9 \xC3\xA9" "crire le fichier de licence ?");
    LicenceDraft bound = d;
    LicenseInputs in;
    std::string why;
    if (d.online && !d.extend) return OpResult::fail("une licence en ligne se signe avec son code (Nouvelle licence en ligne)");
    if (!inputsOf(*this, bound, archivedText(kRegistryRel), archivedText(kRevocationsRel), in, &why)) return OpResult::fail(why);
    in.options = d.options;            // 1.42.0
    in.purchases = purchasesToWrite(d);
    in.alsoTrusted = alsoTrusted();    // 1.44.0
    std::string out, summary;
    std::vector<ActivationRecord> records;
    const LicenceEntry* before = d.extend ? licence(d.licenseId) : nullptr;
    const Seconds beforeEnd = before ? before->licence.expiresAt : 0;
    const std::string action = licenceRight(d);
    if (!signLicense(*signer(), in, now(), out, &why, &summary, &records)) return OpResult::fail(why);
    {
        std::error_code ec;   // 1.34.0 : demandes-en-ligne/licences/ (une demande en ligne : le fichier sans rien demander)
        fs::create_directories(P(outPath).parent_path(), ec);
    }
    if (!writeText(outPath, withAdminList(out), &why)) return OpResult::fail(why);   // 1.44.0 : un admin : la liste d'abord
    OpResult r = OpResult::done("Licence " + d.licenseId + " sign\xC3\xA9" "e : " + outPath);
    std::vector<std::pair<int, std::string>> accepted;   // 1.44.0 : le journal (les demandes acceptees)
    for (const auto& p : d.requestPaths)
        if (const online::Entry* e = onlineRequestAt(p); e && e->waiting()) accepted.emplace_back(e->number, e->customer);
    // 1.37.0 : UNE SEULE ACCEPTATION - les demandes en ligne auxquelles elle repond sont << acceptees ici >> des maintenant,
    // meme si la reponse ne part pas tout de suite (Demandes en ligne : Renvoyer la reponse).
    for (const auto& p : d.requestPaths)
        if (const online::Entry* e = onlineRequestAt(p); e && e->waiting()) {
            std::string w;
            if (!online::markAcceptedHere(folder_, e->number, d.licenseId, &w)) r.details.push_back("ATTENTION : " + w);
        }
    addSummary(r, summary);
    if (signer()->test()) r.details.push_back("!!! sign\xC3\xA9" "e avec une CL\xC3\x89 D'ESSAI : jamais pour un client !!!");
    if (role_ == Role::Admin) r.details.push_back("Sign\xC3\xA9" "e par ta cl\xC3\xA9 d'admin : le fichier porte aussi la liste des admins (un Studio hors ligne la lit pour la croire).");
    licensing::License signedLicence;
    SignedBlock lb;
    const bool online = blockOf(out, DocKind::License, lb, nullptr) && licensing::parseLicense(lb.document, signedLicence, nullptr) && isOnlineLicence(signedLicence);
    noteLicence(*this, r, action, d, beforeEnd, signedLicence.expiresAt, accepted);
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
    if (const auto deny = denied(licenceRight(draft))) return *deny;   // 1.44.0
    if (requestNumber > 0)
        if (const auto deny = denied("demandes")) return *deny;
    if (!hasRegistry_) return OpResult::fail("pas de registre sign\xC3\xA9 dans l'archive (\xC3\x89" "ditions \xE2\x80\xBA Signer le registre)");
    if (!draft.online) return OpResult::fail("ce n'est pas une licence en ligne");
    if (draft.leaseDays < 1 || draft.leaseDays > 366) return OpResult::fail("le bail d'une activation va de 1 \xC3\xA0 366 jours");
    if (!draft.extend && licence(draft.licenseId))
        return OpResult::fail("la licence " + draft.licenseId + " est d\xC3\xA9j\xC3\xA0 dans l'archive : choisis un autre identifiant");
    LicenseInputs in;
    std::string why;
    if (!inputsOf(*this, draft, archivedText(kRegistryRel), archivedText(kRevocationsRel), in, &why)) return OpResult::fail(why);
    in.options = draft.options;        // 1.42.0
    in.purchases = purchasesToWrite(draft);
    in.alsoTrusted = alsoTrusted();    // 1.44.0
    std::string out, summary;
    std::vector<ActivationRecord> records;
    const LicenceEntry* before = draft.extend ? licence(draft.licenseId) : nullptr;
    const Seconds beforeEnd = before ? before->licence.expiresAt : 0;
    const std::string action = licenceRight(draft);
    if (!signLicense(*signer(), in, now(), out, &why, &summary, &records)) return OpResult::fail(why);
    if (!outPath.empty()) {
        std::error_code ec;
        fs::create_directories(P(outPath).parent_path(), ec);
        if (!writeText(outPath, withAdminList(out), &why)) return OpResult::fail(why);
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
    if (signer()->test()) r.details.push_back("!!! sign\xC3\xA9" "e avec une CL\xC3\x89 D'ESSAI : jamais pour un client !!!");
    if (!outPath.empty()) r.output = outPath;
    {
        licensing::License signedLicence;
        SignedBlock lb;
        if (blockOf(out, DocKind::License, lb, nullptr)) (void)licensing::parseLicense(lb.document, signedLicence, nullptr);
        std::vector<std::pair<int, std::string>> accepted;
        if (requestNumber > 0) {
            const online::Entry* e = onlineRequest(requestNumber);
            accepted.emplace_back(requestNumber, e ? e->customer : draft.customer);
        }
        noteLicence(*this, r, action, draft, beforeEnd, signedLicence.expiresAt, accepted);
    }
    return archiveAndNote(out, records, std::move(r));
}

OpResult Workspace::prepareNewCode(const std::string& licenseId, ServiceJob& job, std::string& code) {
    code.clear();
    if (const auto deny = denied("licence_creer")) return *deny;   // 1.44.0 : un code neuf (ce que la creation donne)
    const LicenceEntry* l = licence(licenseId);
    if (!l) return OpResult::fail("la licence " + licenseId + " n'est pas dans l'archive");
    if (!isOnlineLicence(l->licence)) return OpResult::fail("la licence " + licenseId + " est li\xC3\xA9" "e \xC3\xA0 des PC : elle n'a pas de code d'activation");
    if (!service_.ready()) return OpResult::fail("le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne)");
    std::string text, why;
    if (!readText(l->path, text, &why)) return OpResult::fail(why);
    {   // 1.44.0 : le service ne prend que la licence signee par la cle qui la depose
        SignedBlock b;
        if (blockOf(text, DocKind::License, b, nullptr) && signer() && b.keyId != signer()->keyId())
            return OpResult::fail("la licence " + licenseId + " est sign\xC3\xA9" "e par une autre cl\xC3\xA9 (" + b.keyId +
                                  ") : prolonge-la d'abord (une nouvelle \xC3\xA9mission sign\xC3\xA9" "e par ta cl\xC3\xA9), puis tire un code neuf");
    }
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
    (void)journal("licence_creer", licenseId, "a tir\xC3\xA9 un code d'activation neuf pour la licence " + licenseId);   // 1.44.0
    return OpResult::done("Un code neuf pour la licence " + licenseId + " : il remplace l'ancien d\xC3\xA8s qu'il est d\xC3\xA9pos\xC3\xA9.");
}

OpResult Workspace::deleteRevokedLicence(const std::string& licenseId) {
    if (const auto deny = denied("licence_revoquer")) return *deny;   // 1.44.0
    const LicenceEntry* e = licence(licenseId);
    if (!e) return OpResult::fail("la licence " + licenseId + " n'est pas dans l'archive");
    if (!e->revoked)
        return OpResult::fail("la licence " + licenseId + " n'est pas r\xC3\xA9voqu\xC3\xA9" "e : seule une licence r\xC3\xA9voqu\xC3\xA9" "e se supprime (R\xC3\xA9voquer\xE2\x80\xA6 d'abord)");
    const Seconds t = now();
    const Seconds issued = e->licence.issuedAt;
    const std::string from = e->path;
    // La corbeille : jamais effacee (le nom porte l'heure : deux suppressions de la meme licence ne s'ecrasent pas).
    const fs::path trash = P(deploy::joinPath(folder_, archive::kTrashDir));
    std::error_code ec;
    fs::create_directories(trash, ec);
    std::string stamp = licensing::isoTime(t);
    for (char& ch : stamp)
        if (ch == ':') ch = '-';
    const fs::path to = trash / P(archive::safeName(licenseId) + "-" + stamp + ".xpglic");
    std::string why;
    if (!archive::noteDeleted(folder_, {licenseId, issued, t}, &why)) return OpResult::fail("la suppression n'a pas pu \xC3\xAAtre not\xC3\xA9" "e : " + why);
    fs::rename(P(from), to, ec);
    if (ec) {
        fs::copy_file(P(from), to, fs::copy_options::overwrite_existing, ec);
        if (!ec) fs::remove(P(from), ec);
    }
    if (ec) return OpResult::fail("le fichier " + from + " n'a pas pu aller dans la corbeille : " + ec.message());
    loadArchive();
    OpResult r = OpResult::done("Licence " + licenseId + " supprim\xC3\xA9" "e de l'archive.");
    r.details.push_back("Son fichier est dans la corbeille de l'outil : " + U(to) + " (rien n'est effac\xC3\xA9).");
    r.details.push_back("Sa r\xC3\xA9vocation reste dans la liste sign\xC3\xA9" "e : les Studios la refusent toujours.");
    r.details.push_back("La synchro de l'archive ne la ram\xC3\xA8ne pas sur ce poste (l'archive en ligne en garde une copie chiffr\xC3\xA9" "e).");
    return r;
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
    if (const auto deny = denied("licence_revoquer")) return *deny;   // 1.44.0
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
    if (!signRevocations(*signer(), core::json::write(root), now(), out, &why, &records)) return OpResult::fail(why);
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
    if (r.ok) {   // 1.44.0 : le journal des admins
        core::json::Value detail = core::json::Value::object();
        detail.set("raison", reason);
        if (!deviceKey.empty()) detail.set("pc", deviceKey.substr(0, 16));
        if (onlyThisIssue) detail.set("emissions", "jusqu'ici");
        if (const LicenceEntry* l = licence(licenseId)) detail.set("client", l->licence.customer);
        const OpResult j = journal("licence_revoquer", licenseId,
                                   deviceKey.empty() ? "a r\xC3\xA9voqu\xC3\xA9 la licence " + licenseId : "a r\xC3\xA9voqu\xC3\xA9 un PC de la licence " + licenseId,
                                   core::json::write(detail, false));
        if (!j.ok) r.details.push_back("ATTENTION : journal des admins : " + j.message);
    }
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
    if (r.ok) {   // 1.44.0
        const OpResult j = journal("licence_revoquer", removed.licenseId, "a retir\xC3\xA9 la r\xC3\xA9vocation de la licence " + removed.licenseId);
        if (!j.ok) r.details.push_back("ATTENTION : journal des admins : " + j.message);
    }
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
namespace {
// 1.44.0 : les lignes du journal d'un registre signe (une par partie qui change : offres, options, annonces).
void noteRegistry(const Workspace& w, OpResult& r, const std::vector<std::string>& parts, std::uint64_t number) {
    const std::vector<std::string> list = parts.empty() ? std::vector<std::string>{"offres"} : parts;
    for (const auto& part : list) {
        const std::string what = part == "options" ? "les options" : part == "annonces" ? "les annonces" : "les offres";
        core::json::Value detail = core::json::Value::object();
        detail.set("registre", static_cast<std::uint64_t>(number));
        const OpResult j = w.journal(part, "registre " + std::to_string(number), "a modifi\xC3\xA9 " + what + " (registre n\xC2\xB0 " + std::to_string(number) + ")",
                                     core::json::write(detail, false));
        if (!j.ok) r.details.push_back("ATTENTION : journal des admins : " + j.message);
    }
}
} // namespace

OpResult Workspace::signRegistry(const licensing::Registry& edited) {
    if (!signer() && role_ == Role::None) return OpResult::fail(key_.problem);
    // 1.44.0 : le droit de chaque partie qui change (offres, options, annonces), comme le service le verifie
    const std::vector<std::string> parts = detail::registryRights(hasRegistry_ ? &registry_ : nullptr, edited);
    for (const auto& right : parts)
        if (const auto deny = denied(right)) return *deny;
    if (parts.empty() && !can("offres") && !can("options") && !can("annonces"))
        if (const auto deny = denied("offres")) return *deny;
    licensing::Registry reg = edited;
    reg.number = hasRegistry_ ? registry_.number + 1 : std::max<std::uint64_t>(1, edited.number);
    reg.issuedAt = now();
    std::string out, why;
    if (!license_admin::signRegistry(*signer(), licensing::writeRegistry(reg), now(), out, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Registre n\xC2\xB0 " + std::to_string(reg.number) + " sign\xC3\xA9 (" + plural(static_cast<int>(reg.editions.size()), "\xC3\xA9" "dition", "\xC3\xA9" "ditions")
                                + ", " + plural(static_cast<int>(reg.features.size()), "fonctionnalit\xC3\xA9", "fonctionnalit\xC3\xA9s") + ").");
    r.details.push_back("Il part avec chaque licence sign\xC3\xA9" "e ensuite ; un client l'a \xC3\xA0 sa prochaine licence.");
    noteRegistry(*this, r, parts, reg.number);   // 1.44.0
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
    if (!signer() && role_ == Role::None) return OpResult::fail(key_.problem);
    if (!signer()) return *denied("offres");
    std::string text, out, why;
    if (!readText(path, text, &why)) return OpResult::fail(why);
    if (!license_admin::signRegistry(*signer(), text, now(), out, &why)) return OpResult::fail(why);
    SignedBlock b;
    licensing::Registry reg;
    if (blockOf(out, DocKind::Registry, b, &why)) (void)licensing::parseRegistry(b.document, reg, &why);
    // 1.44.0 : le droit de chaque partie qui change
    const std::vector<std::string> parts = detail::registryRights(hasRegistry_ ? &registry_ : nullptr, reg);
    for (const auto& right : parts)
        if (const auto deny = denied(right)) return *deny;
    if (hasRegistry_ && reg.number <= registry_.number)
        return OpResult::fail("ce registre porte le n\xC2\xB0 " + std::to_string(reg.number) + ", pas plus que celui de l'archive (n\xC2\xB0 " + std::to_string(registry_.number)
                              + ") : augmente \"number\"");
    OpResult r = OpResult::done("Registre n\xC2\xB0 " + std::to_string(reg.number) + " sign\xC3\xA9 depuis " + U(P(path).filename()) + ".");
    noteRegistry(*this, r, parts, reg.number);   // 1.44.0
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
    if (const auto deny = denied("clients_exporter")) return *deny;   // 1.44.0
    if (path.empty()) return OpResult::fail("o\xC3\xB9 \xC3\xA9" "crire le tableau ?");
    std::string why;
    if (!writeText(path, devicesCsv(list), &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Tableau \xC3\xA9" "crit pour Excel : " + path + " (" + plural(static_cast<int>(list.size()), "PC", "PC") + ").");
    {   // 1.44.0
        core::json::Value detail = core::json::Value::object();
        detail.set("lignes", static_cast<int>(list.size()));
        const OpResult j = journal("clients_exporter", "CSV", "a export\xC3\xA9 les clients (" + plural(static_cast<int>(list.size()), "PC", "PC") + ")",
                                   core::json::write(detail, false));
        if (!j.ok) r.details.push_back("ATTENTION : journal des admins : " + j.message);
    }
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

} // namespace

OpResult Workspace::serviceConfigure(const std::string& url) {
    // 1.47.0 : changer l'adresse, c'est gerer le service (l'admin+, ou une derogation) ; un poste sans role ou en attente
    // la tape encore s'il n'a recu aucune connexion signee (sa demande d'admin en a besoin).
    if ((role_ == Role::Admin || role_ == Role::Retired) && !mayManageService()) return OpResult::fail(whyNotService());
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
    if (role_ != Role::Plus && role_ != Role::None)   // 1.44.0 : un admin recoit celle de l'admin+ (le trousseau), jamais une autre
        return OpResult::fail("la cl\xC3\xA9 de synchro est celle de l'admin+ : tu la re\xC3\xA7ois par le trousseau d\xC3\xA8s que tu es admin "
                              "(ou par son code de secours) ; une autre rendrait l'archive illisible");
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

// Les fonctions lentes relisent la cle (une copie a ce fil : le Workspace n'est pas partage). 1.44.0 : la cle qui signe
// (celle de l'editeur pour l'admin+, celle de l'admin sinon) et ce qu'elle croit (detail::loadCaller).
#define XPG_SERVICE_KEY_OR_FAIL                                                           \
    std::string why;                                                                      \
    detail::Caller caller;                                                                \
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);           \
    const licensing::SigningKey* const key = caller.key.get()
// 1.44.0 : le droit qu'il faut (vide : l'admin+ seulement)
#define XPG_SERVICE_RIGHT(right)                                                          \
    if (const auto deny_ = detail::callerDenied(caller, right)) return *deny_

OpResult Workspace::serviceVerify(online::ServiceState* state) const {
    XPG_SERVICE_KEY_OR_FAIL;
    return fromResult(online::verify(folder_, *key, detail::optionsFor(caller, now()), state));
}

OpResult Workspace::serviceReadKey(std::string& keyHex) const {
    XPG_SERVICE_KEY_OR_FAIL;
    XPG_SERVICE_RIGHT({});   // 1.44.0 : le certificat du service est a l'admin+
    return fromResult(online::readServiceKey(folder_, *key, detail::optionsFor(caller, now()), keyHex));
}

OpResult Workspace::serviceCertify(const std::string& expectedKey) const {
    XPG_SERVICE_KEY_OR_FAIL;
    XPG_SERVICE_RIGHT({});
    return fromResult(online::certify(folder_, *key, detail::optionsFor(caller, now()), expectedKey));
}

archive::SyncReport Workspace::archiveSync(bool receiveOnly, bool dryRun) const {
    archive::SyncOptions o;
    o.receiveOnly = receiveOnly;
    o.dryRun = dryRun;
    o.now = now();
    o.tool = std::string(kAppName) + " " + kAppVersion;
    std::string why;
    detail::Caller caller;
    archive::SyncReport r;
    if (!detail::loadCaller(folder_, caller, &why)) {
        r.why = why;
        return r;
    }
    if (caller.role != Role::Plus && caller.role != Role::Admin) {   // 1.44.0 : l'admin+ ou un admin de la liste
        const auto deny = detail::callerDenied(caller, "demandes");
        r.why = deny ? deny->message.substr(deny->message.find(':') + 2) : std::string("ce poste n'est pas admin");
        return r;
    }
    o.editorKeys = caller.editorKeys;
    o.admins = caller.admins;
    r = archive::synchronize(folder_, *caller.key, o);
    if (r.ok) (void)detail::flushJournalQuiet(folder_, caller, now());   // 1.44.0 : le journal a envoyer part aussi
    return r;
}

OpResult Workspace::serviceListLicences(online::Snapshot& out) const {
    XPG_SERVICE_KEY_OR_FAIL;
    return fromResult(online::listLicences(folder_, *key, detail::optionsFor(caller, now()), out));
}

OpResult Workspace::serviceRetire(const std::string& licenseId, const std::string& deviceKey) const {
    XPG_SERVICE_KEY_OR_FAIL;
    XPG_SERVICE_RIGHT("poste_retirer");
    OpResult r = fromResult(online::retireDevice(folder_, *key, licenseId, deviceKey, detail::optionsFor(caller, now())));
    if (r.ok) {   // 1.44.0
        core::json::Value d = core::json::Value::object();
        d.set("pc", deviceKey.substr(0, 16));
        (void)detail::queueJournal(folder_, *key, now(), "poste_retirer", licenseId, "a retir\xC3\xA9 un PC de la licence en ligne " + licenseId,
                                   core::json::write(d, false), {}, {}, nullptr);
        (void)detail::flushJournalQuiet(folder_, caller, now());
    }
    return r;
}

OpResult Workspace::serviceSetActive(const std::string& licenseId, bool active) const {
    XPG_SERVICE_KEY_OR_FAIL;
    XPG_SERVICE_RIGHT("licence_revoquer");   // 1.44.0 : desactiver une licence en ligne, c'est la revoquer
    OpResult r = fromResult(online::setActive(folder_, *key, licenseId, active, 0, detail::optionsFor(caller, now())));
    if (r.ok) {
        (void)detail::queueJournal(folder_, *key, now(), "licence_revoquer", licenseId,
                                   (active ? "a r\xC3\xA9" "activ\xC3\xA9" " la licence en ligne " : "a d\xC3\xA9" "sactiv\xC3\xA9" " la licence en ligne ") + licenseId, {}, {}, {},
                                   nullptr);
        (void)detail::flushJournalQuiet(folder_, caller, now());
    }
    return r;
}

OpResult Workspace::serviceFetch() const {
    XPG_SERVICE_KEY_OR_FAIL;
    XPG_SERVICE_RIGHT("demandes");
    return fromResult(online::fetchRequests(folder_, *key, detail::optionsFor(caller, now())));
}

OpResult Workspace::serviceRefuse(const std::string& which, const std::string& reason) const {
    XPG_SERVICE_KEY_OR_FAIL;
    XPG_SERVICE_RIGHT("demandes");
    online::Entry e;
    std::string w;
    const bool found = online::findWaiting(folder_, which, e, &w);
    OpResult r = fromResult(online::refuse(folder_, *key, which, reason, detail::optionsFor(caller, now())));
    if (r.ok) {   // 1.44.0
        core::json::Value d = core::json::Value::object();
        d.set("raison", reason.substr(0, 300));
        if (found) d.set("client", e.customer);
        const std::string object = found ? "demande " + std::to_string(e.number) : "demande " + which;
        (void)detail::queueJournal(folder_, *key, now(), "demande_refuser", object,
                                   "a refus\xC3\xA9 la " + object + (found && !e.customer.empty() ? " (" + e.customer + ")" : std::string()),
                                   core::json::write(d, false), {}, {}, nullptr);
        (void)detail::flushJournalQuiet(folder_, caller, now());
    }
    return r;
}

OpResult Workspace::serviceAnswerOption(const std::string& which, const std::string& licenseId, const std::string& message) const {   // 1.42.0
    XPG_SERVICE_KEY_OR_FAIL;
    XPG_SERVICE_RIGHT("demandes");
    OpResult r = fromResult(online::answerOption(folder_, *key, which, licenseId, message, detail::optionsFor(caller, now())));
    if (r.ok) {   // 1.44.0
        (void)detail::queueJournal(folder_, *key, now(), "demande_accepter", "demande " + which,
                                   "a accept\xC3\xA9 la demande d'option " + which + " (licence " + licenseId + ")", {}, {}, {}, nullptr);
        (void)detail::flushJournalQuiet(folder_, caller, now());
    }
    return r;
}

OpResult Workspace::serviceStats() const {
    XPG_SERVICE_KEY_OR_FAIL;
    return fromResult(online::stats(folder_, *key, detail::optionsFor(caller, now())));
}

OpResult Workspace::serviceWatch(online::AdminWatch& out) const {
    XPG_SERVICE_KEY_OR_FAIL;
    OpResult r = fromResult(online::watchState(folder_, *key, detail::optionsFor(caller, now()), out));
    if (r.ok) (void)detail::flushJournalQuiet(folder_, caller, now());   // 1.44.0 : toutes les 15 s, le journal a envoyer part
    return r;
}

OpResult Workspace::serviceRun(const ServiceJob& job) const {
    XPG_SERVICE_KEY_OR_FAIL;
    const online::Options o = detail::optionsFor(caller, now());
    OpResult r = OpResult::fail("publication inconnue");
    switch (job.kind) {
        case ServiceJob::Kind::Licence: r = fromResult(online::publishBoundLicence(folder_, *key, job.licenceText, false, o, mailer())); break;
        case ServiceJob::Kind::Removal: r = fromResult(online::publishRemoval(folder_, *key, job.licenceText, job.deviceKey, o)); break;
        case ServiceJob::Kind::Revocation: r = fromResult(online::publishRevocation(folder_, *key, job.licenseId, job.deviceKeys, job.reason, o)); break;
        case ServiceJob::Kind::Registry: {
            r = fromResult(online::publishRegistry(folder_, *key, o));
            // 1.46.1 : la boutique suit le registre (un prix, une reduction) - l'admin+ seul ; ni un echec, ni un service sans
            // boutique ne font echouer le depot.
            if (r.ok && caller.role == Role::Plus && hasRegistry_) {
                shop::Settings s;
                if (shop::readSettings(folder_, *key, o, s).ok && shop::followRegistry(s, registry_, now()) > 0) {
                    const online::Result w = shop::saveSettings(folder_, *key, o, s);
                    r.details.push_back(w.ok ? "La boutique suit le registre : " + w.message : "La boutique ne suit pas encore le registre : " + w.message);
                }
            }
            break;
        }
        case ServiceJob::Kind::Unrevoke: {
            r = fromResult(online::publishRevocations(folder_, *key, o));
            if (!r.ok || job.licenceText.empty()) break;
            // La liste qui ne revoque plus cette licence part scellee a chacun de ses PC lies.
            OpResult l = fromResult(online::publishBoundLicence(folder_, *key, job.licenceText, true, o));
            l.details.insert(l.details.begin(), r.message);
            r = std::move(l);
            break;
        }
        case ServiceJob::Kind::OnlineLicence: return serviceRunOnline(job, {});
    }
    if (r.ok) (void)detail::flushJournalQuiet(folder_, caller, now());   // 1.44.0 : la ligne du journal de l'operation part avec elle
    return r;
}

OpResult Workspace::serviceRunOnline(const ServiceJob& job, const std::string& code) const {
    XPG_SERVICE_KEY_OR_FAIL;
    const online::Options o = detail::optionsFor(caller, now());
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
    if (r.ok) (void)detail::flushJournalQuiet(folder_, caller, now());   // 1.44.0
    return r;
}

OpResult Workspace::serviceResendAnswer(int number) const {
    XPG_SERVICE_KEY_OR_FAIL;
    XPG_SERVICE_RIGHT("demandes");
    const online::Entry* e = onlineRequest(number);
    if (!e || !e->acceptedHere())
        return OpResult::fail("la demande n\xC2\xB0 " + std::to_string(number) + " n'a pas de r\xC3\xA9ponse en attente sur ce poste (rel\xC3\xA8ve les demandes)");
    const std::string licenseId = e->answer;
    const LicenceEntry* l = licence(licenseId);
    std::string text;
    why.clear();
    if (!l || !readText(l->path, text, &why))
        return OpResult::fail("la licence " + licenseId + " n'est pas dans l'archive de ce poste" + (why.empty() ? std::string() : " (" + why + ")"));
    const online::Options o = detail::optionsFor(caller, now());
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
#undef XPG_SERVICE_RIGHT

void Workspace::adoptServiceState(const online::ServiceState& state) {
    // 1.47.0 : le certificat verifie (signe par l'editeur, cette adresse, cette cle) : la connexion a exporter
    if (state.certOk && !state.certBlock.empty()) (void)connexion::keepCertificate(folder_, state.certBlock, nullptr);
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

// ======================================================== 1.42.0 : offres, clients, options, achats ==
namespace {

constexpr const char* kDeletedClientsFile = "clients-supprimes.json";
constexpr const char* kOffersToDeleteFile = "offres-a-supprimer.json";

// "1 semaine", "1 mois", "1 an", "45 jours" ; until 0 : "sans fin".
std::string durationText(Seconds from, Seconds until) {
    if (!until) return "sans fin";
    const long long days = static_cast<long long>((until - from + licensing::kDay / 2) / licensing::kDay);
    if (days == 7) return "1 semaine";
    if (days > 7 && days < 28 && days % 7 == 0) return std::to_string(days / 7) + " semaines";
    if (days >= 28 && days <= 31) return "1 mois";
    if (days >= 365 && days <= 366) return "1 an";
    if (days >= 730 && days <= 731) return "2 ans";
    if (days >= 1095 && days <= 1096) return "3 ans";
    return std::to_string(days) + (days == 1 ? " jour" : " jours");
}

const char* const kDot = " \xC2\xB7 ";

// La meme ligne d'achat (a la journee pres : une ligne derivee du registre des activations et celle ecrite dans la licence).
std::string purchaseKey(const licensing::Purchase& p) {
    return p.licenseId + "|" + p.kind + "|" + (p.kind == "option" ? p.label : std::string()) + "|" + std::to_string(p.from / licensing::kDay) + "|" +
           std::to_string(p.until / licensing::kDay);
}

licensing::Purchase optionPurchase(const licensing::LicenseOption& o, const licensing::License& l, Seconds at) {
    licensing::Purchase p;
    p.at = at;
    p.kind = "option";
    p.licenseId = l.licenseId;
    p.from = o.from;
    p.until = o.until ? o.until : l.expiresAt;
    p.label = o.label + kDot + (o.until ? durationText(o.from, o.until) : std::string("jusqu'\xC3\xA0 la fin de la licence"));
    p.price = o.gift ? (o.price.empty() ? std::string("offerte") : o.price) : o.price;
    if (p.label.size() > 120) p.label.resize(120);
    return p;
}

std::vector<DeletedClient> readDeletedClients(const std::string& folder) {
    std::vector<DeletedClient> out;
    std::string text, w;
    if (!readText(deploy::joinPath(folder, kDeletedClientsFile), text, &w)) return out;
    core::json::Value v;
    if (!core::json::parse(text, v, nullptr) || !v.isObject()) return out;
    if (const core::json::Value* arr = v.find("clients"); arr && arr->isArray())
        for (const auto& x : arr->items()) {
            DeletedClient d;
            d.name = x.str("nom");
            (void)licensing::parseIsoTime(x.str("supprime_le"), d.deletedAt);
            if (const core::json::Value* l = x.find("licences"); l && l->isArray())
                for (const auto& i : l->items())
                    if (i.isString()) d.licenceIds.push_back(i.asString());
            if (const core::json::Value* f = x.find("fichiers"); f && f->isArray())
                for (const auto& i : f->items())
                    if (i.isString()) d.trashFiles.push_back(i.asString());
            if (!d.name.empty()) out.push_back(std::move(d));
        }
    return out;
}

bool writeDeletedClients(const std::string& folder, const std::vector<DeletedClient>& list, std::string* why) {
    core::json::Value root = core::json::Value::object();
    root.set("format", "xpg-clients-supprimes/1");
    core::json::Value arr = core::json::Value::array();
    for (const auto& d : list) {
        core::json::Value o = core::json::Value::object();
        o.set("nom", d.name);
        o.set("supprime_le", licensing::isoTime(d.deletedAt));
        core::json::Value l = core::json::Value::array();
        for (const auto& id : d.licenceIds) l.push(id);
        o.set("licences", std::move(l));
        core::json::Value f = core::json::Value::array();
        for (const auto& p : d.trashFiles) f.push(p);
        o.set("fichiers", std::move(f));
        arr.push(std::move(o));
    }
    root.set("clients", std::move(arr));
    return writeText(deploy::joinPath(folder, kDeletedClientsFile), core::json::write(root) + "\n", why);
}

} // namespace

std::vector<std::string> Workspace::licencesInForce(const std::string& edition) const {
    std::vector<std::string> out;
    for (const auto& l : licences_)
        if (l.licence.edition == edition && inForce(l)) out.push_back(l.licence.licenseId);
    return out;
}

Seconds Workspace::lastLicenceEnd(const std::string& edition) const {
    Seconds end = 0;
    for (const auto& l : licences_) {
        if (l.licence.edition != edition || !inForce(l)) continue;
        if (l.licence.expiresAt == 0) return -1;
        end = std::max(end, l.licence.expiresAt);
    }
    return end;
}

std::vector<DeletedClient> Workspace::deletedClients() const { return readDeletedClients(folder_); }

// 1.44.0 : un client supprime ou restaure - une ligne << client >> au registre des activations (l'historique, le tableau
// de bord) et une au journal des admins (client_supprimer, client_restaurer). Ce qui ne s'ecrit pas est dit.
void Workspace::noteClient(OpResult& r, const std::string& action, const std::string& name, const std::vector<std::string>& licenceIds) {
    const bool deleted = action == "supprime";
    ActivationRecord rec;
    rec.kind = "client";
    rec.at = now();
    rec.customer = name;
    rec.action = action;
    rec.licences = licenceIds;
    rec.by = role_ == Role::Admin && me() ? me()->name : std::string("admin+");
    std::string why;
    if (!appendActivations(folder_, {rec}, &why)) r.details.push_back("ATTENTION : l'historique n'a pas \xC3\xA9t\xC3\xA9 mis \xC3\xA0 jour (" + why + ").");
    loadActivations();
    core::json::Value detail = core::json::Value::object();
    core::json::Value ids = core::json::Value::array();
    for (const auto& id : licenceIds) ids.push(id);
    detail.set("licences", std::move(ids));
    const OpResult j = journal(deleted ? "client_supprimer" : "client_restaurer", name,
                               (deleted ? "a supprim\xC3\xA9 le client " : "a restaur\xC3\xA9 le client ") + name, core::json::write(detail, false));
    if (!j.ok) r.details.push_back("ATTENTION : journal des admins : " + j.message);
}

OpResult Workspace::deleteClient(const std::string& name) {
    if (const auto deny = denied("client_supprimer")) return *deny;   // 1.44.0
    const std::string k = lower(name);
    std::vector<const LicenceEntry*> mine;
    for (const auto& l : licences_)
        if (lower(l.licence.customer) == k) mine.push_back(&l);
    for (const LicenceEntry* l : mine)
        if (inForce(*l))
            return OpResult::fail(name + " a une licence en cours (" + l->licence.licenseId + ", " + l->licence.edition +
                                  (l->licence.expiresAt ? ", jusqu'au " + dateFr(l->licence.expiresAt) : std::string(", sans fin")) +
                                  ") : r\xC3\xA9voque-la, ou attends sa fin, pour supprimer le client");
    for (const auto& c : clients())
        if (lower(c.name) == k && c.pendingRequests > 0)
            return OpResult::fail(name + " a une demande qui attend ta d\xC3\xA9" "cision (Demandes) : accepte-la ou refuse-la d'abord");
    const Seconds t = now();
    DeletedClient d;
    d.name = name;
    d.deletedAt = t;
    const fs::path trash = P(deploy::joinPath(folder_, archive::kTrashDir));
    std::error_code ec;
    fs::create_directories(trash, ec);
    std::string stamp = licensing::isoTime(t);
    for (char& ch : stamp)
        if (ch == ':') ch = '-';
    std::string why;
    for (const LicenceEntry* l : mine) {
        const fs::path to = trash / P(archive::safeName(l->licence.licenseId) + "-" + stamp + ".xpglic");
        if (!archive::noteDeleted(folder_, {l->licence.licenseId, l->licence.issuedAt, t}, &why))
            return OpResult::fail("la suppression n'a pas pu \xC3\xAAtre not\xC3\xA9" "e : " + why);
        fs::rename(P(l->path), to, ec);
        if (ec) {
            fs::copy_file(P(l->path), to, fs::copy_options::overwrite_existing, ec);
            if (!ec) fs::remove(P(l->path), ec);
        }
        if (ec) return OpResult::fail("le fichier " + l->path + " n'a pas pu aller dans la corbeille : " + ec.message());
        d.licenceIds.push_back(l->licence.licenseId);
        d.trashFiles.push_back(U(to));
    }
    auto list = readDeletedClients(folder_);
    std::erase_if(list, [&](const DeletedClient& x) { return lower(x.name) == k; });
    list.push_back(d);
    if (!writeDeletedClients(folder_, list, &why)) return OpResult::fail("le client n'a pas pu \xC3\xAAtre not\xC3\xA9 supprim\xC3\xA9 : " + why);
    loadArchive();
    OpResult r = OpResult::done("Client " + name + " supprim\xC3\xA9.");
    noteClient(r, "supprime", name, d.licenceIds);   // 1.44.0 : l'historique et le journal des admins
    if (!d.licenceIds.empty())
        r.details.push_back(plural(static_cast<int>(d.licenceIds.size()), "licence", "licences") + " dans la corbeille de l'outil (rien n'est effac\xC3\xA9) ; la synchro ne les ram\xC3\xA8ne pas.");
    r.details.push_back("Pour le remettre : Clients \xE2\x80\xBA Corbeille \xE2\x80\xBA Restaurer.");
    return r;
}

OpResult Workspace::restoreClient(const std::string& name) {
    if (const auto deny = denied("client_supprimer")) return *deny;   // 1.44.0
    const std::string k = lower(name);
    auto list = readDeletedClients(folder_);
    auto it = std::find_if(list.begin(), list.end(), [&](const DeletedClient& x) { return lower(x.name) == k; });
    if (it == list.end()) return OpResult::fail(name + " n'est pas dans la corbeille des clients");
    const fs::path dir = P(archive::archiveFolder(folder_)) / kLicencesDir;
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string why;
    int back = 0;
    for (std::size_t i = 0; i < it->trashFiles.size(); ++i) {
        const std::string id = i < it->licenceIds.size() ? it->licenceIds[i] : std::string();
        if (id.empty()) continue;
        const fs::path to = dir / P(archive::safeName(id) + ".xpglic");
        if (fs::exists(to, ec)) continue;   // une emission plus recente est revenue entre-temps : elle reste
        fs::copy_file(P(it->trashFiles[i]), to, fs::copy_options::none, ec);
        if (ec) return OpResult::fail("la licence " + id + " n'a pas pu revenir de la corbeille : " + ec.message());
        if (!archive::forgetDeleted(folder_, id, &why)) return OpResult::fail(why);
        ++back;
    }
    const std::vector<std::string> ids = it->licenceIds;
    list.erase(it);
    if (!writeDeletedClients(folder_, list, &why)) return OpResult::fail(why);
    loadArchive();
    OpResult r = OpResult::done("Client " + name + " restaur\xC3\xA9.");
    noteClient(r, "restaure", name, ids);   // 1.44.0 : l'historique et le journal des admins
    if (back) r.details.push_back(plural(back, "licence revenue", "licences revenues") + " dans l'archive (la copie reste dans la corbeille).");
    return r;
}

std::vector<licensing::Purchase> Workspace::purchaseHistory(const std::string& customer) const {
    const std::string want = lower(customer);
    std::vector<licensing::Purchase> out;
    std::set<std::string> seen;
    const auto add = [&](licensing::Purchase p) {
        if (seen.insert(purchaseKey(p)).second) out.push_back(std::move(p));
    };
    // 1. ce que les licences portent deja (les prix d'alors)
    for (const auto& l : licences_)
        if (lower(l.licence.customer) == want)
            for (const auto& p : l.licence.purchases) add(p);
    // 2. le registre des activations : chaque signature d'une licence (une offre ; une prolongation quand la fin change)
    std::map<std::string, std::vector<const ActivationRecord*>> byLicence;
    for (const auto& r : records_)
        if (r.kind == "licence" && !r.licenseId.empty() && lower(r.customer) == want) byLicence[r.licenseId].push_back(&r);
    for (auto& [id, recs] : byLicence) {
        const auto signedOf = [](const ActivationRecord* r) { return r->signedAt ? r->signedAt : r->at; };
        std::stable_sort(recs.begin(), recs.end(), [&](const ActivationRecord* a, const ActivationRecord* b) { return signedOf(a) < signedOf(b); });
        std::string prevEdition;
        Seconds prevExpires = 0, lastSigned = -1;
        bool first = true;
        for (const ActivationRecord* r : recs) {
            const Seconds at = signedOf(r);
            if (at == lastSigned) continue;   // une ligne par PC lie : la meme signature
            lastSigned = at;
            const licensing::EditionDef* ed = registry_.edition(r->edition);
            const std::string edLabel = ed && !ed->label.empty() ? ed->label : r->edition;
            licensing::Purchase p;
            p.at = at;
            p.licenseId = id;
            p.until = r->expiresAt;
            p.price = ed ? ed->price : std::string();
            if (first || r->edition != prevEdition) {
                p.kind = "offer";
                p.from = r->issuedAt ? r->issuedAt : at;
                p.label = edLabel + kDot + durationText(p.from, p.until);
            } else if (r->expiresAt != prevExpires) {
                p.kind = "extension";
                p.from = prevExpires > at ? prevExpires : at;
                p.label = edLabel + " prolong\xC3\xA9" "e" + kDot + durationText(p.from, p.until);
            } else {
                continue;   // un PC retire, une option ajoutee : pas un achat de l'offre
            }
            first = false;
            prevEdition = r->edition;
            prevExpires = r->expiresAt;
            add(std::move(p));
        }
    }
    // 3. les options des licences
    for (const auto& l : licences_)
        if (lower(l.licence.customer) == want)
            for (const auto& o : l.licence.options) add(optionPurchase(o, l.licence, o.from));
    std::stable_sort(out.begin(), out.end(), [](const licensing::Purchase& a, const licensing::Purchase& b) { return a.at < b.at; });
    return out;
}

std::optional<std::vector<licensing::Purchase>> Workspace::purchasesToWrite(const LicenceDraft& d) const {
    if (!hasRegistry_ || !registry_.usesFormat142()) return std::nullopt;   // la 1.41 relit la licence (rien de nouveau)
    std::vector<licensing::Purchase> list = purchaseHistory(d.customer);
    std::optional<licensing::Purchase> line = d.purchase;
    if (!line) {
        const LicenceEntry* old = d.extend ? licence(d.licenseId) : nullptr;
        const licensing::EditionDef* ed = registry_.edition(d.edition);
        const Seconds t = now();
        const licensing::LicenseType type = d.type ? *d.type : (ed ? ed->type : licensing::LicenseType::Temporary);
        Seconds expires = 0;
        if (type != licensing::LicenseType::Perpetual) {
            if (d.expiresAt > 0) expires = d.expiresAt;
            else if (d.durationDays > 0) expires = t + static_cast<Seconds>(d.durationDays) * licensing::kDay;
            else if (d.durationDays < 0 && ed && ed->durationDays > 0) expires = t + static_cast<Seconds>(ed->durationDays) * licensing::kDay;
        }
        const std::string edLabel = ed && !ed->label.empty() ? ed->label : d.edition;
        licensing::Purchase p;
        p.at = t;
        p.licenseId = d.licenseId;
        p.until = expires;
        p.price = ed ? (!ed->promoPrice.empty() && t < ed->promoUntil ? ed->promoPrice : ed->price) : std::string();
        if (!old || old->licence.edition != d.edition) {
            p.kind = "offer";
            p.from = t;
            p.label = edLabel + kDot + durationText(p.from, p.until);
            line = p;
        } else if (expires != old->licence.expiresAt) {
            p.kind = "extension";
            p.from = old->licence.expiresAt > t ? old->licence.expiresAt : t;
            p.label = edLabel + " prolong\xC3\xA9" "e" + kDot + durationText(p.from, p.until);
            line = p;
        }
    }
    if (line) {
        if (line->label.size() > 120) line->label.resize(120);
        const std::string key = purchaseKey(*line);
        if (std::none_of(list.begin(), list.end(), [&](const licensing::Purchase& x) { return purchaseKey(x) == key; })) list.push_back(*line);
    }
    std::stable_sort(list.begin(), list.end(), [](const licensing::Purchase& a, const licensing::Purchase& b) { return a.at < b.at; });
    if (list.size() > 400) list.erase(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(list.size() - 400));
    return list;
}

OpResult Workspace::addOption(const std::string& licenseId, const OptionDraft& od, const std::string& outPath) {
    if (const auto deny = denied("licence_option")) return *deny;   // 1.44.0
    if (!hasRegistry_) return OpResult::fail("pas de registre sign\xC3\xA9 dans l'archive (Offres \xE2\x80\xBA Signer le registre)");
    const LicenceEntry* e = licence(licenseId);
    if (!e) return OpResult::fail("la licence " + licenseId + " n'est pas dans l'archive");
    if (!inForce(*e)) return OpResult::fail("la licence " + licenseId + " est " + e->stateLabel() + " : une option s'ajoute \xC3\xA0 une licence en cours");
    const licensing::OptionDef* opt = registry_.option(od.optionId);
    if (!opt) return OpResult::fail("l'option " + od.optionId + " n'est pas dans le registre sign\xC3\xA9 (Options \xE2\x80\xBA Signer le registre)");
    if (opt->retired) return OpResult::fail("l'option " + opt->label + " est retir\xC3\xA9" "e du catalogue");
    const licensing::License& L = e->licence;
    const Seconds t = now();
    if (t <= L.issuedAt) return OpResult::fail("la licence " + licenseId + " vient d'\xC3\xAAtre sign\xC3\xA9" "e : r\xC3\xA9" "essaie dans une seconde");
    licensing::LicenseOption o;
    o.id = opt->id;
    o.label = opt->label;
    o.features = opt->features;
    o.from = od.from ? od.from : t;
    o.until = od.until ? od.until : (od.days > 0 ? o.from + static_cast<Seconds>(od.days) * licensing::kDay : 0);
    o.gift = od.gift;
    if (o.until && o.until <= o.from) return OpResult::fail("l'option finirait avant de commencer : choisis d'autres dates");
    std::string note;
    if (L.expiresAt && o.from >= L.expiresAt) return OpResult::fail("la licence " + licenseId + " finit le " + dateFr(L.expiresAt) + " : l'option commencerait apr\xC3\xA8s (prolonge d'abord la licence)");
    if (L.expiresAt && o.until > L.expiresAt) {
        o.until = L.expiresAt;
        note = "L'option s'arr\xC3\xAAte avec la licence, le " + dateFr(L.expiresAt) + " (prolonge la licence pour aller plus loin).";
    }
    if (!od.price.empty()) o.price = od.price;
    else if (!od.gift) {
        const int days = od.until ? -1 : od.days;
        for (const auto& dur : opt->durations)
            if (dur.days == days) o.price = dur.price;
    }
    if (o.price.size() > 60) o.price.resize(60);
    LicenceDraft d;
    d.licenseId = L.licenseId;
    d.customer = L.customer;
    d.edition = L.edition;
    d.type = L.type;
    d.maxDevices = L.maxDevices;
    d.offlineGraceDays = L.offlineGraceDays;
    d.expiresAt = L.expiresAt;
    d.durationDays = L.expiresAt ? -1 : 0;
    for (const auto& g : L.features) d.features.push_back(g.id);
    d.extend = true;
    d.online = isOnlineLicence(L);
    std::vector<licensing::LicenseOption> opts = L.options;
    opts.push_back(o);
    d.options = opts;
    d.purchase = optionPurchase(o, L, t);
    OpResult r;
    if (d.online) {
        ServiceJob job;
        std::string code;
        r = prepareOnlineLicence(d, outPath, job, code);
        if (r.ok) {
            if (service_.ready()) queueServiceJob(std::move(job));
            r.message = "Option " + o.label + " ajout\xC3\xA9" "e \xC3\xA0 la licence " + licenseId + " : la nouvelle \xC3\xA9mission part sur le service (ses PC la re\xC3\xA7oivent seuls).";
        }
    } else {
        if (outPath.empty()) return OpResult::fail("o\xC3\xB9 \xC3\xA9" "crire le fichier de licence \xC3\xA0 envoyer au client ?");
        r = signLicence(d, outPath);
        if (r.ok) r.message = "Option " + o.label + " ajout\xC3\xA9" "e \xC3\xA0 la licence " + licenseId + " : " + outPath;
    }
    if (r.ok && !note.empty()) r.details.insert(r.details.begin(), note);
    if (r.ok) r.details.push_back("Les Studios de ce client doivent avoir la 1.42 ou plus pour lire une licence avec des options.");
    return r;
}

std::vector<std::string> Workspace::offersToDelete() const {
    std::vector<std::string> out;
    std::string text, w;
    if (!readText(deploy::joinPath(folder_, kOffersToDeleteFile), text, &w)) return out;
    core::json::Value v;
    if (!core::json::parse(text, v, nullptr) || !v.isObject()) return out;
    if (const core::json::Value* arr = v.find("offres"); arr && arr->isArray())
        for (const auto& x : arr->items())
            if (x.isString() && licensing::isEditionId(x.asString())) out.push_back(x.asString());
    return out;
}

OpResult Workspace::setDeleteWhenUnused(const std::string& edition, bool on) {
    auto list = offersToDelete();
    std::erase(list, edition);
    if (on) list.push_back(edition);
    core::json::Value root = core::json::Value::object();
    root.set("format", "xpg-offres-a-supprimer/1");
    core::json::Value arr = core::json::Value::array();
    for (const auto& e : list) arr.push(e);
    root.set("offres", std::move(arr));
    std::string why;
    if (!writeText(deploy::joinPath(folder_, kOffersToDeleteFile), core::json::write(root) + "\n", &why)) return OpResult::fail(why);
    return OpResult::done(on ? "L'offre " + edition + " sera supprim\xC3\xA9" "e d'elle-m\xC3\xAAme quand plus aucune licence ne l'utilisera."
                             : "L'offre " + edition + " ne sera plus supprim\xC3\xA9" "e d'elle-m\xC3\xAAme.");
}

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
