// =============================================================================
//  licensing/Licensing.cpp - 1.30.0 : l'API des logiciels (voir Licensing.hpp)
// =============================================================================
#include "Licensing.hpp"

#include "KeyFile.hpp"

#include <algorithm>
#include <memory>
#include <mutex>

namespace licensing {

namespace {

struct Module {
    std::mutex                    mutex;
    std::unique_ptr<LicenseStore> store;
    LicenseState                  state;
    // 1.30.0 : l'ecran Licence (gardes a chaque evaluation : les lectures ne touchent aucun fichier)
    std::optional<RegistryView> registry;
    std::vector<DeviceView>     devices;
    std::vector<HistoryEvent>   history;
};

Module& module() {
    static Module m;
    return m;
}

RegistryView viewOf(const Registry& r) {
    RegistryView v;
    v.number = r.number;
    v.issuedAt = isoTime(r.issuedAt);
    v.unlicensedEdition = r.unlicensedEdition;
    for (const auto& f : r.features) {
        RegistryFeatureView fv;
        fv.id = f.id;
        fv.product = f.product;
        fv.label = f.label;
        fv.retired = f.retired;
        for (const auto& l : f.limits) fv.limits.emplace_back(l.id, l.label);
        v.features.push_back(std::move(fv));
    }
    for (const auto& e : r.editions) {
        RegistryEditionView ev;
        ev.id = e.id;
        ev.label = e.label;
        ev.type = licenseTypeKey(e.type);
        ev.maxDevices = e.maxDevices;
        ev.durationDays = e.durationDays;
        ev.graceDays = e.offlineGraceDays;
        ev.price = e.price;              // 1.37.0
        ev.description = e.description;
        ev.promoPrice = e.promoPrice;    // 1.38.0
        ev.promoUntil = static_cast<std::int64_t>(e.promoUntil);
        ev.retired = e.retired;
        for (const auto& g : e.features) ev.features.push_back(g.id);
        std::sort(ev.features.begin(), ev.features.end());
        v.editions.push_back(std::move(ev));
    }
    for (const auto& a : r.announcements)   // 1.38.0
        v.announcements.push_back({a.id, a.title, a.text, a.edition, static_cast<std::int64_t>(a.at), static_cast<std::int64_t>(a.until)});
    return v;
}

// Apres chaque evaluation (sous le verrou) : le registre, les PC lies, l'historique.
void capture(Module& m) {
    m.registry.reset();
    m.devices.clear();
    m.history.clear();
    if (!m.store) return;
    if (m.store->registry()) m.registry = viewOf(*m.store->registry());
    if (const auto& l = m.store->license()) {
        for (const auto& d : l->devices) {
            DeviceView dv;
            dv.name = d.name;
            crypto::Bytes raw;
            crypto::PublicKey pub{};
            if (crypto::unhex(d.key, raw, 32)) std::copy(raw.begin(), raw.end(), pub.begin());
            dv.fingerprint = publicKeyFingerprint(pub);
            dv.activatedAt = isoTime(d.activatedAt);
            dv.thisPc = !m.store->deviceKeyHex().empty() && d.key == m.store->deviceKeyHex();
            m.devices.push_back(std::move(dv));
        }
    }
    m.history = m.store->history(static_cast<int>(LicenseStore::kHistoryMaxLines));
}

} // namespace

LicenseState initialize(StoreOptions options) {
    Module& m = module();
    auto store = std::make_unique<LicenseStore>(std::move(options));
    LicenseState s = store->evaluate();
    std::lock_guard<std::mutex> lock(m.mutex);
    m.store = std::move(store);
    m.state = s;
    capture(m);
    return s;
}

LicenseState refresh() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) return m.state;
    m.state = m.store->evaluate();
    capture(m);
    return m.state;
}

bool isFeatureEnabled(std::string_view featureId) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    return m.state.hasFeature(featureId);
}

std::string getEdition() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    return m.state.edition;
}

LicenseStatus getLicenseStatus() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    return m.state.status;
}

int getMaxDevices() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    return m.state.maxDevices;
}

std::string getExpirationDate() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    return m.state.expirationDate();
}

std::string getLicenseId() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    return m.state.licenseId;
}

