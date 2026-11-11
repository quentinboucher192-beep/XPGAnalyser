// =============================================================================
//  license_admin/gui/WorkspaceDelete.cpp - 1.48.1 : SUPPRIMER, ET QUE CA SE
//  VOIE PARTOUT (Licences > Supprimer, Activations > Supprimer)
// -----------------------------------------------------------------------------
//  Une licence qui ne sert plus (revoquee ou finie) : son fichier dans la
//  corbeille de l'outil (comme la suppression d'un client, 1.42.0), une ligne au
//  journal des admins, effacee du service si elle est en ligne (ses PC liberes).
//  Une licence en vigueur ne se supprime pas : un Studio s'arreterait chez le
//  client (revoque-la d'abord).
//  Un PC qui ne sert plus (ni actif, ni en attente d'une decision que l'Admin
//  doit prendre ailleurs) : retenu dans pc-supprimes.json ; il revient s'il a une
//  activite plus recente. Le registre des activations n'est jamais reecrit
//  (l'Historique garde tout).
//  CE QUI EST SUPPRIME NE REVIENT PAS PAR UN AUTRE CHEMIN : withoutDeleted (les
//  PC d'une licence ou d'un client supprimes, les PC supprimes) sert a
//  Workspace::devices - donc Activations, Clients, le tableau de bord, la fiche
//  d'un client ; dropDeletedFromSnapshot (le releve du service) sert aux
//  licences en ligne, aux licences flottantes et aux compteurs.
// =============================================================================
#include "Workspace.hpp"

#include "../../core/Json.hpp"
#include "../../deploy/Paths.hpp"
#include "../../licensing/Time.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace {

namespace fs = std::filesystem;
using core::json::Value;

constexpr const char* kDeletedDevicesFile = "pc-supprimes.json";

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

struct DeletedDevice {
    std::string key;
    Seconds     at{0};
};

std::vector<DeletedDevice> readDeletedDevices(const std::string& folder) {
    std::vector<DeletedDevice> out;
    std::ifstream f(P(deploy::joinPath(folder, kDeletedDevicesFile)), std::ios::binary);
    if (!f) return out;
    const std::string text((std::istreambuf_iterator<char>(f)), {});
    Value v;
    if (!core::json::parse(text, v, nullptr) || !v.isObject()) return out;
    if (const Value* arr = v.find("pc"); arr && arr->isArray())
        for (const auto& x : arr->items()) {
            DeletedDevice d;
            d.key = x.str("cle");
            (void)licensing::parseIsoTime(x.str("supprime_le"), d.at);
            if (!d.key.empty()) out.push_back(std::move(d));
        }
    return out;
}

bool writeDeletedDevices(const std::string& folder, const std::vector<DeletedDevice>& list, std::string* why) {
    Value root = Value::object();
    root.set("format", "xpg-pc-supprimes/1");
    Value arr = Value::array();
    for (const auto& d : list) {
        Value o = Value::object();
        o.set("cle", d.key);
        o.set("supprime_le", licensing::isoTime(d.at));
        arr.push(std::move(o));
    }
    root.set("pc", std::move(arr));
    std::error_code ec;
    fs::create_directories(P(folder), ec);
    const fs::path path = P(deploy::joinPath(folder, kDeletedDevicesFile));
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << core::json::write(root) << "\n";
        if (!out) {
            if (why) *why = "impossible d'\xC3\xA9" "crire " + U(path);
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        if (why) *why = "impossible d'\xC3\xA9" "crire " + U(path) + " : " + ec.message();
        return false;
    }
    return true;
}

// La derniere activite connue d'un PC (une demande, une licence, une revocation).
Seconds lastActivity(const KnownDevice& d) { return std::max({d.requestedAt, d.activatedAt, d.revokedAt}); }

std::string countText(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

} // namespace

