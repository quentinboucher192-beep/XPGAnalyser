// =============================================================================
//  license_admin/gui/WorkspaceStats.cpp - 1.47.0 : LES STATISTIQUES ET LE GLOBE
//  DANS LE POSTE ADMIN (Workspace.hpp ; docs/STATISTIQUES.md)
// -----------------------------------------------------------------------------
//  Les appels LENTS (le reseau, a lancer par Background) que les pages
//  Statistiques, le globe, la fiche d'un client et celles des admins font au
//  service : signes par la cle qui signe (l'admin+, ou un admin : le service ne
//  lui rend que ce que ses droits permettent). Rien n'est garde sur ce poste :
//  les chiffres se relisent quand la page le demande. Un service d'avant la
//  1.47.0 : la phrase << deploie la 1.47.0 >> (stats::serviceTooOldText).
// =============================================================================
#include "Workspace.hpp"

#include "../../deploy/Crypto.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace license_admin::gui {

namespace {

OpResult fromResult(const online::Result& x) {
    OpResult r;
    r.ok = x.ok;
    r.message = x.message;
    r.details = x.details;
    return r;
}

// ---- 1.48.1 : ce qui est supprime ici et que le service doit savoir (stats-a-envoyer.json, dans le dossier) ----
constexpr const char* kStatsOutbox = "stats-a-envoyer.json";
constexpr const char* kStatsOutboxFormat = "xpg-stats-a-envoyer/1";

std::mutex& outboxLock() {
    static std::mutex m;
    return m;
}
std::filesystem::path outboxPath(const std::string& folder) {
    return std::filesystem::path(std::u8string(folder.begin(), folder.end())) / kStatsOutbox;
}
std::string lowered(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
void addOnce(std::vector<std::string>& v, const std::string& x) {
    if (x.empty()) return;
    for (const auto& y : v)
        if (lowered(y) == lowered(x)) return;
    v.push_back(x);
}
void dropFrom(std::vector<std::string>& v, const std::string& x) {
    std::erase_if(v, [&](const std::string& y) { return lowered(y) == lowered(x); });
}
// Ajouter `more` a `ex` : supprimer un nom l'ote de << remettre >>, et l'inverse.
void merge(stats::Exclusions& ex, const stats::Exclusions& more) {
    for (const auto& x : more.deleteClients) {
        dropFrom(ex.restoreClients, x);
        addOnce(ex.deleteClients, x);
    }
    for (const auto& x : more.restoreClients) {
        dropFrom(ex.deleteClients, x);
        addOnce(ex.restoreClients, x);
    }
    for (const auto& x : more.deleteLicences) {
        dropFrom(ex.restoreLicences, x);
        addOnce(ex.deleteLicences, x);
    }
    for (const auto& x : more.restoreLicences) {
        dropFrom(ex.deleteLicences, x);
        addOnce(ex.restoreLicences, x);
    }
}
stats::Exclusions readOutbox(const std::string& folder) {
    stats::Exclusions ex;
    std::error_code ec;
    const auto path = outboxPath(folder);
    if (!std::filesystem::exists(path, ec)) return ex;
    deploy::crypto::Bytes raw;
    const auto u = path.u8string();
    if (!deploy::crypto::readFile(std::string(u.begin(), u.end()), raw, nullptr, 4u * 1024u * 1024u)) return ex;
    core::json::Value v;
    if (!core::json::parse(std::string(raw.begin(), raw.end()), v, nullptr) || !v.isObject() || v.str("format") != kStatsOutboxFormat) return ex;
    const auto list = [&](const char* group, const char* key, std::vector<std::string>& out) {
        const core::json::Value* g = v.find(group);
        const core::json::Value* a = g && g->isObject() ? g->find(key) : nullptr;
        if (a && a->isArray())
            for (const auto& x : a->items())
                if (x.isString()) addOnce(out, x.asString());
    };
    list("supprimer", "clients", ex.deleteClients);
    list("supprimer", "licences", ex.deleteLicences);
    list("remettre", "clients", ex.restoreClients);
    list("remettre", "licences", ex.restoreLicences);
    return ex;
}
void writeOutbox(const std::string& folder, const stats::Exclusions& ex) {
    std::error_code ec;
    const auto path = outboxPath(folder);
    if (ex.empty()) {
        std::filesystem::remove(path, ec);
        return;
    }
    core::json::Value v;
    if (!core::json::parse(ex.body(), v, nullptr) || !v.isObject()) return;
    v.set("format", kStatsOutboxFormat);
    const std::string text = core::json::write(v, true) + "\n";
    const auto u = path.u8string();
    (void)deploy::crypto::writePrivateFile(std::string(u.begin(), u.end()), deploy::crypto::Bytes(text.begin(), text.end()), nullptr);
}
// Prendre ce qui attend (le fichier vide) ; un echec le remet (requeue).
stats::Exclusions takeOutbox(const std::string& folder) {
    std::lock_guard<std::mutex> lock(outboxLock());
    stats::Exclusions ex = readOutbox(folder);
    if (!ex.empty()) writeOutbox(folder, {});
    return ex;
}
void requeue(const std::string& folder, const stats::Exclusions& ex) {
    if (ex.empty()) return;
    std::lock_guard<std::mutex> lock(outboxLock());
    stats::Exclusions now = ex;
    merge(now, readOutbox(folder));   // ce qui est arrive depuis gagne
    writeOutbox(folder, now);
}
// Avant de lire les statistiques : ce qui attend part au service (un refus pour un droit : oublie ; le reseau : plus tard).
void flushOutbox(const std::string& folder, online::Client& c) {
    const stats::Exclusions ex = takeOutbox(folder);
    if (ex.empty()) return;
    stats::ReevaluateReply rep;
    const online::Result r = stats::reevaluate(c, ex, rep);
    if (!r.ok && r.message.find("droit") == std::string::npos) requeue(folder, ex);
}

// Le client du service ouvert avec la cle qui signe (l'admin+ ou un admin) ; `plusOnly` : l'admin+ seulement. 1.48.1 : ce
// qui est supprime ici et attend part d'abord (les chiffres lus ensuite le savent).
OpResult openStats(const std::string& folder, Seconds now, bool plusOnly, std::unique_ptr<online::Client>& client, detail::Caller& caller, bool flush = true) {
    std::string why;
    if (!detail::loadCaller(folder, caller, &why)) return OpResult::fail(why);
    if (plusOnly) {
        if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    } else if (caller.role != Role::Plus && caller.role != Role::Admin) {
        return OpResult::fail("ce poste n'a pas de cl\xC3\xA9 qui signe (ni l'admin+, ni un admin de la liste)");
    }
    if (!online::configured(folder)) return OpResult::fail("le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne)");
    if (!online::openClient(folder, *caller.key, detail::optionsFor(caller, now), client, &why)) return OpResult::fail(why);
    if (flush) flushOutbox(folder, *client);
    return OpResult::done({});
}

} // namespace

// ============================================================================ sans reseau
bool Workspace::serviceKnowsStats() const {
    return service_.hasState && service_.state.reached && stats::serviceKnowsStats(service_.state.version);
}

std::string Workspace::statsServiceNote() const {
    if (!service_.hasState || !service_.state.reached) return {};
    return stats::serviceTooOldText(service_.state.version);
}

// ============================================================================ LENT
OpResult Workspace::statsSeries(const stats::SeriesQuery& q, stats::SeriesReply& out, std::string* raw) const {
    std::unique_ptr<online::Client> c;
    detail::Caller caller;
    if (OpResult r = openStats(folder_, now(), false, c, caller); !r.ok) return r;
    return fromResult(stats::fetchSeries(*c, q, out, raw));
}

OpResult Workspace::statsUpdates(const stats::UpdatesQuery& q, stats::UpdatesReply& out, std::string* raw) const {
    std::unique_ptr<online::Client> c;
    detail::Caller caller;
    if (OpResult r = openStats(folder_, now(), false, c, caller); !r.ok) return r;
    return fromResult(stats::fetchUpdates(*c, q, out, raw));
}

OpResult Workspace::statsAdmins(const stats::Period& period, stats::AdminsReply& out, std::string* raw) const {
    std::unique_ptr<online::Client> c;
    detail::Caller caller;
    if (OpResult r = openStats(folder_, now(), false, c, caller); !r.ok) return r;
    return fromResult(stats::fetchAdmins(*c, period, out, raw));
}

OpResult Workspace::statsGlobe(int periodDays, stats::GlobeReply& out, std::string* raw) const {
    std::unique_ptr<online::Client> c;
    detail::Caller caller;
    if (OpResult r = openStats(folder_, now(), false, c, caller); !r.ok) return r;
    return fromResult(stats::fetchGlobe(*c, periodDays, out, raw));
}

OpResult Workspace::statsLive(Seconds since, stats::LiveReply& out, std::string* raw) const {
    std::unique_ptr<online::Client> c;
    detail::Caller caller;
    if (OpResult r = openStats(folder_, now(), false, c, caller); !r.ok) return r;
    return fromResult(stats::fetchLive(*c, since, out, raw));
}

// ============================================================================ 1.48.1 : la coherence
void Workspace::queueStatsExclusion(const stats::Exclusions& ex) const {
    if (!ex.empty()) {
        std::lock_guard<std::mutex> lock(outboxLock());
        stats::Exclusions all = readOutbox(folder_);
        merge(all, ex);
        writeOutbox(folder_, all);
    }
    changes::bump();
}

stats::Exclusions Workspace::pendingStatsExclusions() const {
    std::lock_guard<std::mutex> lock(outboxLock());
    return readOutbox(folder_);
}

OpResult Workspace::statsReevaluate(stats::ReevaluateReply& out, std::string* raw) const {
    std::unique_ptr<online::Client> c;
    detail::Caller caller;
    if (OpResult r = openStats(folder_, now(), false, c, caller, false); !r.ok) return r;
    // ce qui attend, et tout ce qui est dans la corbeille de ce poste (ce que ses droits lui laissent dire)
    const stats::Exclusions pending = takeOutbox(folder_);
    stats::Exclusions ex = pending;
    std::vector<std::string> ofClients;
    if (can("client_supprimer"))
        for (const auto& d : deletedClients()) {
            stats::Exclusions one;
            one.deleteClients.push_back(d.name);
            if (std::none_of(ex.restoreClients.begin(), ex.restoreClients.end(), [&](const std::string& x) { return lowered(x) == lowered(d.name); })) merge(ex, one);
            ofClients.insert(ofClients.end(), d.licenceIds.begin(), d.licenceIds.end());
        }
    if (can("licence_revoquer"))
        for (const auto& d : archive::deletedLicences(folder_))
            if (std::find(ofClients.begin(), ofClients.end(), d.id) == ofClients.end()) addOnce(ex.deleteLicences, d.id);
    const online::Result x = stats::reevaluate(*c, ex, out, raw);
    if (!x.ok) {
        requeue(folder_, pending);
        return fromResult(x);
    }
    const std::size_t n = out.corrections.size();
    core::json::Value d = core::json::Value::object();
    d.set("corrections", static_cast<double>(n));
    (void)detail::queueJournal(folder_, *caller.key, now(), "stats_reevaluer", "statistiques",
                               "a r\xC3\xA9\xC3\xA9valu\xC3\xA9 les statistiques (" + (n ? stats::reevaluateSummary(out) : std::string("aucune correction")) + ")",
                               core::json::write(d, false), {}, {}, nullptr);
    (void)detail::flushJournalWith(folder_, *c);
    changes::bump();
    return OpResult::done("Statistiques r\xC3\xA9\xC3\xA9valu\xC3\xA9" "es : " + stats::reevaluateSummary(out));
}

OpResult Workspace::statsSnapshot(stats::SnapshotReply& out, std::string* raw) const {
    std::unique_ptr<online::Client> c;
    detail::Caller caller;
    if (OpResult r = openStats(folder_, now(), true, c, caller); !r.ok) return r;
    return fromResult(stats::takeSnapshot(*c, out, raw));
}

} // namespace license_admin::gui