std::string getStatusMessage() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) return "Les licences ne sont pas encore lues.";
    return m.state.message;
}

std::string getStatusLabel() { return statusLabel(getLicenseStatus()); }

std::optional<std::int64_t> getFeatureLimit(std::string_view featureId, std::string_view limit) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    return m.state.limit(featureId, limit);
}

LicenseState getLicenseState() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    return m.state;
}

std::string lockedMessage(std::string_view featureId) {
    const LicenseState s = getLicenseState();
    std::string text = "Cette fonctionnalit\xC3\xA9 (" + std::string(featureId) + ") ";
    if (allFeaturesOpen(s.status)) return text + "est ouverte : les licences ne sont pas encore actives dans ce programme.";
    if (s.status == LicenseStatus::Evaluation)   // 1.34.0 : pas de licence, l'edition d'evaluation (1.35.0 : la Decouverte)
        text += "n'est pas incluse dans la D\xC3\xA9" "couverte (" + (s.editionLabel.empty() ? s.edition : s.editionLabel)
                + ") : demande une licence qui la contient (menu Licence\xE2\x80\xA6).";
    else if (licenseFeaturesApply(s.status))
        text += "n'est pas incluse dans ta licence " + (s.editionLabel.empty() ? s.edition : s.editionLabel) + ".";
    else if (s.edition.empty())
        text += "n'est pas ouverte sur ce PC.";
    else
        text += "n'est pas ouverte : " + s.message;
    if (licenseFeaturesApply(s.status) || s.edition.empty()) text += " Ton projet reste ouvert ; Enregistrer et Exporter restent permis.";
    return text;
}

ImportReport importLicenseFile(const std::string& path) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) {
        ImportReport r;
        r.why = "Les licences ne sont pas encore configur\xC3\xA9" "es (initialize).";
        return r;
    }
    ImportReport r = m.store->importFile(path);
    m.state = r.state;
    capture(m);
    return r;
}

bool writeActivationRequest(const std::string& path, const std::string& deviceName, const std::string& licenseId,
                            const std::string& software, std::string* why) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) {
        if (why) *why = "les licences ne sont pas encore configur\xC3\xA9" "es (initialize)";
        return false;
    }
    const bool ok = m.store->writeActivationRequest(path, deviceName, licenseId, software, why);
    if (ok) m.state = m.store->evaluate();   // l'empreinte de la cle, creee a l'instant
    capture(m);                              // l'historique : la demande
    return ok;
}

// ---- 1.34.0 : la demande en ligne et l'etat du poste (depuis la 1.35.0 par le service des licences) ----
ImportReport importLicenseBytes(std::string_view bytes) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) {
        ImportReport r;
        r.why = "Les licences ne sont pas encore configur\xC3\xA9" "es (initialize).";
        return r;
    }
    ImportReport r = m.store->importBytes(bytes);
    m.state = r.state;
    capture(m);
    return r;
}

bool activationRequestText(const std::string& deviceName, const std::string& licenseId, const std::string& software, std::string& blockText,
                           ActivationRequest* parsed, std::string* why) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) {
        if (why) *why = "les licences ne sont pas encore configur\xC3\xA9" "es (initialize)";
        return false;
    }
    const bool ok = m.store->activationRequestText(deviceName, licenseId, software, blockText, parsed, why);
    if (ok) m.state = m.store->evaluate();   // l'empreinte de la cle, creee peut-etre a l'instant
    capture(m);
    return ok;
}

bool deviceOpenSeal(std::string_view sealedBase64, std::string& plain, std::string* why) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) {
        if (why) *why = "les licences ne sont pas encore configur\xC3\xA9" "es (initialize)";
        return false;
    }
    return m.store->deviceOpenSeal(sealedBase64, plain, why);
}

bool deviceKeyHex(std::string& hex, std::string* why) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) {
        if (why) *why = "les licences ne sont pas encore configur\xC3\xA9" "es (initialize)";
        return false;
    }
    return m.store->deviceKey(hex, why);
}

std::vector<TrustedKey> trustedPublisherKeys() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (m.store) return m.store->trustedKeys();
    return compiledPublisherKeys();
}