// ------------------------------------------------------------------------------------------ ce qui ne revient pas
std::vector<KnownDevice> Workspace::withoutDeleted(std::vector<KnownDevice> all) const {
    const auto gone = archive::deletedLicences(folder_);
    const auto clients = deletedClients();
    const auto devices = readDeletedDevices(folder_);
    if (gone.empty() && clients.empty() && devices.empty()) return all;
    std::erase_if(all, [&](const KnownDevice& d) {
        // sa licence supprimee (et pas revenue : une emission plus recente dans l'archive)
        if (!d.licenseId.empty() && !licence(d.licenseId) &&
            std::any_of(gone.begin(), gone.end(), [&](const archive::DeletedLicence& g) { return g.id == d.licenseId; }))
            return true;
        // son client supprime, sans activite depuis
        if (!d.customer.empty()) {
            const std::string c = lower(d.customer);
            for (const auto& x : clients)
                if (lower(x.name) == c && lastActivity(d) <= x.deletedAt) return true;
        }
        // lui-meme supprime, sans activite depuis
        for (const auto& x : devices)
            if (x.key == d.deviceKey && lastActivity(d) <= x.at) return true;
        return false;
    });
    return all;
}

void Workspace::dropDeletedFromSnapshot() {
    if (service_.snapshot.licences.empty()) return;
    const auto gone = archive::deletedLicences(folder_);
    if (gone.empty()) return;
    std::erase_if(service_.snapshot.licences, [&](const auto& l) {
        return !licence(l.licenseId) && std::any_of(gone.begin(), gone.end(), [&](const archive::DeletedLicence& g) { return g.id == l.licenseId; });
    });
}

// ------------------------------------------------------------------------------------------ les licences
std::string Workspace::whyNotDeleteLicence(const LicenceEntry& e) const {
    if (!can("licence_revoquer")) return whyNot("licence_revoquer");
    if (inForce(e))
        return "la licence " + e.licence.licenseId + " est en vigueur" +
               (e.licence.expiresAt ? " jusqu'au " + dateFr(e.licence.expiresAt) : std::string(" (sans fin)")) +
               " : un Studio s'arr\xC3\xAAterait chez le client - r\xC3\xA9voque-la d'abord, ou attends sa fin";
    return {};
}

