// =============================================================================
//  app/Licence.cpp - 1.30.0 : les licences dans l'application (voir Licence.hpp)
// =============================================================================
#include "Licence.hpp"
#include "Updates.hpp"   // 1.44.0 : shutdown (le telechargement en cours)

#include "App.hpp"
#include "BackgroundTasks.hpp"   // 1.34.0 : les avis de la cloche (la Decouverte, le service)
#include "LicenceService.hpp"    // 1.35.0 : le service des licences (remplace la passerelle de la 1.34)
#include "hmi/HmiStationLaunch.hpp"
#include "screens/Screens.hpp"

#include "Settings.hpp"   // 1.37.0 : le dossier des reglages (Mon profil)

#include "../core/Json.hpp"
#include "../licensing/Gateway.hpp"   // 1.37.0 : kMailSent, kMailFailed
#include "../licensing/Licensing.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
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

// 1.39.0 : une entree (action, geste, volet) dont la fonctionnalite n'est pas dans la licence.
bool lockedImpl(Where where, std::string_view key) {
    const auto f = featureOf(where, key);
    return !f.empty() && !allows(f);
}

void arm() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_next = nowSeconds() + 3600.0;
    }
    g_active = true;
    detail::g_require.store(&requireImpl, std::memory_order_release);
    detail::g_locked.store(&lockedImpl, std::memory_order_release);   // 1.39.0 : les menus et les listes
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
        {Where::Action, "session.start", "multi_user", "D\xC3\xA9marrer une session (travail \xC3\xA0 plusieurs)"},   // 1.43.0
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
    if (g_notices) (void)watch::check();   // 1.38.0 : les notifications de la licence, des offres, des annonces
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
    showLocked(app, feature);   // 1.39.0 : << Ta licence ne permet pas cette action >> (la capture, les offres)
    return false;
}

bool gate(App& app, Where where, std::string_view key) {
    const auto f = featureOf(where, key);
    if (f.empty() || allows(f)) return true;
    // 1.39.0 : le libelle de l'entree (<< Simuler l'automate >>, << IHM > Rapports >>) dans la fenetre
    std::string label;
    for (const auto& g : gates())
        if (g.where == where && key == g.key) label = g.label;
    showLocked(app, f, std::move(label));
    return false;
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

// ============================================================ 1.39.0 : la licence perdue en direct ==
namespace {
std::vector<Loss>        g_losses;
std::vector<std::string> g_lossShown;   // une fenetre par licence et par lancement
} // namespace

void queueLoss(std::string licenseId, std::string text) {
    if (!g_notices || text.empty()) return;   // personne devant l'ecran (le poste d'exploitation qui tourne)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const std::string id = licenseId.empty() ? std::string("?") : licenseId;
        if (std::find(g_lossShown.begin(), g_lossShown.end(), id) != g_lossShown.end()) return;
        g_lossShown.push_back(id);
        if (g_losses.size() < 4) g_losses.push_back({licenseId, text});
    }
    // 1.44.0 : UN evenement, UNE notification - la licence revoquee (le regard sur la licence) et la licence perdue (le
    // service) disaient la meme chose deux fois : la << perdue >>, plus precise, remplace la << revoquee >>.
    for (const auto& old : notices::list())
        if (old.key == "licence:revoquee:" + licenseId) (void)notices::remove(old.id);
    notices::Item n;
    n.category = "Licence";
    n.title = "Licence perdue sur ce PC" + (licenseId.empty() ? std::string() : " : " + licenseId);
    n.detail = text;
    n.tone = "error";
    n.key = "licence:perdue:" + licenseId;
    n.button = "Voir ma licence";
    n.action = notices::toLicence(kTabMine);
    (void)notices::post(std::move(n));
}

std::optional<Loss> takeLoss() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_losses.empty()) return std::nullopt;
    Loss l = std::move(g_losses.front());
    g_losses.erase(g_losses.begin());
    return l;
}