bool addHistoryEvent(const HistoryEvent& e, std::string* why) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) {
        if (why) *why = "les licences ne sont pas encore configur\xC3\xA9" "es (initialize)";
        return false;
    }
    const bool ok = m.store->addHistory(e, "", why);
    capture(m);
    return ok;
}

bool isConfigured() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (m.store) return m.store->configured();
    return !compiledPublisherKeys().empty();
}

std::optional<RegistryView> getRegistry() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    return m.registry;
}

std::vector<DeviceView> getLicensedDevices() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    return m.devices;
}

std::vector<HistoryEvent> getHistory(int limit) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (limit <= 0 || static_cast<std::size_t>(limit) >= m.history.size()) return m.history;
    return std::vector<HistoryEvent>(m.history.begin(), m.history.begin() + limit);
}

std::string getLicenseFolder() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    return m.store ? m.store->folder() : LicenseStore::defaultFolder();
}

std::string thisDeviceFingerprint() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    return m.state.deviceFingerprint;
}

// ---- 1.35.0 : le service des licences ----
namespace {
constexpr const char* kNotInitialized = "les licences ne sont pas encore configur\xC3\xA9" "es (initialize)";

bool notInitialized(std::string* why) {
    if (why) *why = kNotInitialized;
    return false;
}

// Apres un appel qui a change quelque chose : l'etat revalue, l'ecran Licence avec (sous le verrou).
void reevaluate(Module& m) {
    m.state = m.store->evaluate();
    capture(m);
}
} // namespace

bool deviceSignServiceMessage(std::string_view message, crypto::Signature& sig, crypto::PublicKey& pub, std::string* why) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) return notInitialized(why);
    bool created = false;
    const bool ok = m.store->deviceSignServiceMessage(message, sig, pub, &created, why);
    if (ok && created) reevaluate(m);   // l'empreinte de la cle, creee a l'instant
    return ok;
}

ImportReport installActivation(std::string_view licenceText, std::string_view activationText, std::string_view certText) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) {
        ImportReport r;
        r.why = "Les licences ne sont pas encore configur\xC3\xA9" "es (initialize).";
        return r;
    }
    ImportReport r = m.store->installActivation(licenceText, activationText, certText);
    m.state = r.state;
    capture(m);
    return r;
}

bool dropActivation(std::string_view licenseId, std::string* why) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) return notInitialized(why);
    const bool ok = m.store->dropActivation(licenseId, why);
    reevaluate(m);
    return ok;
}

bool noteServiceTime(std::string_view timeText, std::string_view expectedNonce, std::string_view certText, std::string* why) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) return notInitialized(why);
    const bool ok = m.store->noteServiceTime(timeText, expectedNonce, certText, why);
    if (ok) reevaluate(m);
    return ok;
}

bool installLease(std::string_view leaseText, std::string_view certText, std::string* why) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) return notInitialized(why);
    const bool ok = m.store->installLease(leaseText, certText, why);
    if (ok) reevaluate(m);
    return ok;
}

void dropLease() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) return;
    m.store->dropLease();
    reevaluate(m);
}

bool installTrialRecord(std::string_view trialText, std::string_view certText, std::string* why) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) return notInitialized(why);
    const bool ok = m.store->installTrialRecord(trialText, certText, why);
    if (ok) reevaluate(m);
    return ok;
}

std::string thisMachineHash() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (m.store) return m.store->thisMachineHash();
    return service::machineHash(systemMachineId());
}

std::string thisStationHash() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) return {};
    bool created = false;
    std::string h = m.store->thisStationHash(&created);
    if (created) reevaluate(m);   // l'empreinte de la cle, creee a l'instant
    return h;
}

std::string pendingActivationCode() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (m.store) return m.store->pendingActivationCode();
    StoreOptions o;
    o.folder = LicenseStore::defaultFolder();
    o.compiledKeys = false;
    o.machineId = "-";   // pas besoin de lire l'identifiant de l'installation pour lire un fichier
    o.evaluationPeriod = false;
    return LicenseStore(std::move(o)).pendingActivationCode();
}

bool clearPendingActivationCode(std::string* why) {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) return notInitialized(why);
    return m.store->clearPendingActivationCode(why);
}

void resetForTests() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    m.store.reset();
    m.state = LicenseState{};
    capture(m);
}

} // namespace licensing
