// =============================================================================
//  licensing/Licensing.cpp - 1.30.0 : l'API des logiciels (voir Licensing.hpp)
// =============================================================================
#include "Licensing.hpp"

#include <memory>
#include <mutex>

namespace licensing {

namespace {

struct Module {
    std::mutex                    mutex;
    std::unique_ptr<LicenseStore> store;
    LicenseState                  state;
};

Module& module() {
    static Module m;
    return m;
}

} // namespace

LicenseState initialize(StoreOptions options) {
    Module& m = module();
    auto store = std::make_unique<LicenseStore>(std::move(options));
    LicenseState s = store->evaluate();
    std::lock_guard<std::mutex> lock(m.mutex);
    m.store = std::move(store);
    m.state = s;
    return s;
}

LicenseState refresh() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (!m.store) return m.state;
    m.state = m.store->evaluate();
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
    if (licenseFeaturesApply(s.status))
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
    return ok;
}

void resetForTests() {
    Module& m = module();
    std::lock_guard<std::mutex> lock(m.mutex);
    m.store.reset();
    m.state = LicenseState{};
}

} // namespace licensing