void showLoss(App& app, const Loss& l) {
    std::string text = l.text;
    text += "\n\nLes fonctions de ta licence sont ferm\xC3\xA9" "es d\xC3\xA8s maintenant. Rien n'est ferm\xC3\xA9 : ton projet reste ouvert ; "
            "Enregistrer et Exporter restent permis.\nSi c'est une erreur, contacte l'\xC3\xA9" "diteur.";
    auto dialog = std::make_unique<MessageDialog>("Ta licence n'est plus active sur ce PC", std::move(text), MessageDialog::Icon::Warning, "Voir ma licence\xE2\x80\xA6");
    dialog->setCancelLabel("Fermer");
    app.menus().ShowDialog(std::move(dialog), [&app](const menu::DialogResult& r) {
        if (r.accepted()) showLicenceWindow(app, kTabMine);
    });
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
    const bool option = !a.option.empty();   // 1.43.0 : une option acceptee, pas une licence
    v.title = option   ? "Ton option est arriv\xC3\xA9" "e"
            : updated  ? "Ta licence est mise \xC3\xA0 jour"
            : a.online ? "Ta licence en ligne est activ\xC3\xA9" "e"
                       : "Ta licence est activ\xC3\xA9" "e";
    v.confirm = "Voir ma licence";
    v.tab = kTabMine;
    const std::string id = !st.licenseId.empty() ? st.licenseId : a.licenseId;
    std::string text = option     ? "L'\xC3\xA9" "diteur a accept\xC3\xA9 ta demande d'option : \xC2\xAB " + a.option + " \xC2\xBB est sur ce PC, tu peux "
                                    "t'en servir. Elle s'arr\xC3\xAAte seule \xC3\xA0 sa date ; le reste de ta licence ne bouge pas."
                       : updated  ? std::string("L'\xC3\xA9" "diteur a mis \xC3\xA0 jour ta licence : elle est install\xC3\xA9" "e sur ce PC.")
                       : a.online ? std::string("L'\xC3\xA9" "diteur a accept\xC3\xA9 ta demande : ta licence en ligne est activ\xC3\xA9" "e sur ce PC (avec son code, re\xC3\xA7u "
                                    "scell\xC3\xA9 pour lui), tu peux travailler.")
                                  : std::string("L'\xC3\xA9" "diteur a accept\xC3\xA9 ta demande : ta licence est install\xC3\xA9" "e sur ce PC, tu peux travailler.");
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
    // 1.37.0 : une licence en ligne : le mot automatique de l'outil n'apprend rien ici (la fenetre dit tout).
    if (!a.note.empty() && !a.online) text += "\n\nLe mot de l'\xC3\xA9" "diteur : \xC2\xAB " + a.note + " \xC2\xBB";
    // 1.37.0 : le mail de l'editeur ; parti : le dire ; pas parti : l'adresse a verifier, et le code (une fois, ici).
    if (a.mail == licensing::gateway::kMailFailed) {
        text += "\n\nLe mail de l'\xC3\xA9" "diteur n'est pas parti" + (a.mailTo.empty() ? std::string() : " (vers " + a.mailTo + ")") +
                " : v\xC3\xA9rifie ton adresse dans ton profil.";
        v.confirm = "V\xC3\xA9rifier mon adresse\xE2\x80\xA6";
        v.tab = kTabProfile;
    }
    if (!a.code.empty() && a.mail != licensing::gateway::kMailSent) {
        text += std::string(a.mail == licensing::gateway::kMailFailed ? "\n" : "\n\n") + "Ton code d'activation : " + a.code +
                "\nNote-le : il r\xC3\xA9" "active ta licence sur un autre PC ou apr\xC3\xA8s une r\xC3\xA9installation. Garde-le pour toi.";
    } else if (a.mail == licensing::gateway::kMailSent) {
        text += "\n\nL'\xC3\xA9" "diteur t'a aussi envoy\xC3\xA9 un mail" + (a.mailTo.empty() ? std::string() : " (\xC3\xA0 " + a.mailTo + ")") +
                (a.online ? std::string(" avec ton code d'activation.") : std::string("."));
    }
    v.text = std::move(text);
    return v;
}

// ============================================================ 1.37.0 : Mon profil ==
namespace {
std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
std::string trimmed(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
bool readAll(const std::filesystem::path& p, std::string& out, std::size_t limit) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec) || std::filesystem::file_size(p, ec) > limit) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}
std::mutex  g_featureMutex;
std::string g_featureFolder;
} // namespace

