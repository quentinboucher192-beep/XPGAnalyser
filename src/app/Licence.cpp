// =============================================================================
//  app/Licence.cpp - 1.30.0 : les licences dans l'application (voir Licence.hpp)
// =============================================================================
#include "Licence.hpp"

#include "App.hpp"
#include "BackgroundTasks.hpp"   // 1.34.0 : les avis de la cloche (la Decouverte, le service)
#include "LicenceService.hpp"    // 1.35.0 : le service des licences (remplace la passerelle de la 1.34)
#include "hmi/HmiStationLaunch.hpp"
#include "screens/Screens.hpp"

#include "../licensing/Licensing.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace app::licence {

namespace {

// 1.35.0 (revue) : une licence en ligne << bail expire >> dont le bail court encore a l'heure retenue par le magasin :
// l'heure du service n'est plus connue sur ce PC (ou son certificat a fini) - la licence est en pause, son bail ne
// l'est pas.
bool leasePaused(const licensing::LicenseState& st) {
    return st.status == licensing::LicenseStatus::LeaseExpired && st.activationUntil > 0 && st.trustedNow > 0 && st.activationUntil > st.trustedNow;
}

std::atomic<bool> g_active{false};
std::mutex        g_mutex;
double            g_next{0};      // la prochaine revaluation (secondes, horloge monotone)
// 1.34.0 : les avis (startNotices) et les messages pour la barre d'etat (takeMessages).
std::atomic<bool>        g_notices{false};
std::vector<std::string> g_messages;   // sous g_mutex
std::vector<Answer>      g_answers;    // 1.36.0 : les reponses de l'editeur a montrer (sous g_mutex)
constexpr const char*    kDiscoveryNotice = "licence:decouverte";
constexpr const char*    kLeaseNotice = "licence:bail-fin";

// Les avis qui suivent l'etat : poses (ou mis a jour) tant que la chose dure, retires sinon - la Decouverte et ses
// jours ; 1.35.0 : le bail d'une licence en ligne qui finit (7 jours) ou qui est fini.
void syncStateNotices() {
    if (!g_notices || !g_active) return;
    const auto st = licensing::getLicenseState();
    if (const std::string text = discoveryText(st); text.empty()) {
        bgtasks::withdraw(kDiscoveryNotice);
    } else {
        postNotice(kDiscoveryNotice, text,
                   std::string("Les fonctions de la D\xC3\xA9" "couverte sont ouvertes jusqu'\xC3\xA0 cette date ; ensuite, sans licence, aucun projet ne s'ouvre ni ne se cr\xC3\xA9" "e. ") +
                   (onlineRequests() ? std::string("Pour continuer : demande ta licence \xC3\xA0 l'\xC3\xA9" "diteur (Licence\xE2\x80\xA6 \xE2\x80\xBA Demander ma licence) ; "
                                                   "d\xC3\xA8s qu'il l'accepte, elle s'installe d'elle-m\xC3\xAAme.")
                                       : std::string("Pour continuer : Licence\xE2\x80\xA6 \xE2\x80\xBA Activation (le code re\xC3\xA7u de l'\xC3\xA9" "diteur).")),
                   st.evaluationDaysLeft <= 7 ? "warning" : "info");
    }
    if (const std::string text = leaseText(st); text.empty()) {
        bgtasks::withdraw(kLeaseNotice);
    } else {
        postNotice(kLeaseNotice, text,
                   "Le bail d'une licence en ligne se renouvelle tout seul \xC3\xA0 chaque connexion. Sans connexion avant sa fin, les fonctions "
                   "de la licence se mettent en pause sur ce PC (rien ne se ferme) jusqu'\xC3\xA0 la prochaine connexion.",
                   st.status == licensing::LicenseStatus::LeaseExpired ? "error" : "warning");
    }
}

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
    syncStateNotices();
}

