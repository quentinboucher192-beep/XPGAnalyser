// =============================================================================
//  license_admin/gui/WorkspaceReset.cpp - 1.49.0 : REPARTIR DE ZERO (l'admin+ ;
//  docs/ADMINS.md, par. 13)
// -----------------------------------------------------------------------------
//  Jamais d'elle-meme (ni a l'installation, ni au deploiement) : un geste de
//  l'admin+ (Service en ligne > << Repartir de zero... >>, ou le bandeau du premier
//  lancement de l'Admin+ 1.49), le mot << REPARTIR >> tape, et TOUJOURS une
//  sauvegarde complete exacte d'abord (backupNow, docs/ADMINS.md par. 9 :
//  << Restaurer une sauvegarde... >> remet tout).
//  L'ordre : la sauvegarde (son SHA-256 part au service, qui exige qu'elle ait ete
//  exportee dans l'heure par cette cle) ; une liste des admins VIDE et une liste de
//  revocation VIDE signees par la cle de l'editeur, numero + 1 (l'anti-retour
//  tient) ; le registre re-signe par l'editeur s'il est signe par un admin (il
//  n'existera plus) ; POST /admin/reinitialiser ; puis, le service d'accord, le
//  dossier de CE poste (kErasedFiles, kErasedDirs) et les listes neuves rangees ;
//  l'archive en ligne (videe par le service) renvoyee depuis ce poste.
//  XPG Admin (l'edition classique) ne l'a jamais.
// =============================================================================
#include "Workspace.hpp"

#include "../Changes.hpp"
#include "../../core/Json.hpp"
#include "../../deploy/Crypto.hpp"
#include "../../deploy/Paths.hpp"
#include "../../licensing/Documents.hpp"
#include "../../licensing/SignedFile.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace license_admin::gui {