OpResult Workspace::deleteLicences(const std::vector<std::string>& licenseIds) {
    if (const auto deny = denied("licence_revoquer")) return *deny;
    std::vector<std::string> ids;
    for (const auto& id : licenseIds)
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    if (ids.empty()) return OpResult::fail("aucune licence choisie");
    struct One {
        std::string id, customer, edition, path;
        Seconds     issued{0};
        bool        revoked{false}, online{false};
    };
    std::vector<One> todo;
    for (const auto& id : ids) {
        const LicenceEntry* e = licence(id);
        if (!e) return OpResult::fail("la licence " + id + " n'est pas (ou plus) dans l'archive");
        if (const std::string why = whyNotDeleteLicence(*e); !why.empty()) return OpResult::fail(why + (ids.size() > 1 ? " (rien n'est supprim\xC3\xA9)" : ""));
        todo.push_back({id, e->licence.customer, e->licence.edition, e->path, e->licence.issuedAt, e->revoked, isOnlineLicence(e->licence)});
    }
    const Seconds t = now();
    const fs::path trash = P(deploy::joinPath(folder_, archive::kTrashDir));
    std::error_code ec;
    fs::create_directories(trash, ec);
    std::string stamp = licensing::isoTime(t);
    for (char& ch : stamp)
        if (ch == ':') ch = '-';
    std::vector<std::string> done, online, trashed;
    std::string problem;
    for (const One& o : todo) {
        const fs::path to = trash / P(archive::safeName(o.id) + "-" + stamp + ".xpglic");
        std::string why;
        if (!archive::noteDeleted(folder_, {o.id, o.issued, t}, &why)) {
            problem = "la suppression de " + o.id + " n'a pas pu \xC3\xAAtre not\xC3\xA9" "e : " + why;
            break;
        }
        fs::rename(P(o.path), to, ec);
        if (ec) {
            fs::copy_file(P(o.path), to, fs::copy_options::overwrite_existing, ec);
            if (!ec) fs::remove(P(o.path), ec);
        }
        if (ec) {
            (void)archive::forgetDeleted(folder_, o.id, nullptr);
            problem = "le fichier " + o.path + " n'a pas pu aller dans la corbeille : " + ec.message();
            break;
        }
        done.push_back(o.id);
        trashed.push_back(U(to));
        if (o.online) online.push_back(o.id);
        Value detail = Value::object();
        detail.set("client", o.customer);
        detail.set("offre", o.edition);
        detail.set("etat", o.revoked ? "revoquee" : "finie");
        detail.set("en_ligne", o.online);
        (void)journal("licence_supprimer", o.id,
                      "a supprim\xC3\xA9 la licence " + std::string(o.revoked ? "r\xC3\xA9voqu\xC3\xA9" "e " : "finie ") + o.id +
                          (o.customer.empty() ? std::string() : " (" + o.customer + ")"),
                      core::json::write(detail, false));
    }
    loadArchive();
    dropDeletedFromSnapshot();
    if (done.empty()) return OpResult::fail(problem);
    OpResult r = OpResult::done(done.size() == 1 ? "Licence " + done.front() + " supprim\xC3\xA9" "e de l'archive."
                                                 : countText(done.size(), "licence supprim\xC3\xA9" "e", "licences supprim\xC3\xA9" "es") + " de l'archive.");
    if (done.size() == 1) r.details.push_back("Son fichier est dans la corbeille de l'outil : " + trashed.front() + " (rien n'est effac\xC3\xA9).");
    else {
        std::string list;
        for (const auto& id : done) list += (list.empty() ? "" : ", ") + id;
        r.details.push_back(list + " : leurs fichiers sont dans la corbeille de l'outil (rien n'est effac\xC3\xA9).");
    }
    r.details.push_back("Leurs PC quittent Activations ; leur histoire reste (Historique, Journal des admins). Une r\xC3\xA9vocation reste dans la liste "
                        "sign\xC3\xA9" "e : les Studios la refusent toujours.");
    r.details.push_back("La synchro de l'archive ne les ram\xC3\xA8ne pas sur ce poste.");
    if (!problem.empty()) {
        r.ok = false;
        r.message = "Refus\xC3\xA9 en partie : " + problem;
    }
    if (!online.empty() && service_.ready()) {
        ServiceJob job;
        job.kind = ServiceJob::Kind::DeleteLicences;
        job.label = "Service : suppression de " + countText(online.size(), "licence en ligne", "licences en ligne");
        job.licenseIds = online;
        job_ = std::move(job);
        r.details.push_back("En ligne : " + countText(online.size(), "licence effac\xC3\xA9" "e", "licences effac\xC3\xA9" "es") +
                            " aussi du service (leurs PC sont lib\xC3\xA9r\xC3\xA9s).");
    }
    stats::Exclusions ex;   // les statistiques du service ne les comptent plus (comme deleteRevokedLicence)
    ex.deleteLicences = done;
    queueStatsExclusion(ex);
    return r;
}