void start(bool takeFloatingSeat) {
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
    // 1.35.0 : une licence flottante prend son poste AVANT la premiere evaluation (sur un fil, quelques secondes au plus).
    if (takeFloatingSeat) (void)service::startupLease(o);
    (void)licensing::initialize(std::move(o));
    arm();
    syncStateNotices();
}

void tick() {
    if (!g_active) return;
    service::poll();   // 1.35.0 : un travail du service fini (les avis) ; ce que le fil de fond a a faire
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const double now = nowSeconds();
        if (now < g_next) return;
        g_next = now + 3600.0;
    }
    (void)licensing::refresh();
    syncStateNotices();
}

void refresh() {
    if (!g_active) return;
    (void)licensing::refresh();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_next = nowSeconds() + 3600.0;
    }
    syncStateNotices();
}

bool active() noexcept { return g_active; }

bool allows(std::string_view feature) {
    if (!g_active || feature.empty()) return true;
    return licensing::isFeatureEnabled(feature);
}

std::string stateText(const licensing::LicenseState& st) {
    std::string m = st.message;
    if (!g_active || licensing::allFeaturesOpen(st.status) || licensing::licenseFeaturesApply(st.status)) return m;
    // << l'edition X s'applique >> (le module) : ici, rien ne s'ouvre
    const std::string ed = "l'\xC3\xA9" "dition ";
    if (const auto a = m.find(ed); a != std::string::npos)
        if (const auto b = m.find(" s'applique", a); b != std::string::npos)
            m.replace(a, b + 11 - a, "aucun projet ne s'ouvre ni ne se cr\xC3\xA9" "e");
    const std::string keep = "Ton projet reste ouvert ; Enregistrer et Exporter restent permis.";
    if (const auto k = m.find(keep); k != std::string::npos) m.replace(k, keep.size(), "Un projet d\xC3\xA9j\xC3\xA0 ouvert reste ouvert et s'enregistre.");
    if (m.find("aucun projet") == std::string::npos) m += (m.empty() ? "" : " ") + std::string("Aucun projet ne s'ouvre ni ne se cr\xC3\xA9" "e.");
    return m;
}

bool projectsAllowed(std::string* why) {
    if (!g_active) return true;
    const auto st = licensing::getLicenseState();
    if (licensing::allFeaturesOpen(st.status) || licensing::licenseFeaturesApply(st.status)) return true;
    if (why) {
        if (st.status == licensing::LicenseStatus::Absent)
            *why = st.evaluationOver ? "Ta D\xC3\xA9" "couverte (l'essai de 30 jours) est finie, et ce PC n'a pas de licence."
                                     : "Ce PC n'a pas de licence.";
        else
            *why = licensing::statusLabel(st.status) + (st.message.empty() ? std::string(".") : " : " + stateText(st));
    }
    return false;
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
        if (r.accepted()) showLicenceWindow(app, kTabFeatures);
    });
    return false;
}

bool gate(App& app, Where where, std::string_view key) {
    const auto f = featureOf(where, key);
    return f.empty() || gate(app, f);
}

namespace {

// L'edition affichee (son nom du registre, sinon son identifiant).
std::string editionOf(const licensing::LicenseState& st) {
    if (!st.licensedEdition.empty()) return st.licensedEdition;
    return st.editionLabel.empty() ? st.edition : st.editionLabel;
}

std::string daysText(int n) { return std::to_string(std::max(0, n)) + (n > 1 ? " jours" : " jour"); }

} // namespace

std::string dayOf(licensing::Seconds at) {
    const std::string d = at ? licensing::isoDate(at) : std::string();   // "2026-11-19"
    return d.size() == 10 ? d.substr(8, 2) + "/" + d.substr(5, 2) + "/" + d.substr(0, 4) : d;
}