std::string profilePath() {
    const auto p = std::filesystem::path(Settings::sharedFolder()) / "profil.json";
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

Profile loadProfile() {
    Profile p;
    std::string text;
    core::json::Value v;
    if (!readAll(pathOf(profilePath()), text, 64 * 1024) || !core::json::parse(text, v, nullptr) || !v.isObject()) return p;
    const auto field = [&v](const char* key) {
        std::string s = trimmed(v.str(key));
        if (s.size() > kMaxProfileField) s.clear();
        return s;
    };
    p.email = field("courriel");
    p.name = field("nom");
    p.company = field("societe");
    p.verified = field("courriel_verifie");   // 1.38.0
    if (!p.email.empty() && !licensing::gateway::plausibleEmail(p.email)) p.email.clear();
    return p;
}

bool saveProfile(const Profile& in, std::string* why) {
    Profile p{trimmed(in.email), trimmed(in.name), trimmed(in.company), trimmed(in.verified)};
    const auto fail = [why](std::string t) {
        if (why) *why = std::move(t);
        return false;
    };
    if (p.email.size() > kMaxProfileField || p.name.size() > kMaxProfileField || p.company.size() > kMaxProfileField)
        return fail("un champ est trop long (" + std::to_string(kMaxProfileField) + " caract\xC3\xA8res au plus)");
    if (!p.email.empty() && !licensing::gateway::plausibleEmail(p.email))
        return fail("\xC2\xAB " + p.email + " \xC2\xBB n'a pas la forme d'une adresse (nom@domaine.fr) : corrige-la, ou laisse-la vide");
    for (const std::string* s : {&p.email, &p.name, &p.company})
        for (unsigned char ch : *s)
            if (ch < 0x20) return fail("un champ contient un caract\xC3\xA8re de contr\xC3\xB4le");
    core::json::Value v = core::json::Value::object();
    v.set("format", "xpg-profil/1");
    v.set("courriel", p.email);
    v.set("nom", p.name);
    v.set("societe", p.company);
    if (!p.verified.empty() && p.verified == p.email) v.set("courriel_verifie", p.verified);   // 1.38.0
    const auto path = pathOf(profilePath());
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << core::json::write(v, true) << "\n";
        if (!out.good()) return fail("le profil n'a pas pu \xC3\xAAtre \xC3\xA9" "crit (" + profilePath() + ")");
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return fail("le profil n'a pas pu \xC3\xAAtre \xC3\xA9" "crit (" + profilePath() + ")");
    }
    return true;
}

bool plausibleEmail(const std::string& email) { return licensing::gateway::plausibleEmail(trimmed(email)); }

void noteEmailReached(const std::string& address) {
    Profile p = loadProfile();
    const std::string a = trimmed(address);
    if (a.empty() || p.email != a || p.verified == a) return;
    p.verified = a;
    (void)saveProfile(p, nullptr);
}

// ============================================================ 1.37.0 : l'apercu d'une fonctionnalite ==
void setFeatureImagesFolder(std::string folder) {
    std::lock_guard<std::mutex> lock(g_featureMutex);
    g_featureFolder = std::move(folder);
}

FeaturePreview featurePreview(const std::string& featureId) {
    FeaturePreview out;
    // Un identifiant du registre ([a-z0-9._-]) : jamais un chemin.
    if (featureId.empty() || featureId.size() > 64 ||
        !std::all_of(featureId.begin(), featureId.end(), [](char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        }) ||
        featureId.find("..") != std::string::npos)
        return out;
    std::vector<std::filesystem::path> dirs;
    {
        std::lock_guard<std::mutex> lock(g_featureMutex);
        if (!g_featureFolder.empty()) dirs.push_back(pathOf(g_featureFolder));
    }
    std::error_code ec;
    dirs.push_back(std::filesystem::absolute(std::filesystem::path("resources") / "licences" / "fonctions", ec));
    for (const auto& d : dirs) {
        if (!std::filesystem::is_directory(d, ec)) continue;
        // 1.44.0 : toutes les captures - <id>.png, puis <id>-2.png, <id>-3.png... (la premiere qui manque arrete)
        for (int i = 1; i <= 8; ++i) {
            const auto img = d / (featureId + (i == 1 ? std::string(".png") : "-" + std::to_string(i) + ".png"));
            if (!std::filesystem::is_regular_file(img, ec)) break;
            const auto u8 = img.u8string();
            CaptureShot s;
            s.imagePath.assign(u8.begin(), u8.end());
            out.shots.push_back(std::move(s));
        }
        if (!out.shots.empty()) out.imagePath = out.shots.front().imagePath;
        std::string text;
        core::json::Value v;
        if (readAll(d / "descriptions.json", text, 256 * 1024) && core::json::parse(text, v, nullptr) && v.isObject()) {
            if (const auto* all = v.find("fonctions"); all && all->isObject()) out.description = trimmed(all->str(featureId));
            // les legendes et les bulles, capture par capture
            if (const auto* caps = v.find("captures"); caps && caps->isObject())
                if (const auto* list = caps->find(featureId); list && list->isArray())
                    for (std::size_t i = 0; i < list->items().size() && i < out.shots.size(); ++i) {
                        const auto& o = list->items()[i];
                        if (!o.isObject()) continue;
                        out.shots[i].caption = trimmed(o.str("legende"));
                        if (const auto* spots = o.find("bulles"); spots && spots->isArray())
                            for (const auto& s : spots->items()) {
                                if (!s.isObject() || s.str("texte").empty()) continue;
                                CaptureSpot cs;
                                cs.x = static_cast<float>(s.num("x"));
                                cs.y = static_cast<float>(s.num("y"));
                                cs.w = static_cast<float>(s.num("l"));
                                cs.h = static_cast<float>(s.num("h"));
                                cs.tip = trimmed(s.str("texte"));
                                if (cs.w > 0.f && cs.h > 0.f && out.shots[i].spots.size() < 12) out.shots[i].spots.push_back(std::move(cs));
                            }
                    }
        }
        if (!out.imagePath.empty() || !out.description.empty()) break;
    }
    return out;
}

