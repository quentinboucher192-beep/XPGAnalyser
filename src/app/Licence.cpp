// =============================================================================
//  app/Licence.cpp - 1.30.0 : les licences dans l'application (voir Licence.hpp)
// =============================================================================
#include "Licence.hpp"

#include "App.hpp"
#include "hmi/HmiStationLaunch.hpp"
#include "screens/Screens.hpp"

#include "../licensing/Licensing.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace app::licence {

namespace {

std::atomic<bool> g_active{false};
std::mutex        g_mutex;
double            g_next{0};      // la prochaine revaluation (secondes, horloge monotone)

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::filesystem::path P(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
std::string U(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

// 1.32.0 (H3) : le controle des points de passage (Licence.hpp, require) : la phrase du module quand c'est ferme.
bool requireImpl(std::string_view feature, std::string* why) {
    if (allows(feature)) return true;
    if (why) *why = licensing::lockedMessage(feature);
    return false;
}

void arm() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_next = nowSeconds() + 3600.0;
    }
    g_active = true;
    detail::g_require.store(&requireImpl, std::memory_order_release);
}

} // namespace

// LA TABLE DES POINTS D'ENTREE. Les identifiants sont ceux du registre (la maquette) ; le registre signe dit,
// pour chaque edition et chaque licence, lesquels sont ouverts. Ni Ouvrir, ni Enregistrer, ni Exporter le
// projet, ni Fermer, ni Quitter : jamais (licence_ui_test le verifie).
const std::vector<Gate>& gates() {
    static const std::vector<Gate> g = {
        {Where::Action, "ia.assistant", "ai_assistant", "Assistant IA (le panneau)"},
        {Where::Gesture, "ia.envoyer", "ai_assistant", "Assistant IA : envoyer une demande"},
        {Where::Gesture, "ia.mission", "ai_generation", "Assistant IA : cr\xC3\xA9" "er une mission (g\xC3\xA9n\xC3\xA9ration)"},
        {Where::Action, "ia.memoire", "ai_learning", "M\xC3\xA9moire et apprentissage de l'IA"},
        {Where::Gesture, "sim.run", "api_simulation", "Simuler l'automate"},
        {Where::Gesture, "sim.step", "api_simulation", "Un cycle de simulation de l'automate"},
        {Where::Gesture, "hmi.start", "hmi_runtime", "D\xC3\xA9marrer l'IHM (simulation)"},
        {Where::Gesture, "poste.essayer", "hmi_runtime", "Essayer le poste d'exploitation"},
        {Where::Action, "project.exportSources", "api_export", "Exporter vers Control Expert"},
        {Where::ApiPane, "tables", "api_debugger", "API \xE2\x80\xBA Tables d'animation"},
        {Where::ApiPane, "bibliotheque", "api_library", "API \xE2\x80\xBA Biblioth\xC3\xA8ques"},
        {Where::HmiPane, "scripts", "hmi_scripts", "Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Scripts g\xC3\xA9n\xC3\xA9raux"},
        {Where::HmiPane, "fonctions", "hmi_scripts", "Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Fonctions"},
        {Where::Gesture, "vues.exporter", "hmi_export", "Exporter les vues (.xpgvues)"},
        {Where::Gesture, "vues.importer", "advanced_import", "Importer des vues (.xpgvues)"},
        {Where::HmiPane, "rapports", "advanced_export", "IHM \xE2\x80\xBA Rapports (PDF, Excel)"},
        {Where::HmiPane, "deploiement", "professional_tools", "IHM \xE2\x80\xBA D\xC3\xA9ploiement r\xC3\xA9seau"},
        {Where::HmiPane, "outil", "professional_tools", "IHM \xE2\x80\xBA Outil Modbus"},
    };
    return g;
}

std::string_view featureOf(Where where, std::string_view key) {
    for (const auto& g : gates())
        if (g.where == where && key == g.key) return g.feature;
    return {};
}

void start(licensing::StoreOptions options) {
    (void)licensing::initialize(std::move(options));
    arm();
}