Badge indicator() {
    Badge b;
    if (!g_active) return b;
    const auto st = licensing::getLicenseState();
    using S = licensing::LicenseStatus;
    const std::string edition = editionOf(st);
    switch (st.status) {
        case S::NotConfigured:
            return b;   // rien a montrer : les licences ne sont pas actives (tout est ouvert)
        case S::Valid:
            if (st.online && st.activationWarning) {
                b.text = "Bail : " + std::to_string(std::max(0, st.activationDaysLeft)) + " j \xE2\x80\x94 connecte-toi";
                b.shortText = "Bail : " + std::to_string(std::max(0, st.activationDaysLeft)) + " j";
                b.tone = 2;
            } else if (st.floating) {
                b.text = "Flottante \xC2\xB7 poste pris";
                b.shortText = "Flottante";
                b.tone = 1;
            } else {
                b.text = "Licence : " + edition + (st.expiresAt ? " \xC2\xB7 jusqu'au " + dayOf(st.expiresAt) : std::string(" \xC2\xB7 sans fin"));
                b.shortText = edition;
                b.tone = 1;
            }
            break;
        case S::Grace:
            b.text = "Licence : " + edition + " \xC2\xB7 gr\xC3\xA2" "ce : " + std::to_string(st.graceDaysLeft) + " j";
            b.shortText = edition + " \xC2\xB7 gr\xC3\xA2" "ce";
            b.tone = 2;
            break;
        case S::Evaluation:
            b.text = "D\xC3\xA9" "couverte \xC2\xB7 " + std::to_string(std::max(0, st.evaluationDaysLeft)) + " j";
            b.shortText = b.text;
            b.tone = st.evaluationDaysLeft <= 7 ? 2 : 4;
            break;
        case S::Absent:
            b.text = "Sans licence";
            b.shortText = b.text;
            b.tone = 2;
            break;
        case S::LeaseExpired:
            // 1.35.0 (revue) : le bail court encore, mais l'heure du service n'est plus connue sur ce PC (ou le certificat a
            // fini) : la licence est en pause, pas son bail fini.
            if (leasePaused(st)) {
                b.text = "Licence en pause \xE2\x80\x94 connecte-toi";
                b.shortText = "En pause";
            } else {
                b.text = "Bail expir\xC3\xA9 \xE2\x80\x94 connecte-toi";
                b.shortText = "Bail expir\xC3\xA9";
            }
            b.tone = 3;
            break;
        case S::ClockRolledBack:
            b.text = "Horloge recul\xC3\xA9" "e \xE2\x80\x94 remets-la \xC3\xA0 l'heure";
            b.shortText = "Horloge recul\xC3\xA9" "e";
            b.tone = 3;
            break;
        case S::NoFloatingSeat:
            b.text = "Flottante : aucun poste";
            b.shortText = "Aucun poste";
            b.tone = 2;
            break;
        default:
            b.text = "Licence : " + licensing::statusLabel(st.status);
            b.shortText = licensing::statusLabel(st.status);
            b.tone = 3;
            break;
    }
    std::string tip = stateText(st);
    if (st.online && st.activationUntil) tip += "\nBail de ce PC jusqu'au " + service::dateText(st.activationUntil) + " (renouvel\xC3\xA9 \xC3\xA0 chaque connexion).";
    if (st.floating && st.leaseUntil) tip += "\nPoste flottant pris jusqu'\xC3\xA0 " + service::dateText(st.leaseUntil).substr(11) + ".";
    b.tip = tip + "\n(un clic : la fen\xC3\xAAtre Licence)";
    return b;
}

Badge badge() {
    if (!g_active) return {};
    if (licensing::getLicenseState().status == licensing::LicenseStatus::NotConfigured) {
        Badge b;
        b.text = "Licences non activ\xC3\xA9" "es";
        b.shortText = b.text;
        b.tip = licensing::getStatusMessage() + "\n(un clic : la fen\xC3\xAAtre Licence)";
        return b;
    }
    return indicator();
}