void showNotices() {
    if (!g_notices.exchange(true)) syncStateNotices();
}

void startNotices(bool online) {
    g_notices = true;
    syncStateNotices();
    if (online) service::startRunner();
}

void shutdown() {
    service::shutdown();
    updates::shutdown();   // 1.44.0 : un telechargement en cours s'arrete
}

bool noticesShown() noexcept { return g_notices; }   // 1.44.0

// ============================================================ 1.38.0 : les notifications de la licence ==
namespace watch {

namespace {
std::mutex  g_watchMutex;
std::string g_seenFile;            // vide : seenPath()
double      g_watchNext{0};
std::string g_watchSig;            // la derniere signature regardee (rien de change : rien a comparer)

std::string labelOf(const Seen& s, const std::string& id) {
    for (const auto& [k, v] : s.names)
        if (k == id) return v.empty() ? id : v;
    return id;
}
std::string listOf(const Seen& a, const Seen& b, const std::vector<std::string>& ids) {
    std::string out;
    for (const auto& id : ids) {
        std::string l = labelOf(a, id);
        if (l == id) l = labelOf(b, id);
        out += (out.empty() ? "" : ", ") + l;
    }
    return out.empty() ? std::string("aucune") : out;
}
// 1.39.0 : le logiciel d'une fonctionnalite (API Studio, IHM Studio...).
std::string productOf(const Seen& a, const Seen& b, const std::string& id) {
    for (const auto* s : {&a, &b})
        for (const auto& [k, v] : s->products)
            if (k == id && !v.empty()) return v;
    return {};
}
std::string daysText(int d) { return d == 0 ? std::string("sans fin") : std::to_string(d) + (d > 1 ? " jours" : " jour"); }
const Offer* offerOf(const Seen& s, const std::string& id) {
    for (const auto& o : s.offers)
        if (o.id == id) return &o;
    return nullptr;
}
std::string dayOf(std::int64_t at) { return service::dayText(static_cast<licensing::Seconds>(at)); }
} // namespace

std::string noticesPath() {
    const auto p = std::filesystem::path(Settings::defaultPath()).parent_path() / "notifications.json";
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

std::string seenPath() {
    const auto p = std::filesystem::path(Settings::defaultPath()).parent_path() / "vu-licence.json";
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

void setSeenFile(std::string file) {
    std::lock_guard<std::mutex> lk(g_watchMutex);
    g_seenFile = std::move(file);
    g_watchSig.clear();
    g_watchNext = 0;
}

Seen snapshot(const licensing::RegistryView* reg, const licensing::LicenseState& st) {
    Seen s;
    s.known = true;
    if (reg) {
        s.registry = reg->number;
        for (const auto& f : reg->features) {
            s.names.emplace_back(f.id, f.label);
            s.products.emplace_back(f.id, f.product);   // 1.39.0 : la colonne Logiciel
        }
        for (const auto& e : reg->editions) {
            if (e.id == reg->unlicensedEdition) continue;   // l'edition sans licence : rien a annoncer
            Offer o;
            o.id = e.id;
            o.label = e.label.empty() ? e.id : e.label;
            o.price = e.price;
            o.promoPrice = e.promoPrice;
            o.promoUntil = e.promoUntil;
            o.features = e.features;
            std::sort(o.features.begin(), o.features.end());
            o.retired = e.retired;
            o.devices = e.maxDevices;    // 1.39.0
            o.days = e.durationDays;
            s.offers.push_back(std::move(o));
        }
        for (const auto& a : reg->announcements) s.current.emplace_back(a.id, a.title, a.text, a.edition, a.until);
    }
    s.licenseId = st.licenseId;
    s.edition = st.licensedEdition;
    s.editionLabel = st.editionLabel;
    s.expiresAt = st.licenseId.empty() ? 0 : static_cast<std::int64_t>(st.expiresAt);
    s.status = static_cast<int>(st.status);
    return s;
}

std::vector<notices::Item> compare(const Seen& before, Seen& now, std::int64_t at) {
    std::vector<notices::Item> out;
    const auto add = [&](std::string category, std::string title, std::string detail, std::string tone, std::string key, int tab) {
        notices::Item n;
        n.at = at;
        n.category = std::move(category);
        n.title = std::move(title);
        n.detail = std::move(detail);
        n.tone = std::move(tone);
        n.key = std::move(key);
        if (tab >= 0) {
            n.button = tab == kTabRequest ? "Voir les offres" : tab == kTabEditions ? "Comparer les offres" : "Voir ma licence";
            n.action = notices::toLicence(tab);
        }
        out.push_back(std::move(n));
    };
    const std::string q1 = "\xC2\xAB ", q2 = " \xC2\xBB";
    // ---- les offres (un registre plus recent, deja vu un autre avant) ----
    if (before.known && before.registry != 0 && now.registry > before.registry) {
        const std::string rk = std::to_string(now.registry);
        for (const auto& o : now.offers) {
            const Offer* old = offerOf(before, o.id);
            const bool mine = !now.edition.empty() && o.id == now.edition;
            const std::string tag = mine ? std::string(" (ta licence)") : std::string();
            if (!old) {
                if (o.retired) continue;
                add("Offres", "Nouvelle offre : " + o.label,
                    "Elle contient : " + listOf(now, before, o.features) + "." + (o.price.empty() ? std::string() : " Prix : " + o.price + "."), "info",
                    "offre:" + o.id + ":" + rk, kTabRequest);
                continue;
            }
            if (o.retired && !old->retired) {
                add("Offres", "Offre retir\xC3\xA9" "e : " + o.label + tag,
                    mine ? std::string("Ta licence reste valable telle qu'elle est ; l'\xC3\xA9" "diteur ne propose plus cette offre aux nouveaux clients.")
                         : std::string("L'\xC3\xA9" "diteur ne propose plus cette offre."),
                    "info", "offre:" + o.id + ":" + rk, kTabEditions);
                continue;
            }
            // 1.39.0 : les fonctionnalites, le prix, les PC, la duree - ce qui change, precisement : un tableau dans la
            // notification (les changements) et sa fenetre (toute l'offre, l'avant et l'apres, la capture de chacune).
            const bool priceMoved = !o.price.empty() && !old->price.empty() && o.price != old->price;
            const bool devicesMoved = o.devices >= 0 && old->devices >= 0 && o.devices != old->devices;
            const bool daysMoved = o.days >= 0 && old->days >= 0 && o.days != old->days;
            if (o.features != old->features || priceMoved || devicesMoved || daysMoved) {
                std::vector<std::string> added, removed;
                std::set_difference(o.features.begin(), o.features.end(), old->features.begin(), old->features.end(), std::back_inserter(added));
                std::set_difference(old->features.begin(), old->features.end(), o.features.begin(), o.features.end(), std::back_inserter(removed));
                const std::string key = "offre:" + o.id + ":" + rk;
                std::vector<std::string> parts;
                if (!removed.empty())
                    parts.push_back(std::to_string(removed.size()) + (removed.size() > 1 ? " fonctionnalit\xC3\xA9s retir\xC3\xA9" "es : " : " fonctionnalit\xC3\xA9 retir\xC3\xA9" "e : ") +
                                    listOf(before, now, removed));
                if (!added.empty())
                    parts.push_back(std::to_string(added.size()) + (added.size() > 1 ? " ajout\xC3\xA9" "es : " : " ajout\xC3\xA9" "e : ") + listOf(now, before, added));
                core::json::Value values = core::json::Value::array();
                core::json::Value table = core::json::Value::array();
                const auto tableRow = [&](std::vector<std::string> cells, const char* tone) {
                    core::json::Value r = core::json::Value::object();
                    core::json::Value c = core::json::Value::array();
                    for (auto& x : cells) c.push(std::move(x));
                    r.set("cellules", std::move(c));
                    if (*tone) r.set("ton", tone);
                    table.push(std::move(r));
                };
                const auto value = [&](const char* label, const std::string& a, const std::string& b) {
                    core::json::Value v = core::json::Value::object();
                    v.set("nom", label);
                    v.set("avant", a);
                    v.set("apres", b);
                    values.push(std::move(v));
                    parts.push_back(std::string(label) + " : " + a + " \xE2\x86\x92 " + b);
                };
                const std::string yes = "\xE2\x9C\x93 incluse", no = "\xE2\x9C\x97 retir\xC3\xA9" "e", none = "\xE2\x80\x94", plus = "\xE2\x9C\x93 ajout\xC3\xA9" "e";
                for (const auto& id : removed) tableRow({labelOf(before, id) == id ? labelOf(now, id) : labelOf(before, id), productOf(before, now, id), yes, no}, "error");
                for (const auto& id : added) tableRow({labelOf(now, id) == id ? labelOf(before, id) : labelOf(now, id), productOf(now, before, id), none, plus}, "ok");
                if (priceMoved) value("Prix", old->price, o.price);
                if (devicesMoved) value("PC au plus", std::to_string(old->devices), std::to_string(o.devices));
                if (daysMoved) value("Dur\xC3\xA9" "e", daysText(old->days), daysText(o.days));
                for (const auto& v : values.items()) tableRow({v.str("nom"), std::string(), v.str("avant"), v.str("apres")}, "");
                // toute l'offre, pour la fenetre : les changements d'abord, puis ce qui reste
                core::json::Value rows = core::json::Value::array();
                const auto rowOf = [&](const std::string& id, bool a, bool b) {
                    core::json::Value r = core::json::Value::object();
                    r.set("id", id);
                    r.set("nom", labelOf(now, id) == id ? labelOf(before, id) : labelOf(now, id));
                    r.set("logiciel", productOf(now, before, id));
                    r.set("avant", a);
                    r.set("apres", b);
                    rows.push(std::move(r));
                };
                for (const auto& id : removed) rowOf(id, true, false);
                for (const auto& id : added) rowOf(id, false, true);
                for (const auto& id : o.features)
                    if (std::binary_search(old->features.begin(), old->features.end(), id)) rowOf(id, true, true);
                core::json::Value offer = core::json::Value::object();
                offer.set("id", o.id);
                offer.set("nom", o.label);
                offer.set("ta_licence", mine);
                offer.set("registre", now.registry);
                offer.set("lignes", std::move(rows));
                offer.set("valeurs", std::move(values));
                core::json::Value cols = core::json::Value::array();
                for (const char* c : {"Fonctionnalit\xC3\xA9", "Logiciel", "Avant", "Apr\xC3\xA8s"}) cols.push(c);
                core::json::Value t = core::json::Value::object();
                t.set("colonnes", std::move(cols));
                t.set("lignes", std::move(table));
                core::json::Value data = core::json::Value::object();
                data.set("tableau", std::move(t));
                data.set("offre", std::move(offer));
                std::string d;
                for (const auto& p : parts) d += (d.empty() ? "" : " \xC2\xB7 ") + p;
                d += ".";
                if (mine) d += " Ta licence garde ce qu'elle contient jusqu'\xC3\xA0 son renouvellement.";
                add("Offres", "Offre " + q1 + o.label + q2 + " modifi\xC3\xA9" "e" + tag, d, mine && !removed.empty() ? "warning" : "info", key, -1);
                out.back().button = "Voir le d\xC3\xA9tail";
                out.back().action = notices::toDetail(key);
                out.back().data = core::json::write(data);
            }
        }
        for (const auto& old : before.offers)
            if (!offerOf(now, old.id) && !old.retired)
                add("Offres", "Offre retir\xC3\xA9" "e : " + old.label, "L'\xC3\xA9" "diteur ne propose plus cette offre.", "info", "offre:" + old.id + ":" + rk,
                    kTabEditions);
    }
    // ---- les annonces de l'editeur (une seule fois chacune ; une annonce finie n'est plus montree) ----
    std::vector<std::string> seen;
    for (const auto& [id, title, text, edition, until] : now.current) {
        const bool already = std::find(before.announcements.begin(), before.announcements.end(), id) != before.announcements.end();
        seen.push_back(id);
        if (already || (until != 0 && until <= at)) continue;
        std::string d = text;
        if (until != 0) d += (d.empty() ? "" : " ") + std::string("(jusqu'au ") + dayOf(until) + ")";
        add("Annonce", title, d, "info", "annonce:" + id, edition.empty() ? -1 : kTabRequest);
    }
    now.announcements = std::move(seen);   // seulement celles du registre courant (une annonce retiree est oubliee)
    // ---- la licence ----
    now.remindedEnd = before.remindedEnd;
    using S = licensing::LicenseStatus;
    if (before.known) {
        const std::string lk = now.licenseId + ":" + std::to_string(now.expiresAt);
        const std::string name = now.editionLabel.empty() ? now.edition : now.editionLabel;
        if (!now.licenseId.empty() && now.licenseId != before.licenseId)
            add("Licence", "Licence install\xC3\xA9" "e : " + name,
                "Licence " + now.licenseId + (now.expiresAt ? ", jusqu'au " + dayOf(now.expiresAt) : std::string(", sans fin")) + ".", "ok",
                "licence:installee:" + lk, kTabMine);
        else if (!now.licenseId.empty() && now.licenseId == before.licenseId) {
            if (now.edition != before.edition && !now.edition.empty())
                add("Licence", "Ta licence passe en " + name, "Licence " + now.licenseId + " : l'offre " + name + ".", "ok",
                    "licence:offre:" + lk + ":" + now.edition, kTabMine);
            else if (now.expiresAt > before.expiresAt && before.expiresAt != 0)
                add("Licence", "Licence renouvel\xC3\xA9" "e jusqu'au " + dayOf(now.expiresAt), "Licence " + now.licenseId + " (" + name + ").", "ok",
                    "licence:renouvelee:" + lk, kTabMine);
            if (now.status != before.status && now.status == static_cast<int>(S::Revoked))
                add("Licence", "Licence r\xC3\xA9voqu\xC3\xA9" "e", "L'\xC3\xA9" "diteur a r\xC3\xA9voqu\xC3\xA9 la licence " + now.licenseId + " : contacte-le si c'est une erreur.",
                    "error", "licence:revoquee:" + now.licenseId, kTabMine);
            else if (now.status != before.status && now.status == static_cast<int>(S::Expired))
                add("Licence", "Licence finie", "La licence " + now.licenseId + " a pris fin : demande \xC3\xA0 l'\xC3\xA9" "diteur de la renouveler.", "warning",
                    "licence:finie:" + lk, kTabRequest);
        }
    }
    // Bientot finie : 14 jours avant (une fois par fin).
    if (!now.licenseId.empty() && now.expiresAt > at && now.expiresAt - at <= 14 * 86400 && now.remindedEnd != now.expiresAt &&
        (now.status == static_cast<int>(S::Valid) || now.status == static_cast<int>(S::Grace))) {
        const auto days = (now.expiresAt - at + 86399) / 86400;
        add("Licence", "Ta licence finit le " + dayOf(now.expiresAt),
            "Encore " + std::to_string(days) + (days > 1 ? " jours" : " jour") + " : pense \xC3\xA0 la faire renouveler par l'\xC3\xA9" "diteur.", "warning",
            "licence:bientot:" + now.licenseId + ":" + std::to_string(now.expiresAt), kTabMine);
        now.remindedEnd = now.expiresAt;
    }
    return out;
}

std::optional<OfferChange> offerChangeOf(const std::string& data) {
    core::json::Value v;
    if (data.empty() || !core::json::parse(data, v, nullptr) || !v.isObject()) return std::nullopt;
    const auto* o = v.find("offre");
    if (!o || !o->isObject() || o->str("id").empty()) return std::nullopt;
    OfferChange c;
    c.id = o->str("id");
    c.label = o->str("nom", c.id);
    c.mine = o->flag("ta_licence");
    c.registry = static_cast<std::uint64_t>(std::max<std::int64_t>(0, o->integer("registre")));
    for (const auto& r : o->at("lignes").items()) {
        if (!r.isObject() || r.str("id").empty()) continue;
        c.rows.push_back({r.str("id"), r.str("nom", r.str("id")), r.str("logiciel"), r.flag("avant"), r.flag("apres")});
        if (c.rows.size() >= 500) break;
    }
    for (const auto& x : o->at("valeurs").items())
        if (x.isObject()) c.values.push_back({x.str("nom"), x.str("avant"), x.str("apres")});
    return c;
}

bool loadSeen(const std::string& file, Seen& out) {
    out = Seen{};
    std::string text;
    core::json::Value v;
    if (!readAll(pathOf(file), text, 1024 * 1024) || !core::json::parse(text, v, nullptr) || !v.isObject() || v.str("format") != "xpg-vu-licence/1")
        return false;
    out.known = true;
    out.registry = static_cast<std::uint64_t>(std::max<std::int64_t>(0, v.integer("registre")));
    for (const auto& o : v.at("offres").items()) {
        Offer x;
        x.id = o.str("id");
        x.label = o.str("nom");
        x.price = o.str("prix");
        x.promoPrice = o.str("prix_reduit");
        x.promoUntil = o.integer("reduction_fin");
        x.retired = o.flag("retiree");
        x.devices = static_cast<int>(o.integer("postes", -1));   // 1.39.0 : -1 (un fichier de la 1.38) : pas retenu
        x.days = static_cast<int>(o.integer("duree", -1));
        for (const auto& f : o.at("fonctions").items()) x.features.push_back(f.asString());
        std::sort(x.features.begin(), x.features.end());
        if (!x.id.empty()) out.offers.push_back(std::move(x));
    }
    for (const auto& [k, n] : v.at("noms").members()) out.names.emplace_back(k, n.asString());
    for (const auto& [k, n] : v.at("logiciels").members()) out.products.emplace_back(k, n.asString());   // 1.39.0
    for (const auto& a : v.at("annonces").items()) out.announcements.push_back(a.asString());
    const auto& l = v.at("licence");
    out.licenseId = l.str("id");
    out.edition = l.str("offre");
    out.editionLabel = l.str("nom");
    out.expiresAt = l.integer("fin");
    out.status = static_cast<int>(l.integer("etat", -1));
    out.remindedEnd = l.integer("rappel");
    return true;
}

bool saveSeen(const std::string& file, const Seen& s) {
    core::json::Value v = core::json::Value::object();
    v.set("format", "xpg-vu-licence/1");
    v.set("registre", s.registry);
    core::json::Value offers = core::json::Value::array();
    for (const auto& o : s.offers) {
        core::json::Value x = core::json::Value::object();
        x.set("id", o.id);
        x.set("nom", o.label);
        if (!o.price.empty()) x.set("prix", o.price);
        if (!o.promoPrice.empty()) {
            x.set("prix_reduit", o.promoPrice);
            x.set("reduction_fin", o.promoUntil);
        }
        if (o.retired) x.set("retiree", true);
        if (o.devices >= 0) x.set("postes", o.devices);   // 1.39.0
        if (o.days >= 0) x.set("duree", o.days);
        core::json::Value f = core::json::Value::array();
        for (const auto& id : o.features) f.push(id);
        x.set("fonctions", std::move(f));
        offers.push(std::move(x));
    }
    v.set("offres", std::move(offers));
    core::json::Value names = core::json::Value::object();
    for (const auto& [k, n] : s.names) names.set(k, n);
    v.set("noms", std::move(names));
    core::json::Value products = core::json::Value::object();   // 1.39.0
    for (const auto& [k, n] : s.products) products.set(k, n);
    v.set("logiciels", std::move(products));
    core::json::Value anns = core::json::Value::array();
    for (const auto& a : s.announcements) anns.push(a);
    v.set("annonces", std::move(anns));
    core::json::Value l = core::json::Value::object();
    l.set("id", s.licenseId);
    l.set("offre", s.edition);
    l.set("nom", s.editionLabel);
    l.set("fin", s.expiresAt);
    l.set("etat", s.status);
    l.set("rappel", s.remindedEnd);
    v.set("licence", std::move(l));
    const auto path = pathOf(file);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << core::json::write(v, true) << "\n";
        if (!out.good()) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

std::size_t check(bool force) {
    std::string file;
    {
        std::lock_guard<std::mutex> lk(g_watchMutex);
        const double t = nowSeconds();
        if (!force && t < g_watchNext) return 0;
        g_watchNext = t + 3.0;
        file = g_seenFile.empty() ? seenPath() : g_seenFile;
    }
    const auto st = licensing::getLicenseState();
    const std::int64_t at = static_cast<std::int64_t>(std::time(nullptr));
    // Rien de change (le registre, la licence, le jour) : rien a comparer - pas de copie du registre a chaque fois.
    const std::string sig = std::to_string(st.registryNumber) + "|" + st.licenseId + "|" + st.licensedEdition + "|" + std::to_string(st.expiresAt) + "|" +
                            std::to_string(static_cast<int>(st.status)) + "|" + std::to_string(at / 86400);
    {
        std::lock_guard<std::mutex> lk(g_watchMutex);
        if (!force && sig == g_watchSig) return 0;
        g_watchSig = sig;
    }
    const auto reg = licensing::getRegistry();
    Seen before;
    (void)loadSeen(file, before);
    Seen now = snapshot(reg ? &*reg : nullptr, st);
    if (!before.known) {   // le premier lancement : retenir sans rien dire (sauf les annonces en cours)
        Seen empty;
        auto items = compare(empty, now, at);
        std::erase_if(items, [](const notices::Item& n) { return n.category != "Annonce" && n.key.rfind("licence:bientot:", 0) != 0; });
        for (auto& n : items) (void)notices::post(std::move(n));
        (void)saveSeen(file, now);
        return items.size();
    }
    if (!reg) {   // pas de registre lisible : garder les offres et les annonces deja vues
        now.registry = before.registry;
        now.offers = before.offers;
        now.names = before.names;
        for (const auto& id : before.announcements) now.current.emplace_back(id, std::string(), std::string(), std::string(), 1);
    }
    auto items = compare(before, now, at);
    if (!reg) now.announcements = before.announcements;
    if (now.registry < before.registry) {   // un registre plus ancien (une restauration) : on garde le plus recent vu
        now.registry = before.registry;
        now.offers = before.offers;
        now.names = before.names;
    }
    for (auto& n : items) {
        // 1.39.0 : une licence revoquee pendant que le logiciel tourne (une liste de revocation adoptee) : la fenetre aussi.
        // 1.44.0 : et une seule notification pour cet evenement - celle de la licence perdue, si elle est la.
        if (n.key.rfind("licence:revoquee:", 0) == 0) {
            queueLoss(now.licenseId, n.detail);
            const std::string lost = "licence:perdue:" + now.licenseId;
            const auto all = notices::list();
            if (std::any_of(all.begin(), all.end(), [&lost](const notices::Item& x) { return x.key == lost; })) continue;
        }
        (void)notices::post(std::move(n));
    }
    (void)saveSeen(file, now);
    return items.size();
}

} // namespace watch

} // namespace app::licence