namespace {

namespace fs = std::filesystem;
namespace crypto = deploy::crypto;
namespace adm = licensing::admins;
using core::json::Value;
using licensing::DocKind;
using licensing::SignedBlock;

constexpr const char* kOfferFile = "repartir-de-zero.json";
constexpr const char* kOfferFormat = "xpg-repartir-de-zero/1";
constexpr const char* kRegistryRel = "registre/registre.xpgreg";
constexpr const char* kRevocationsRel = "revocations/revocations.xpgrev";
constexpr const char* kAdminsFile = "admins.xpgadm";
constexpr const char* kAdminsSeenFile = "admins-vus.jsonl";

// Ce qui est efface du dossier de l'admin+ (les donnees des clients et de l'usage). Le reste (la cle de l'editeur, la cle de
// synchro, le registre, service.json, la connexion, la sauvegarde, le courriel, la criticite, les profils, les fichiers
// envoyes des versions, les filtres des listes...) reste tel quel.
const char* const kErasedFiles[] = {
    "activations.jsonl",          // le registre des activations : les PC, les demandes notees
    "licences-supprimees.json",   // la corbeille des licences
    "clients-supprimes.json",     // la corbeille des clients
    "pc-supprimes.json",
    "service-releve.json",        // le releve des licences en ligne
    "postes-publies.json",        // les etats de poste publies
    "boutique-livraisons.json",   // les livraisons de commandes commencees
    "stats-a-envoyer.json",       // les exclusions des statistiques a envoyer
    "statistiques-tableau.json",  // le dernier releve du tableau de bord
    "admins-vus.jsonl",           // les listes des admins vues (refaite : la liste neuve)
    "demande-admin.json",         // la demande d'admin d'avant la 1.51
    "inscription-admin.json",     // 1.51.0 : l'inscription par code d'invitation
    "invitation.txt",
    "invitations-admins.json",
    "journal-a-envoyer.jsonl",    // le journal des admins : il repart de la ligne n 1
    "journal-admins.jsonl",
    "journal-admins-etat.json",
    "journal-notes.json",         // les criticites changees a la main, ligne par ligne
};
const char* const kErasedDirs[] = {
    "archive/licences",     // les licences et leurs fichiers
    "corbeille-licences",   // la corbeille de l'outil
    "codes-activation",     // les codes d'activation gardes
    "demandes-en-ligne",    // les demandes relevees et les licences signees pour elles
};

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string at(const std::string& folder, const char* name) { return deploy::joinPath(folder, name); }

bool readText(const std::string& path, std::string& out, std::string* why) {
    crypto::Bytes raw;
    if (!crypto::readFile(path, raw, why, 2048u * 1024u * 1024u)) return false;
    out.assign(raw.begin(), raw.end());
    return true;
}
bool writeText(const std::string& path, const std::string& text, std::string* why) {
    std::error_code ec;
    fs::create_directories(P(path).parent_path(), ec);
    return crypto::writePrivateFile(path, crypto::Bytes(text.begin(), text.end()), why);
}

Value readOffer(const std::string& folder) {
    std::string text;
    Value v;
    std::error_code ec;
    if (!fs::is_regular_file(P(at(folder, kOfferFile)), ec) || !readText(at(folder, kOfferFile), text, nullptr) || !core::json::parse(text, v, nullptr) ||
        !v.isObject())
        return Value::object();
    return v;
}
void writeOffer(const std::string& folder, Value v) {
    v.set("format", kOfferFormat);
    (void)writeText(at(folder, kOfferFile), core::json::write(v, true) + "\n", nullptr);
}

// Le bloc d'un document de l'archive de ce poste (un seul bloc : le registre, la liste de revocation).
bool archivedBlock(const std::string& folder, const char* relative, DocKind kind, SignedBlock& out) {
    const std::string text = archive::archivedText(folder, relative);
    std::vector<SignedBlock> blocks;
    if (text.empty() || !licensing::readBlocks(text, blocks, nullptr) || blocks.size() != 1 || blocks[0].kind != kind) return false;
    out = std::move(blocks[0]);
    return true;
}

std::string plural(std::int64_t n, const std::string& one, const std::string& many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

} // namespace

const std::vector<std::string>& Workspace::startOverErased() {
    static const std::vector<std::string> v = {
        "Les licences et leurs fichiers (l'archive de ce poste, la corbeille)",
        "Les activations et les PC, les PC en D\xC3\xA9" "couverte (essai)",
        "Les clients (et leur corbeille)",
        "Les demandes de licence et les commandes de la boutique (leurs lignes)",
        "Les r\xC3\xA9vocations : une nouvelle liste vide, sign\xC3\xA9" "e (num\xC3\xA9ro + 1)",
        "Les admins : une nouvelle liste sign\xC3\xA9" "e sans aucun admin (num\xC3\xA9ro + 1) ; les inscrits, les invitations, les d\xC3\xA9rogations, "
        "le trousseau",
        "Les messages envoy\xC3\xA9s aux PC et leurs fichiers joints",
        "Les statistiques (relev\xC3\xA9s, compteurs, globe, exclusions)",
        "Le journal des admins : il repart \xC3\xA0 la ligne n\xC2\xB0 1, \xC2\xAB r\xC3\xA9initialisation par l'admin+ \xC2\xBB",
    };
    return v;
}

const std::vector<std::string>& Workspace::startOverKept() {
    static const std::vector<std::string> v = {
        "La cl\xC3\xA9 de l'\xC3\xA9" "diteur et ton identit\xC3\xA9 d'admin+ (jamais touch\xC3\xA9" "es)",
        "Le service : son certificat, son adresse, ses adresses de secours, la connexion",
        "Le catalogue : le registre (offres, options, fonctions, r\xC3\xA9" "ductions)",
        "La boutique : son r\xC3\xA9glage, les produits et les prix chez Stripe (sans les commandes)",
        "Les versions publi\xC3\xA9" "es et leurs fichiers",
        "Tes r\xC3\xA9glages : la sauvegarde, le courriel, la criticit\xC3\xA9, les profils de droits, la cl\xC3\xA9 de synchro",
    };
    return v;
}

std::string Workspace::whyNotStartOver() const {
    if (license_admin::classique()) return std::string(kClassicRefusal) + ".";
    if (role_ != Role::Plus) return "R\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+ (le poste qui a la cl\xC3\xA9 de l'\xC3\xA9" "diteur).";
    if (!service_.configured) return "Le service n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne \xE2\x80\xBA Adresse).";
    if (!backupSettings().configured)
        return "R\xC3\xA8gle d'abord la sauvegarde compl\xC3\xA8te (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9) : elle est faite juste avant, et \xC2\xAB Restaurer une "
               "sauvegarde\xE2\x80\xA6 \xC2\xBB remet tout.";
    return {};
}

bool Workspace::startOverOfferDue() const {
    if (license_admin::classique() || role_ != Role::Plus) return false;
    // le premier lancement d'un Admin+ 1.49 ou plus recent (1.50.0 : on peut sauter la 1.49) ; une seule fois (la note)
    {
        int ma = 0, mi = 0;
        if (std::sscanf(kAppVersion, "%d.%d", &ma, &mi) != 2 || ma < 1 || (ma == 1 && mi < 49)) return false;
    }
    if (readOffer(folder_).has("propose")) return false;                 // deja propose (ou deja fait)
    return !licences_.empty() || !records_.empty() || (adminList_ && !adminList_->admins.empty());
}

void Workspace::noteStartOverOffered() const {
    Value v = readOffer(folder_);
    if (v.has("propose")) return;
    v.set("propose", static_cast<std::int64_t>(now()));
    v.set("version", std::string(kAppVersion));
    writeOffer(folder_, std::move(v));
}

OpResult Workspace::startOver(const std::string& confirmation, StartOverReport* report) const {
    StartOverReport rep;
    if (license_admin::classique()) return OpResult::fail(std::string(kClassicRefusal));
    if (confirmation != kStartOverWord)
        return OpResult::fail("tape \xC2\xAB " + std::string(kStartOverWord) + " \xC2\xBB pour confirmer : rien n'est effac\xC3\xA9 sans ce mot");
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    if (!online::configured(folder_)) return OpResult::fail("le service n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne \xE2\x80\xBA Adresse) : rien n'est effac\xC3\xA9");
    if (!backupSettings().configured)
        return OpResult::fail("r\xC3\xA8gle d'abord la sauvegarde compl\xC3\xA8te (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9 : son dossier et sa phrase) : elle se fait "
                              "juste avant de repartir de z\xC3\xA9ro. Rien n'est effac\xC3\xA9.");
    const licensing::SigningKey& editor = *caller.key;

    // 1. la sauvegarde complete exacte (le service entier, le dossier, la cle) - sans elle, rien
    std::string backupPath;
    const OpResult b = backupNow(&backupPath);
    if (!b.ok || backupPath.empty()) return OpResult::fail("la sauvegarde compl\xC3\xA8te n'a pas pu se faire (" + b.message + ") : rien n'est effac\xC3\xA9");
    std::string backupText;
    if (!readText(backupPath, backupText, &why)) return OpResult::fail("la sauvegarde " + backupPath + " ne se relit pas (" + why + ") : rien n'est effac\xC3\xA9");
    rep.backupPath = backupPath;
    rep.backupSha256 = crypto::sha256Hex(backupText);

    const Seconds t = now();
    const online::Options o = detail::optionsFor(caller, t);
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, editor, o, c, &why)) return OpResult::fail(why + " (la sauvegarde est faite : " + backupPath + ")");
    const online::Reply st = c->call("GET", "/v1/etat", {}, false);
    if (st.status != 200 || !st.error.empty() || !st.json.isObject())
        return OpResult::fail(c->explain(st, "lire l'\xC3\xA9tat du service") + " (rien n'est effac\xC3\xA9 ; la sauvegarde est faite : " + backupPath + ")");

    // 2. la liste des admins neuve : vide, numero + 1 (le plus grand du poste et du service)
    adm::AdminList admins;
    admins.number = std::max<int>(caller.admins ? caller.admins->number : 0, static_cast<int>(st.json.integer("admins"))) + 1;
    admins.issued = t;
    const std::string adminsText =
        licensing::writeBlock(licensing::signBlock(DocKind::Admins, adm::writeAdminList(admins), editor.publicKey(), [&editor](std::string_view m) { return editor.sign(m); }));
    // 3. la liste de revocation neuve : vide, numero + 1
    std::uint64_t revNumber = static_cast<std::uint64_t>(std::max<std::int64_t>(0, st.json.integer("revocations")));
    {
        SignedBlock blk;
        licensing::RevocationList rev;
        if (archivedBlock(folder_, kRevocationsRel, DocKind::Revocations, blk) && licensing::parseRevocations(blk.document, rev, nullptr))
            revNumber = std::max<std::uint64_t>(revNumber, rev.number);
    }
    ++revNumber;
    std::string revText;
    {
        Value in = Value::object();
        in.set("number", static_cast<std::uint64_t>(revNumber));
        in.set("revoked", Value::array());
        if (!signRevocations(editor, core::json::write(in), t, revText, &why)) return OpResult::fail(why);
    }
    // 4. le registre : signe par un admin (sur le service ou ici) -> re-signe par l'editeur, numero + 1
    std::string regText;
    {
        SignedBlock local, remote;
        licensing::Registry regLocal, regRemote;
        const bool hasLocal = archivedBlock(folder_, kRegistryRel, DocKind::Registry, local) && licensing::parseRegistry(local.document, regLocal, nullptr);
        bool hasRemote = false;
        const online::Reply r = c->call("GET", "/v1/registre", {}, false);
        if (r.status == 200 && r.error.empty()) {
            std::vector<SignedBlock> blocks;
            hasRemote = licensing::readBlocks(r.body, blocks, nullptr) && blocks.size() == 1 && blocks[0].kind == DocKind::Registry &&
                        licensing::parseRegistry(blocks[0].document, regRemote, nullptr);
            if (hasRemote) remote = std::move(blocks[0]);
        }
        const bool remoteByAdmin = hasRemote && remote.keyId != editor.keyId();
        const bool localByAdmin = hasLocal && local.keyId != editor.keyId() && (!hasRemote || regLocal.number >= regRemote.number);
        if (remoteByAdmin || localByAdmin) {
            licensing::Registry reg = hasRemote && (!hasLocal || regRemote.number >= regLocal.number) ? regRemote : regLocal;
            reg.number = std::max({regLocal.number, regRemote.number, static_cast<std::uint64_t>(std::max<std::int64_t>(0, st.json.integer("registre")))}) + 1;
            reg.issuedAt = t;
            if (!license_admin::signRegistry(editor, licensing::writeRegistry(reg), t, regText, &why)) return OpResult::fail(why);
            rep.registryNumber = static_cast<int>(reg.number);
        }
    }

    // 5. le service
    Value body = Value::object();
    body.set("confirmation", std::string(kStartOverWord));
    body.set("sauvegarde", rep.backupSha256);
    body.set("admins", adminsText);
    body.set("revocations", revText);
    if (!regText.empty()) body.set("registre", regText);
    const online::Reply r = c->call("POST", "/admin/reinitialiser", core::json::write(body, false));
    if (r.status != 200 || !r.error.empty() || !r.json.isObject())
        return OpResult::fail(c->explain(r, "repartir de z\xC3\xA9ro") + " (rien n'est effac\xC3\xA9 sur ce poste ; la sauvegarde est faite : " + backupPath + ")");
    rep.adminsNumber = static_cast<int>(r.json.integer("admins"));
    rep.revocationsNumber = static_cast<int>(r.json.integer("revocations"));
    if (!rep.registryNumber) rep.registryNumber = static_cast<int>(r.json.integer("registre"));
    std::int64_t rows = 0;
    if (const Value* e = r.json.find("effacees"); e && e->isObject())
        for (const auto& [k, v] : e->members()) {
            rep.erased.emplace_back(k, v.asInt());
            rows += v.asInt();
        }

    // 6. ce poste : les donnees effacees, les listes neuves rangees
    OpResult res = OpResult::done("");
    std::error_code ec;
    for (const char* f : kErasedFiles)
        if (fs::remove(P(at(folder_, f)), ec)) ++rep.localFiles;
    for (const char* d : kErasedDirs) {
        const fs::path p = P(deploy::joinPath(folder_, d));
        if (!fs::exists(p, ec)) continue;
        const auto n = fs::remove_all(p, ec);
        if (ec) res.details.push_back("ATTENTION : " + std::string(d) + " n'a pas pu \xC3\xAAtre effac\xC3\xA9 (" + ec.message() + ").");
        else if (n > 0) rep.localFiles += static_cast<int>(n) - 1;   // (le dossier lui-meme n'est pas un fichier)
    }
    const std::vector<licensing::crypto::PublicKey> trusted = {editor.publicKey()};
    if (!archive::archiveSigned(folder_, revText, trusted, nullptr, nullptr, &why)) res.details.push_back("ATTENTION : la liste de r\xC3\xA9vocation neuve n'est pas rang\xC3\xA9" "e (" + why + ").");
    if (!regText.empty() && !archive::archiveSigned(folder_, regText, trusted, nullptr, nullptr, &why))
        res.details.push_back("ATTENTION : le registre re-sign\xC3\xA9 n'est pas rang\xC3\xA9 (" + why + ").");
    if (!writeText(at(folder_, kAdminsFile), adminsText, &why)) res.details.push_back("ATTENTION : la liste des admins neuve n'est pas gard\xC3\xA9" "e (" + why + ").");
    {
        Value seen = Value::object();
        seen.set("numero", admins.number);
        seen.set("bloc", adminsText);
        (void)writeText(at(folder_, kAdminsSeenFile), core::json::write(seen, false) + "\n", nullptr);
    }
    {
        Value v = readOffer(folder_);
        if (!v.has("propose")) v.set("propose", static_cast<std::int64_t>(t));
        v.set("fait_le", static_cast<std::int64_t>(t));
        v.set("sauvegarde", backupPath);
        v.set("sauvegarde_sha256", rep.backupSha256);
        writeOffer(folder_, std::move(v));
    }
    // 7. l'archive en ligne (videe par le service) : renvoyee depuis ce poste (le registre, la liste neuve, les reglages)
    if (archive::hasSyncKey(folder_)) {
        archive::SyncOptions so;
        so.now = t;
        so.tool = std::string(kAppName) + " " + kAppVersion;
        so.editorKeys = caller.editorKeys;
        so.admins = admins;
        so.software = o.software;
        const archive::SyncReport sync = archive::synchronize(folder_, editor, so);
        rep.archiveSent = sync.ok;
        if (!sync.ok) res.details.push_back("ATTENTION : l'archive en ligne n'est pas renvoy\xC3\xA9" "e (" + sync.why + ") : Service en ligne \xE2\x80\xBA Synchroniser l'archive.");
    }
    changes::bump();

    res.message = "Reparti de z\xC3\xA9ro : le service et ce poste n'ont plus ni licences, ni clients, ni PC, ni admins.";
    res.details.insert(res.details.begin(),
                       {"La sauvegarde compl\xC3\xA8te d'avant : " + backupPath + " \xE2\x80\x94 \xC2\xAB Restaurer une sauvegarde\xE2\x80\xA6 \xC2\xBB (Param\xC3\xA8tres de "
                        "s\xC3\xA9" "curit\xC3\xA9) remet tout.",
                        "Le service : " + plural(rows, "ligne effac\xC3\xA9" "e", "lignes effac\xC3\xA9" "es") + " ; la liste des admins n\xC2\xB0 " +
                            std::to_string(rep.adminsNumber) + " (vide), la liste de r\xC3\xA9vocation n\xC2\xB0 " + std::to_string(rep.revocationsNumber) +
                            " (vide)" + (regText.empty() ? std::string() : ", le registre re-sign\xC3\xA9 n\xC2\xB0 " + std::to_string(rep.registryNumber)) + ".",
                        "Ce poste : " + plural(rep.localFiles, "fichier effac\xC3\xA9", "fichiers effac\xC3\xA9s") +
                            " ; gard\xC3\xA9s : ta cl\xC3\xA9, le catalogue, le service, la boutique, les versions, tes r\xC3\xA9glages.",
                        "Les Studios d\xC3\xA9j\xC3\xA0 activ\xC3\xA9s perdent leur licence \xC3\xA0 leur prochain contr\xC3\xB4le (le service ne la conna\xC3\xAEt plus)."});
    if (report) *report = std::move(rep);
    return res;
}

} // namespace license_admin::gui