Card startCard() {
    Card c;
    if (!g_notices || !g_active) return c;
    const auto st = licensing::getLicenseState();
    using S = licensing::LicenseStatus;
    const std::string edition = editionOf(st);
    c.button = "Licence\xE2\x80\xA6";
    c.tab = kTabAuto;
    switch (st.status) {
        case S::NotConfigured:
            // 1.35.1 : l'encart reste la, meme sans cle de l'editeur compilee : on sait toujours ou on en est
            c.title = "Licences non activ\xC3\xA9" "es dans cette version";
            c.text = "Toutes les fonctions sont ouvertes : cette version n'a pas la cl\xC3\xA9 de l'\xC3\xA9" "diteur.";
            c.tone = 0;
            break;
        case S::Evaluation: {
            c.title = discoveryText(st);
            if (!c.title.empty() && c.title.back() == '.') c.title.pop_back();
            c.text = "Ensuite, sans licence, aucun projet ne s'ouvre. Tu as un code ? Active ta licence.";   // 1.35.1
            c.tone = st.evaluationDaysLeft <= 7 ? 2 : 4;
            c.button = "Activer avec un code\xE2\x80\xA6";
            c.tab = kTabActivation;
            // 1.36.0 : la demande en ligne d'abord (ou celle qui attend)
            if (onlineRequests()) {
                if (const auto pr = pendingRequest(); pr.pending) {
                    c.text = "Ta demande n\xC2\xB0 " + std::to_string(pr.number) + " attend l'\xC3\xA9" "diteur : ta licence s'installera d\xC3\xA8s qu'il l'accepte.";
                    c.button = "Voir ma demande";
                } else {
                    c.text = "Ensuite, sans licence, aucun projet ne s'ouvre : demande-la \xC3\xA0 l'\xC3\xA9" "diteur.";
                    c.button = "Demander ma licence\xE2\x80\xA6";
                }
                c.tab = kTabRequest;
            }
            break;
        }
        case S::Absent:
            // 1.35.1 : sans licence, le logiciel ne s'utilise pas - l'encart le dit d'abord
            c.title = "Sans licence : aucun projet ne s'ouvre";
            c.text = st.evaluationOver ? "Ta D\xC3\xA9" "couverte (l'essai de 30 jours) est finie. Active ta licence avec le code re\xC3\xA7u de l'\xC3\xA9" "diteur."
                                       : "Active ta licence avec le code re\xC3\xA7u de l'\xC3\xA9" "diteur (ou demande-la).";
            c.tone = 3;
            c.button = "Activer avec un code\xE2\x80\xA6";
            c.tab = kTabActivation;
            // 1.36.0 : la demande en ligne d'abord (ou celle qui attend)
            if (onlineRequests()) {
                if (const auto pr = pendingRequest(); pr.pending) {
                    c.title = "Demande envoy\xC3\xA9" "e : en attente de l'\xC3\xA9" "diteur";
                    c.text = "Demande n\xC2\xB0 " + std::to_string(pr.number) + " du " + service::dateText(pr.sentAt) +
                             " : ta licence s'installera d\xC3\xA8s qu'il l'accepte.";
                    c.tone = 2;
                    c.button = "Voir ma demande";
                } else {
                    c.text = st.evaluationOver ? std::string("Ta D\xC3\xA9" "couverte est finie : demande ta licence \xC3\xA0 l'\xC3\xA9" "diteur.")
                                               : std::string("Demande ta licence \xC3\xA0 l'\xC3\xA9" "diteur : elle s'installe d\xC3\xA8s qu'il l'accepte.");
                    c.button = "Demander ma licence\xE2\x80\xA6";
                }
                c.tab = kTabRequest;
            }
            break;
        case S::LeaseExpired:
            if (leasePaused(st)) {
                c.title = "Ta licence " + st.licenseId + " est en pause sur ce PC";
                c.text = "Connecte ce PC \xC3\xA0 Internet : l'heure du service des licences se recale toute seule et les fonctions reviennent.";
            } else {
                c.title = "Bail expir\xC3\xA9 : ta licence " + st.licenseId + " est en pause sur ce PC";
                c.text = "Connecte ce PC \xC3\xA0 Internet : le bail se renouvelle tout seul et les fonctions reviennent.";
            }
            c.tone = 3;
            c.tab = kTabActivation;
            break;
        case S::NoFloatingSeat: {
            c.title = "Licence flottante " + st.licenseId + " : aucun poste libre";
            const licensing::Seconds at = service::seatFreeAt();
            c.text = at > licensing::systemNow() ? "Un poste se lib\xC3\xA8re dans " + std::to_string((at - licensing::systemNow() + 59) / 60) +
                                                       " min ; nouvel essai tout seul."
                                                 : std::string("Nouvel essai tout seul toutes les quelques minutes.");
            c.tone = 2;
            c.tab = kTabActivation;
            break;
        }
        case S::Valid:
            if (st.online && st.activationWarning) {
                c.title = "Bail : il reste " + daysText(st.activationDaysLeft) + " sur ce PC";
                c.text = "Connecte ce PC \xC3\xA0 Internet avant le " + dayOf(st.activationUntil) + " : le bail de ta licence " + st.licenseId +
                         " se renouvelle tout seul.";
                c.tone = 2;
                c.tab = kTabActivation;
                break;
            }
            c.title = "Licence : " + edition + (st.expiresAt ? " \xC2\xB7 jusqu'au " + dayOf(st.expiresAt) : std::string(" \xC2\xB7 sans fin"));
            if (st.floating) c.text = "Flottante \xC2\xB7 poste pris" + (st.leaseUntil ? " jusqu'\xC3\xA0 " + service::dateText(st.leaseUntil).substr(11) : std::string());
            else if (st.online) c.text = st.licenseId + " \xC2\xB7 activ\xC3\xA9" "e en ligne, bail jusqu'au " + dayOf(st.activationUntil);
            else c.text = st.licenseId + (st.customer.empty() ? std::string() : " \xC2\xB7 " + st.customer);
            c.tone = 1;
            break;
        case S::Grace:
            c.title = "Licence : " + edition + " \xC2\xB7 gr\xC3\xA2" "ce, " + daysText(st.graceDaysLeft);
            c.text = "Ta licence a pris fin : importe son renouvellement avant la fin de la gr\xC3\xA2" "ce.";
            c.tone = 2;
            break;
        default:
            c.title = "Licence : " + licensing::statusLabel(st.status) + " \xC2\xB7 aucun projet ne s'ouvre";
            c.text = stateText(st);
            c.tone = 3;
            c.tab = kTabActivation;
            // 1.36.0 : une licence finie, revoquee, refusee... : une nouvelle demande en ligne (ou celle qui attend)
            if (onlineRequests()) {
                const auto pr = pendingRequest();
                if (pr.pending) c.text = "Ta demande n\xC2\xB0 " + std::to_string(pr.number) + " attend l'\xC3\xA9" "diteur : ta licence s'installera d\xC3\xA8s qu'il l'accepte.";
                c.button = pr.pending ? "Voir ma demande" : "Demander ma licence\xE2\x80\xA6";
                c.tab = kTabRequest;
            }
            break;
    }
    // 1.35.1 : une licence en pause (bail, flottante sans poste) : rien ne s'ouvre non plus en attendant
    if ((st.status == S::LeaseExpired || st.status == S::NoFloatingSeat) && !c.text.empty())
        c.text += " En attendant, aucun projet ne s'ouvre.";
    return c;
}

