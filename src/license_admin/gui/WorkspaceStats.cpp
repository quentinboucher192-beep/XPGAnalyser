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

#include <memory>
#include <string>

namespace license_admin::gui {

namespace {

OpResult fromResult(const online::Result& x) {
    OpResult r;
    r.ok = x.ok;
    r.message = x.message;
    r.details = x.details;
    return r;
}

// Le client du service ouvert avec la cle qui signe (l'admin+ ou un admin) ; `plusOnly` : l'admin+ seulement.
OpResult openStats(const std::string& folder, Seconds now, bool plusOnly, std::unique_ptr<online::Client>& client, detail::Caller& caller) {
    std::string why;
    if (!detail::loadCaller(folder, caller, &why)) return OpResult::fail(why);
    if (plusOnly) {
        if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    } else if (caller.role != Role::Plus && caller.role != Role::Admin) {
        return OpResult::fail("ce poste n'a pas de cl\xC3\xA9 qui signe (ni l'admin+, ni un admin de la liste)");
    }
    if (!online::configured(folder)) return OpResult::fail("le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne)");
    if (!online::openClient(folder, *caller.key, detail::optionsFor(caller, now), client, &why)) return OpResult::fail(why);
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

OpResult Workspace::statsSnapshot(stats::SnapshotReply& out, std::string* raw) const {
    std::unique_ptr<online::Client> c;
    detail::Caller caller;
    if (OpResult r = openStats(folder_, now(), true, c, caller); !r.ok) return r;
    return fromResult(stats::takeSnapshot(*c, out, raw));
}

} // namespace license_admin::gui
