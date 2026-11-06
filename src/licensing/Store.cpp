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
#include <cstdlib>
#include <filesystem>

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
    bool          found{false};
};

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
    const std::string text = core::json::write(v, true);
    crypto::Bytes plain(text.begin(), text.end()), sealed;
    if (!crypto::protect(plain, sealed, /*machine=*/true, why)) return false;
    return crypto::writePrivateFile(path, sealed, why);
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
};

LicenseStore::LicenseStore(StoreOptions options) : opt_(std::move(options)) {
    if (opt_.trustedKeys.empty() && opt_.compiledKeys) opt_.trustedKeys = compiledPublisherKeys();
    if (opt_.clockTolerance < 0) opt_.clockTolerance = 0;
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
        s.message = kNotConfiguredMessage;
        s.detail = "aucune cl\xC3\xA9 de l'\xC3\xA9" "diteur n'est compil\xC3\xA9" "e dans ce programme (src/licensing/PublisherKey.hpp est vide)";
        registry_.reset();
        license_.reset();
        deviceKeyHex_.clear();
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
            if (hasBundled) bundledBytes = writeBlock(bb);
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
    // ---- les revocations.
    {
        const std::string path = deploy::joinPath(dir, kRevocationsFile);
        if (fileExists(path)) {
            SignedBlock b;
            RevocationList r;
            std::string why;
            if (readStoredBlock(path, DocKind::Revocations, b, &why) && verifyTrusted(b, &why) && parseRevocations(b.document, r, &why)) {
                if (r.number < in.state.revocationsMax) {
                    in.revocationsProblem = "la liste de r\xC3\xA9vocation de ce PC (n\xC2\xB0 " + std::to_string(r.number) + ") est plus ancienne que le n\xC2\xB0 "
                                            + std::to_string(in.state.revocationsMax) + " d\xC3\xA9j\xC3\xA0 vu : elle a \xC3\xA9t\xC3\xA9 remplac\xC3\xA9" "e";
                } else {
                    in.hasRevocations = true;
                    in.revocations = std::move(r);
                    in.state.revocationsMax = std::max(in.state.revocationsMax, in.revocations.number);
                }
            } else {
                in.revocationsProblem = why;
            }
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
            } else {
                in.deviceProblem = why;
            }
        } else {
            in.deviceProblem = "ce PC n'a pas encore de cl\xC3\xA9 d'identit\xC3\xA9 (elle est cr\xC3\xA9\xC3\xA9" "e avec la demande d'activation)";
        }
    }

    LicenseState s = judge(in, t);
    // Ce que l'ecran Licence montre sans relire de fichier.
    registry_ = in.hasRegistry ? std::optional<Registry>(in.registry) : std::nullopt;
    license_ = in.licenseRead ? std::optional<License>(in.license) : std::nullopt;
    deviceKeyHex_ = in.deviceKeyHex;

    // ---- l'heure la plus tardive vue, et les numeros : gardes s'ils avancent (jamais en arriere).
    State after = in.state;
    if (s.status != LicenseStatus::ClockRolledBack) after.latestSeen = std::max(after.latestSeen, t);
    const bool changed = !before.found || after.registryMax != before.registryMax || after.revocationsMax != before.revocationsMax
                         || after.latestSeen >= before.latestSeen + 60;
    std::string unwritten;   // ce qui ne s'ecrit pas (un dossier ou ce compte n'ecrit pas) : dit une fois
    if (changed) {
        std::error_code ec;
        std::filesystem::create_directories(P(dir), ec);
        std::string w;
        if (!writeState(statePath, after, &w)) unwritten = "l'\xC3\xA9tat prot\xC3\xA9g\xC3\xA9 ne s'\xC3\xA9" "crit pas (" + w + ")";
    }
    if (!in.stateNote.empty() && s.detail.empty()) s.detail = in.stateNote;
    if (const std::string w = noteState(s); !w.empty()) unwritten += (unwritten.empty() ? "" : ", ") + w;
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
    const std::string path = deploy::joinPath(opt_.folder, kHistoryFile);
    std::vector<std::string> raw;
    (void)readHistoryLines(path, &raw);   // les lignes illisibles ne sont pas recopiees
    core::json::Value v = core::json::Value::object();
    v.set("at", isoTime(e.at));
    v.set("kind", e.kind);
    v.set("ok", e.ok);
    if (!statusKey.empty()) v.set("status", std::string(statusKey));
    v.set("text", e.text);
    raw.push_back(core::json::write(v, false));
    if (raw.size() > kHistoryMaxLines) raw.erase(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(raw.size() - kHistoryMaxLines));
    std::string text;
    for (const auto& l : raw) text += l + "\n";
    return writeAll(path, text, why);
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
    for (int i = 0; i <= static_cast<int>(LicenseStatus::NotConfigured); ++i)
        if (prev == statusKey(static_cast<LicenseStatus>(i))) prevLabel = statusLabel(static_cast<LicenseStatus>(i));
    HistoryEvent e;
    e.at = s.evaluatedAt;
    e.kind = "etat";
    e.ok = statusIsNormal(s.status);
    e.text = (prevLabel.empty() ? std::string() : prevLabel + " \xE2\x86\x92 ") + statusLabel(s.status) + " : " + s.message;
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

    // Une horloge en arriere : l'heure la plus tardive vue, et les dates d'emission des documents signes.
    Seconds anchor = in.state.latestSeen;
    if (in.hasRegistry) anchor = std::max(anchor, in.registry.issuedAt);
    if (in.hasRevocations) anchor = std::max(anchor, in.revocations.issuedAt);
    if (in.licenseRead) anchor = std::max(anchor, in.license.issuedAt);
    const bool clockBack = anchor > 0 && t + opt_.clockTolerance < anchor;

    if (!in.hasLicenseFile) {
        s.status = LicenseStatus::Absent;
        applyFree();
        s.message = free ? "Aucune licence sur ce PC : " + fallback + "." : "Aucune licence sur ce PC, et " + fallback + ".";
        if (!in.registryProblem.empty()) s.detail = in.registryProblem;
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
    const auto invalid = [&](const std::string& detail) {
        s.status = LicenseStatus::Invalid;
        s.detail = detail;
        applyFree();
        s.message = "Le fichier de licence de ce PC n'est pas valide : " + fallback + "." + kNoLoss
                    + " Demande un nouveau fichier \xC3\xA0 l'\xC3\xA9" "diteur.";
        return s;
    };
    if (!in.licenseRead) return invalid(in.licenseProblem);
    const License& l = in.license;
    if (l.type == LicenseType::Floating)
        return invalid("c'est une licence flottante : elle demande le service de licences en ligne, que cette version n'a pas encore");
    if (!l.service.empty())
        return invalid("cette licence d\xC3\xA9pend d'un service en ligne (" + l.service + "), que cette version ne sait pas encore joindre");
    if (!in.hasRegistry) return invalid(in.registryProblem);
    // Une liste de revocation deja vue qui a disparu, ou remplacee par une plus ancienne : pas de retour a Valide.
    if (!in.hasRevocations && in.state.revocationsMax > 0) return invalid(in.revocationsProblem);
    std::string why;
    if (!checkAgainstRegistry(l, in.registry, &why)) return invalid(why);

    if (!in.hasDeviceKey || !l.boundTo(in.deviceKeyHex)) {
        s.status = LicenseStatus::OtherDevice;
        s.detail = in.hasDeviceKey ? "la cl\xC3\xA9 de ce PC (" + in.deviceFingerprint + ") n'est pas parmi les " + plural(l.devices.size(), "PC li\xC3\xA9", "PC li\xC3\xA9s")
                                   : in.deviceProblem;
        applyFree();
        s.message = "Cette licence (" + l.licenseId + ") a \xC3\xA9t\xC3\xA9 d\xC3\xA9livr\xC3\xA9" "e pour un autre PC : " + fallback + "." + kNoLoss
                    + " Envoie la demande d'activation de ce PC \xC3\xA0 l'\xC3\xA9" "diteur pour recevoir la tienne.";
        return s;
    }
    if (in.hasRevocations) {
        if (const Revocation* r = in.revocations.find(l.licenseId, in.deviceKeyHex)) {
            s.status = LicenseStatus::Revoked;
            s.detail = "liste de r\xC3\xA9vocation n\xC2\xB0 " + std::to_string(in.revocations.number) + (r->reason.empty() ? "" : ", motif : " + r->reason);
            applyFree();
            s.message = "Ta licence " + l.licenseId + " a \xC3\xA9t\xC3\xA9 r\xC3\xA9voqu\xC3\xA9" "e par l'\xC3\xA9" "diteur le " + isoDate(r->revokedAt)
                        + (r->reason.empty() ? "" : " (motif : " + r->reason + ")") + " : " + fallback + "." + kNoLoss;
            return s;
        }
    }
    const bool timed = l.expiresAt != 0;
    if (timed && clockBack) {
        s.status = LicenseStatus::ClockRolledBack;
        s.detail = "heure du PC " + isoTime(t) + ", heure la plus tardive d\xC3\xA9j\xC3\xA0 vue " + isoTime(anchor);
        applyFree();
        s.message = "L'horloge de ce PC indique le " + isoDate(t) + ", alors que le " + isoDate(anchor)
                    + " a d\xC3\xA9j\xC3\xA0 \xC3\xA9t\xC3\xA9 vu : remets-la \xC3\xA0 l'heure. En attendant, " + fallback + "." + kNoLoss;
        return s;
    }
    const std::string edLabel = [&] {
        const EditionDef* e = in.registry.edition(l.edition);
        return e && !e->label.empty() ? e->label : l.edition;
    }();
    if (timed && t >= l.expiresAt) {
        const Seconds graceEnd = l.type == LicenseType::Evaluation ? l.expiresAt : l.expiresAt + static_cast<Seconds>(l.offlineGraceDays) * kDay;
        if (t < graceEnd) {
            s.status = LicenseStatus::Grace;
            s.graceEndsAt = graceEnd;
            s.graceDaysLeft = static_cast<int>((graceEnd - t + kDay - 1) / kDay);
            s.edition = l.edition;
            s.editionLabel = edLabel;
            s.features = l.features;
            s.message = "Ta licence " + edLabel + " a pris fin le " + isoDate(l.expiresAt) + " : ses fonctionnalit\xC3\xA9s restent ouvertes encore "
                        + plural(s.graceDaysLeft, "jour", "jours") + ", le temps d'importer son renouvellement. Ensuite, "
                        + (free ? "l'\xC3\xA9" "dition " + freeName + " s'appliquera" : std::string("seules les fonctions de base resteront")) + " ; tes projets restent ouverts et enregistrables.";
            return s;
        }
        s.status = LicenseStatus::Expired;
        s.graceEndsAt = graceEnd > l.expiresAt ? graceEnd : 0;
        applyFree();
        s.message = (l.type == LicenseType::Evaluation ? "Ta licence d'\xC3\xA9valuation " : "Ta licence ") + edLabel + " a pris fin le "
                    + isoDate(l.expiresAt) + " : " + fallback + "." + kNoLoss + " Pour retrouver tes fonctionnalit\xC3\xA9s, importe le renouvellement re\xC3\xA7u de l'\xC3\xA9" "diteur.";
        return s;
    }
    s.status = LicenseStatus::Valid;
    s.edition = l.edition;
    s.editionLabel = edLabel;
    s.features = l.features;
    if (!timed) s.message = "Licence " + edLabel + " perp\xC3\xA9tuelle, valide sur ce PC.";
    else if (l.type == LicenseType::Evaluation) s.message = "Licence d'\xC3\xA9valuation " + edLabel + ", valide jusqu'au " + isoDate(l.expiresAt) + ".";
    else s.message = "Licence " + edLabel + " valide jusqu'au " + isoDate(l.expiresAt) + ".";
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
ImportReport LicenseStore::finishImport(ImportReport rep) {
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
        if (lic.type == LicenseType::Floating) return refuse(pre + "c'est une licence flottante, qui demande le service de licences en ligne (pas encore dans cette version).");
        if (!lic.service.empty()) return refuse(pre + "elle d\xC3\xA9pend d'un service en ligne, que cette version ne sait pas encore joindre.");
        if (!useReg) return refuse(pre + "le registre des \xC3\xA9" "ditions manque sur ce PC (importe le fichier complet re\xC3\xA7u de l'\xC3\xA9" "diteur).");
        if (!checkAgainstRegistry(lic, *useReg, &why)) return refuse(pre + why + ".");
        std::string keyHex;
        {
            const std::string kp = deploy::joinPath(dir, kDeviceKeyFile);
            if (!fileExists(kp)) return refuse(pre + "ce PC n'a pas encore de cl\xC3\xA9 d'identit\xC3\xA9 : \xC3\xA9" "cris d'abord sa demande d'activation, et envoie-la \xC3\xA0 l'\xC3\xA9" "diteur.");
            auto key = SigningKey::load(kp, deviceKeySpec(), &why);
            if (!key) return refuse(pre + why);
            keyHex = key->publicKeyHex();
        }
        if (!lic.boundTo(keyHex)) return refuse(pre + "elle a \xC3\xA9t\xC3\xA9 d\xC3\xA9livr\xC3\xA9" "e pour un autre PC. Envoie la demande d'activation de CE PC \xC3\xA0 l'\xC3\xA9" "diteur.");
        if (useRev) {
            if (const Revocation* r = useRev->find(lic.licenseId, keyHex))
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
    if (writeReg || writeRev) (void)writeState(statePath, st, &why);
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
    auto key = SigningKey::loadOrCreate(deploy::joinPath(opt_.folder, kDeviceKeyFile), deviceKeySpec(), "", why);
    if (!key) return false;
    ActivationRequest q;
    q.deviceKey = key->publicKeyHex();
    q.deviceName = deviceName;
    q.licenseId = licenseId;
    q.software = software;
    q.createdAt = now();
    q.nonce = newNonce();
    // Relue avant d'etre signee : ce qui part se relit (un nom trop long, une licence mal ecrite...).
    const std::string text = writeRequest(q);
    ActivationRequest back;
    if (!parseRequest(text, back, why)) return false;
    const SignedBlock b = signBlock(DocKind::Request, text, key->publicKey(), [&](std::string_view m) { return key->sign(m); });
    return writeAll(path, writeBlock(b), why);
}

} // namespace licensing