// ============================================================ 1.34.0 : les avis ==
std::string discoveryText(const licensing::LicenseState& st) {
    if (st.status != licensing::LicenseStatus::Evaluation) return {};
    std::string text = "D\xC3\xA9" "couverte : il te reste " + daysText(st.evaluationDaysLeft);
    const licensing::Seconds end = st.evaluationEndsAt ? st.evaluationEndsAt : st.expiresAt;
    if (end) text += " (jusqu'au " + dayOf(end) + ")";
    return text + ".";
}

std::string leaseText(const licensing::LicenseState& st) {
    if (st.status == licensing::LicenseStatus::LeaseExpired && leasePaused(st))
        return "Licence" + (st.licenseId.empty() ? std::string() : " " + st.licenseId) +
               " en pause sur ce PC (l'heure du service n'y est plus connue) : connecte-le \xC3\xA0 Internet pour la r\xC3\xA9" "activer.";
    if (st.status == licensing::LicenseStatus::LeaseExpired)
        return "Bail expir\xC3\xA9" + (st.licenseId.empty() ? std::string() : " pour la licence " + st.licenseId) +
               " : connecte ce PC \xC3\xA0 Internet pour la r\xC3\xA9" "activer.";
    if (st.online && st.status == licensing::LicenseStatus::Valid && st.activationWarning)
        return "Bail : il reste " + daysText(st.activationDaysLeft) + " \xE2\x80\x94 connecte ce PC \xC3\xA0 Internet avant le " + dayOf(st.activationUntil) + ".";
    return {};
}