void start() {
    licensing::StoreOptions o;
    // 1.32.0 (M1) : la variable n'est lue que dans une construction d'essai (vide sinon)
    o.folder = licensing::testFolderOverride();
    if (o.folder.empty()) o.folder = licensing::LicenseStore::defaultFolder();
    // Le registre et (1.32.0) la liste de revocation livres avec le programme (s'il y en a) : adoptes s'ils sont
    // plus recents que ceux du PC ; leur date releve l'heure la plus tardive vue.
    std::error_code ec;
    const std::string exe = station::currentExecutable();
    if (!exe.empty()) {
        const auto dir = P(exe).parent_path() / "licences";
        if (std::filesystem::is_regular_file(dir / "registre.xpgreg", ec)) o.bundledRegistry = U(dir / "registre.xpgreg");
        if (std::filesystem::is_regular_file(dir / "revocations.xpgrev", ec)) o.bundledRevocations = U(dir / "revocations.xpgrev");
    }
    (void)licensing::initialize(std::move(o));
    arm();
}

void tick() {
    if (!g_active) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const double now = nowSeconds();
        if (now < g_next) return;
        g_next = now + 3600.0;
    }
    (void)licensing::refresh();
}

void refresh() {
    if (!g_active) return;
    (void)licensing::refresh();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_next = nowSeconds() + 3600.0;
}

bool active() noexcept { return g_active; }

bool allows(std::string_view feature) {
    if (!g_active || feature.empty()) return true;
    return licensing::isFeatureEnabled(feature);
}

std::string lockedText(std::string_view feature) {
    if (allows(feature)) return {};
    return licensing::lockedMessage(feature);
}

std::string featureLabel(std::string_view feature) {
    if (g_active)
        if (const auto reg = licensing::getRegistry())
            for (const auto& f : reg->features)
                if (f.id == feature) return f.label.empty() ? std::string(feature) : f.label;
    return std::string(feature);
}

bool gate(App& app, std::string_view feature) {
    if (allows(feature)) return true;
    std::string text = licensing::lockedMessage(feature);
    text += "\n\nRien n'est ferm\xC3\xA9 : ton projet reste ouvert ; Enregistrer et Exporter restent permis.";
    auto dialog = std::make_unique<MessageDialog>("Pas dans ta licence", std::move(text), MessageDialog::Icon::Info, "Licence\xE2\x80\xA6");
    dialog->setCancelLabel("Fermer");
    app.menus().ShowDialog(std::move(dialog), [&app](const menu::DialogResult& r) {
        if (r.accepted()) showLicenceWindow(app, 2);
    });
    return false;
}

bool gate(App& app, Where where, std::string_view key) {
    const auto f = featureOf(where, key);
    return f.empty() || gate(app, f);
}

Badge badge() {
    Badge b;
    if (!g_active) return b;
    const auto st = licensing::getLicenseState();
    using S = licensing::LicenseStatus;
    const std::string edition = st.editionLabel.empty() ? st.edition : st.editionLabel;
    switch (st.status) {
        case S::NotConfigured:
            b.text = "Licences non activ\xC3\xA9" "es";
            b.tone = 0;
            break;
        case S::Valid:
            b.text = "Licence " + edition + (st.expiresAt ? " \xC2\xB7 valide jusqu'au " + st.expirationDate() : std::string(" \xC2\xB7 sans fin"));
            b.tone = 1;
            break;
        case S::Grace:
            b.text = "Licence " + edition + " \xC2\xB7 gr\xC3\xA2" "ce : " + std::to_string(st.graceDaysLeft) + " j";
            b.tone = 2;
            break;
        case S::Absent:
            b.text = edition.empty() ? std::string("Sans licence") : "Sans licence \xC2\xB7 \xC3\xA9" "dition " + edition;
            b.tone = 2;
            break;
        default:
            b.text = "Licence " + licensing::statusLabel(st.status) + (edition.empty() ? std::string{} : " \xC2\xB7 \xC3\xA9" "dition " + edition);
            b.tone = 3;
            break;
    }
    b.tip = st.message + "\n(un clic : la fen\xC3\xAAtre Licence)";
    return b;
}

} // namespace app::licence