OpResult Workspace::serviceDeleteLicences(const std::vector<std::string>& licenseIds) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, "licence_revoquer")) return *deny;
    if (licenseIds.empty()) return OpResult::done("Service : rien \xC3\xA0 effacer.");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    Value ids = Value::array();
    for (const auto& id : licenseIds) ids.push(id);
    Value body = Value::object();
    body.set("licences", std::move(ids));
    const online::Reply rep = c->call("POST", "/admin/licences/supprimer", core::json::write(body, false));
    if (rep.error.empty() && rep.status == 404 && rep.code() == "route")
        return OpResult::fail("ton service en ligne ne sait pas encore effacer une licence : d\xC3\xA9ploie la 1.48.1 (Service en ligne \xE2\x80\xBA D\xC3\xA9ployer "
                              "le service) - d'ici l\xC3\xA0, l'Admin ne les montre plus");
    if (rep.status != 200 || !rep.error.empty()) return OpResult::fail(c->explain(rep, "effacer ces licences du service"));
    std::size_t n = 0;
    if (rep.json.isObject())
        if (const Value* s = rep.json.find("supprimees"); s && s->isArray()) n = s->items().size();
    (void)detail::flushJournalQuiet(folder_, caller, now());   // les lignes << licence_supprimer >> partent avec
    OpResult r = OpResult::done("Service : " + countText(n, "licence effac\xC3\xA9" "e", "licences effac\xC3\xA9" "es") + " (leurs PC sont lib\xC3\xA9r\xC3\xA9s).");
    if (n < licenseIds.size()) r.details.push_back(countText(licenseIds.size() - n, "n'y \xC3\xA9tait d\xC3\xA9j\xC3\xA0 plus", "n'y \xC3\xA9taient d\xC3\xA9j\xC3\xA0 plus") + ".");
    return r;
}

// ------------------------------------------------------------------------------------------ les PC
std::string Workspace::whyNotDeleteDevice(const KnownDevice& d) const {
    if (!can("poste_retirer")) return whyNot("poste_retirer");
    if (d.state == "active")
        return "ce PC sert la licence " + d.licenseId + " en vigueur : retire-le de sa licence (Licences \xE2\x80\xBA Retirer un PC) ou r\xC3\xA9voque-le d'abord";
    return {};
}

OpResult Workspace::deleteDevices(const std::vector<std::string>& deviceKeys) {
    if (const auto deny = denied("poste_retirer")) return *deny;
    const auto all = devices();
    std::vector<KnownDevice> todo;
    for (const auto& k : deviceKeys) {
        if (std::any_of(todo.begin(), todo.end(), [&](const KnownDevice& x) { return x.deviceKey == k; })) continue;
        auto it = std::find_if(all.begin(), all.end(), [&](const KnownDevice& x) { return x.deviceKey == k; });
        if (it == all.end()) return OpResult::fail("ce PC n'est pas (ou plus) dans la liste des activations");
        if (const std::string why = whyNotDeleteDevice(*it); !why.empty())
            return OpResult::fail((it->name.empty() ? std::string("ce PC") : it->name) + " : " + why + (deviceKeys.size() > 1 ? " (rien n'est supprim\xC3\xA9)" : ""));
        todo.push_back(*it);
    }
    if (todo.empty()) return OpResult::fail("aucun PC choisi");
    const Seconds t = now();
    auto list = readDeletedDevices(folder_);
    for (const auto& d : todo) {
        std::erase_if(list, [&](const DeletedDevice& x) { return x.key == d.deviceKey; });
        list.push_back({d.deviceKey, t});
    }
    std::string why;
    if (!writeDeletedDevices(folder_, list, &why)) return OpResult::fail(why);
    for (const auto& d : todo) {
        Value detail = Value::object();
        detail.set("pc", d.name);
        detail.set("empreinte", d.fingerprint);
        detail.set("licence", d.licenseId);
        detail.set("etat", d.state);
        (void)journal("pc_supprimer", d.licenseId.empty() ? d.fingerprint : d.licenseId,
                      "a supprim\xC3\xA9 le PC " + (d.name.empty() ? d.fingerprint : d.name) + " de la liste des activations" +
                          (d.customer.empty() ? std::string() : " (" + d.customer + ")"),
                      core::json::write(detail, false));
    }
    OpResult r = OpResult::done(todo.size() == 1 ? "PC " + (todo.front().name.empty() ? todo.front().fingerprint : todo.front().name) + " supprim\xC3\xA9 de la liste."
                                                 : countText(todo.size(), "PC supprim\xC3\xA9", "PC supprim\xC3\xA9s") + " de la liste.");
    r.details.push_back("Le registre des activations n'est pas r\xC3\xA9\xC3\xA9" "crit : l'Historique garde tout. Un PC revient s'il redemande une licence.");
    return r;
}

} // namespace license_admin::gui