std::string launchNotice() {
    if (!g_notices || !g_active) return {};
    const auto st = licensing::getLicenseState();
    if (auto t = leaseText(st); !t.empty()) return t;
    return discoveryText(st);
}

void postNotice(const std::string& key, const std::string& title, const std::string& detail, const std::string& tone) {
    bgtasks::post({key, "Licence", title, detail, "Licence\xE2\x80\xA6", "help.licence", tone});
}

void queueMessage(std::string text) {
    if (text.empty()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_messages.size() < 20) g_messages.push_back(std::move(text));
}

std::vector<std::string> takeMessages() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return std::exchange(g_messages, {});
}

void startService() { service::startRunner(); }

// ============================================================ 1.36.0 : la demande en ligne ==
bool onlineRequests() { return g_active && service::available(); }

PendingRequest pendingRequest() {
    // Le releve relu quand le service a fini un travail, et au plus une fois par seconde (l'autre Studio l'ecrit aussi).
    static std::mutex          m;
    static PendingRequest      cached;
    static unsigned long long  seenRevision = ~0ull;
    static double              seenAt = -1e9;
    std::lock_guard<std::mutex> lock(m);
    const double now = nowSeconds();
    const unsigned long long rev = service::revision();
    if (rev != seenRevision || now - seenAt >= 1.0) {
        seenRevision = rev;
        seenAt = now;
        service::Record rec;
        cached = PendingRequest{};
        if (service::loadRecord(rec, nullptr) && service::pending(rec)) {
            cached.pending = true;
            cached.number = rec.number;
            cached.sentAt = rec.sentAt;
            cached.state = service::stateText(rec);
        }
    }
    return cached;
}

void queueAnswer(Answer a) {
    if (!g_notices || a.kind.empty()) return;   // personne devant l'ecran (le poste d'exploitation qui tourne)
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_answers.size() < 4) g_answers.push_back(std::move(a));
}

std::optional<Answer> takeAnswer() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_answers.empty()) return std::nullopt;
    Answer a = std::move(g_answers.front());
    g_answers.erase(g_answers.begin());
    return a;
}

AnswerView answerView(const Answer& a, const licensing::LicenseState& st) {
    AnswerView v;
    if (a.kind == service::kAnswerRefused) {
        v.accepted = false;
        v.title = "Ta demande de licence est refus\xC3\xA9" "e";
        v.text = "L'\xC3\xA9" "diteur a refus\xC3\xA9 ta demande.";
        v.text += a.note.empty() ? std::string(" Il n'a pas donn\xC3\xA9 de raison.") : "\n\nSa raison : \xC2\xAB " + a.note + " \xC2\xBB";
        v.text += "\n\nTu peux lui envoyer une nouvelle demande (Licence\xE2\x80\xA6 \xE2\x80\xBA Demander ma licence), ou le contacter.";
        v.confirm = "Nouvelle demande\xE2\x80\xA6";
        v.tab = kTabRequest;
        return v;
    }
    const bool updated = a.kind == service::kAnswerUpdated;
    v.title = updated ? "Ta licence est mise \xC3\xA0 jour" : "Ta licence est activ\xC3\xA9" "e";
    v.confirm = "Voir ma licence";
    v.tab = kTabMine;
    const std::string id = !st.licenseId.empty() ? st.licenseId : a.licenseId;
    std::string text = updated ? "L'\xC3\xA9" "diteur a mis \xC3\xA0 jour ta licence : elle est install\xC3\xA9" "e sur ce PC."
                               : "L'\xC3\xA9" "diteur a accept\xC3\xA9 ta demande : ta licence est install\xC3\xA9" "e sur ce PC, tu peux travailler.";
    text += "\n\nLicence : " + id + (st.editionLabel.empty() && st.licensedEdition.empty() ? std::string() : " \xC2\xB7 " + editionOf(st));
    if (!st.customer.empty()) text += "\nClient : " + st.customer;
    // La duree : jusqu'a quand, et les jours qui restent.
    if (st.expiresAt > 0) {
        const licensing::Seconds now = st.trustedNow > 0 ? st.trustedNow : licensing::systemNow();
        const long long days = st.expiresAt > now ? (st.expiresAt - now + licensing::kDay - 1) / licensing::kDay : 0;
        text += "\nValable jusqu'au " + dayOf(st.expiresAt) + " (" + daysText(static_cast<int>(days)) + ")";
    } else {
        text += "\nValable sans fin";
    }
    if (st.maxDevices > 0)
        text += "\nPC : " + std::to_string(st.maxDevices) + (st.maxDevices > 1 ? " au plus (ce PC en est un)" : " (ce PC)");
    // Ce qu'elle ouvre, par logiciel (le registre : l'ordre et les noms).
    if (!st.features.empty()) {
        std::vector<std::pair<std::string, std::vector<std::string>>> byProduct;
        const auto reg = licensing::getRegistry();
        const auto limitText = [](const licensing::FeatureGrant& g) {
            std::string s;
            for (const auto& [name, value] : g.limits) s += (s.empty() ? " (" : ", ") + name + " : " + std::to_string(value);
            return s.empty() ? s : s + ")";
        };
        const auto add = [&byProduct](const std::string& product, std::string label) {
            for (auto& [p, labels] : byProduct)
                if (p == product) {
                    labels.push_back(std::move(label));
                    return;
                }
            byProduct.push_back({product, {std::move(label)}});
        };
        std::size_t counted = 0;
        if (reg)
            for (const auto& f : reg->features)
                for (const auto& g : st.features)
                    if (g.id == f.id) {
                        add(f.product.empty() ? std::string("Autres") : f.product, (f.label.empty() ? f.id : f.label) + limitText(g));
                        ++counted;
                    }
        if (counted < st.features.size())   // une fonction que le registre ne nomme pas : son identifiant
            for (const auto& g : st.features) {
                bool known = false;
                if (reg)
                    for (const auto& f : reg->features) known = known || f.id == g.id;
                if (!known) add("Autres", g.id + limitText(g));
            }
        text += "\n\nCe qu'elle ouvre (" + std::to_string(st.features.size()) + (st.features.size() > 1 ? " fonctions" : " fonction") + ") :";
        for (const auto& [product, labels] : byProduct) {
            text += "\n  " + product + " : ";
            for (std::size_t i = 0; i < labels.size(); ++i) text += (i ? ", " : "") + labels[i];
        }
    }
    if (!a.note.empty()) text += "\n\nLe mot de l'\xC3\xA9" "diteur : \xC2\xAB " + a.note + " \xC2\xBB";
    v.text = std::move(text);
    return v;
}

void showNotices() {
    if (!g_notices.exchange(true)) syncStateNotices();
}

void startNotices(bool online) {
    g_notices = true;
    syncStateNotices();
    if (online) service::startRunner();
}

void shutdown() { service::shutdown(); }

} // namespace app::licence
