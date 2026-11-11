// =============================================================================
//  licensing/Admins.cpp - 1.44.0 : plusieurs admins (voir Admins.hpp, docs/ADMINS.md)
// =============================================================================
#include "Admins.hpp"

#include "../core/Json.hpp"
#include "../deploy/Crypto.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace licensing::admins {

using core::json::Value;

namespace {

bool isHexLower(std::string_view s, std::size_t n) noexcept {
    if (s.size() != n) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool fail(std::string* why, const std::string& text) {
    if (why) *why = text;
    return false;
}

// Un nombre entier positif (une heure, une taille) ; faux : absent ou pas un entier >= 0.
bool getCount(const Value& o, std::string_view key, std::uint64_t& out, bool required) {
    const Value* v = o.find(key);
    if (!v) return !required;
    if (!v->isNumber()) return false;
    const double d = v->asNumber();
    if (!(d >= 0) || d > 9.0e15 || std::floor(d) != d) return false;
    out = static_cast<std::uint64_t>(d);
    return true;
}

bool getText(const Value& o, std::string_view key, std::string& out, bool required, std::size_t maxLen) {
    const Value* v = o.find(key);
    if (!v) return !required;
    if (!v->isString() || v->asString().size() > maxLen) return false;
    out = v->asString();
    return !(required && out.empty());
}

bool getTextList(const Value& o, std::string_view key, std::vector<std::string>& out, std::size_t maxItems, std::size_t maxLen) {
    const Value* v = o.find(key);
    if (!v) return true;
    if (!v->isArray() || v->items().size() > maxItems) return false;
    for (const auto& it : v->items()) {
        if (!it.isString() || it.asString().size() > maxLen) return false;
        out.push_back(it.asString());
    }
    return true;
}

Value textList(const std::vector<std::string>& v) {
    Value a = Value::array();
    for (const auto& s : v) a.push(s);
    return a;
}

// Un objet JSON donne en texte ; vide ou illisible : {}.
Value objectOf(const std::string& text) {
    Value v;
    if (text.empty() || !core::json::parse(text, v) || !v.isObject()) return Value::object();
    return v;
}

const std::vector<RightInfo> kRights = {
    {"licence_creer", "Cr\xC3\xA9" "er et signer une licence", "licences", "Studios et service"},
    {"licence_prolonger", "Prolonger une licence", "licences", "Studios et service"},
    {"licence_option", "Ajouter une option \xC3\xA0 une licence", "licences", "Studios et service"},
    {"licence_revoquer", "R\xC3\xA9voquer une licence", "licences", "Studios et service"},
    {"poste_retirer", "Retirer un PC d'une licence", "licences", "service"},
    {"offres", "Modifier les offres (prix, fonctions)", "offres", "Studios et service"},
    {"options", "Cr\xC3\xA9" "er, modifier une option", "offres", "Studios et service"},
    {"annonces", "Annonces (notifications g\xC3\xA9n\xC3\xA9rales)", "offres", "Studios et service"},
    {"demandes", "Accepter, refuser une demande", "clients", "Studios et service"},
    {"codes", "Revoir le code d'activation", "clients", "l'Admin"},
    {"client_supprimer", "Supprimer un client", "clients", "l'Admin"},
    {"clients_exporter", "Exporter les clients (CSV)", "clients", "l'Admin"},
    {"courriels", "\xC3\x89" "crire aux clients (courriel)", "clients", "l'Admin"},
};

const std::vector<ActionType> kActions = {
    {"licence_creer", "Signer une licence", "licences", Level::Medium, false, false},
    {"licence_prolonger", "Prolonger une licence", "licences", Level::Info, false, false},
    {"licence_option", "Ajouter une option \xC3\xA0 une licence", "licences", Level::Medium, false, false},
    {"licence_revoquer", "R\xC3\xA9voquer une licence", "licences", Level::Critical, true, false},
    {"poste_retirer", "Retirer un PC d'une licence", "licences", Level::Medium, false, false},
    {"offres", "Modifier une offre (prix, fonctions)", "offres", Level::High, true, false},
    {"options", "Cr\xC3\xA9" "er, modifier une option", "offres", Level::Info, false, false},
    {"annonces", "Publier une annonce", "offres", Level::Medium, false, false},
    {"demande_accepter", "Accepter une demande", "clients", Level::Info, false, false},
    {"demande_refuser", "Refuser une demande", "clients", Level::Info, false, false},
    {"code_revoir", "Revoir un code d'activation", "clients", Level::Medium, false, false},
    {"client_supprimer", "Supprimer un client", "clients", Level::Medium, false, false},
    {"client_restaurer", "Restaurer un client", "clients", Level::Info, false, false},   // 1.44.0
    {"clients_exporter", "Exporter les clients (CSV)", "clients", Level::Medium, false, false},
    {"courriel", "\xC3\x89" "crire \xC3\xA0 un client", "clients", Level::Info, false, false},
    {"admin_ajouter", "Ajouter un admin", "admins", Level::Critical, true, false},
    {"admin_retirer", "Retirer un admin", "admins", Level::Critical, true, false},
    {"admin_droits", "Changer les droits d'un admin", "admins", Level::Critical, true, false},
    {"admin_profil", "Cr\xC3\xA9" "er, modifier un profil de droits", "admins", Level::High, false, false},
    {"version_publier", "Publier une version", "admins", Level::High, false, false},
    {"fichier_envoyer", "Envoyer un fichier \xC3\xA0 un PC", "admins", Level::Medium, false, false},
    {"criticite", "Changer la criticit\xC3\xA9", "admins", Level::Medium, false, false},
    {"connexion", "Connexion de l'Admin", "admins", Level::Info, false, false},
    // 1.46.0 : la boutique (docs/BOUTIQUE.md)
    {"boutique", "R\xC3\xA9gler la boutique (vente en ligne, montants)", "offres", Level::High, true, false},
    {"stripe_brancher", "Brancher Stripe", "admins", Level::Critical, true, false},
    {"commande_livrer", "Livrer une commande de la boutique", "clients", Level::Medium, false, false},
    {"commande_revoir", "Commande de la boutique \xC3\xA0 revoir", "clients", Level::High, true, false},
    {"commande_rembourser", "Rembourser une commande", "clients", Level::High, true, false},
    {"commande_supprimer", "Supprimer une commande ou une demande (sans argent)", "clients", Level::Medium, false, false},   // 1.48.1
    {"licence_supprimer", "Supprimer une licence finie ou r\xC3\xA9voqu\xC3\xA9" "e", "licences", Level::Medium, false, false},   // 1.48.1
    {"pc_supprimer", "Supprimer un PC qui ne sert plus (Activations)", "licences", Level::Info, false, false},   // 1.48.1
    {"sauvegarde", "Sauvegarde compl\xC3\xA8te", "admins", Level::Info, false, false},
    {"restauration", "Restauration d'une sauvegarde", "admins", Level::Critical, true, false},
    // 1.47.0 : la derogation << Gerer le service >> (docs/ADMINS.md, par. 11) - accordee, revoquee par l'admin+ ; utilisee
    // et finie : le service les ecrit (signees par sa cle), l'admin+ est prevenu tout de suite d'un usage.
    {"derogation_accorder", "Accorder une d\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB", "admins", Level::Critical, true, false},
    {"derogation_revoquer", "R\xC3\xA9voquer une d\xC3\xA9rogation", "admins", Level::High, false, false},
    {"derogation_utilisee", "D\xC3\xA9rogation utilis\xC3\xA9" "e (g\xC3\xA9rer le service)", "admins", Level::Critical, true, true},
    {"derogation_expiree", "D\xC3\xA9rogation termin\xC3\xA9" "e", "admins", Level::Medium, false, true},
    {"signature_refusee", "Signature refus\xC3\xA9" "e (certificat r\xC3\xA9voqu\xC3\xA9)", "admins", Level::Critical, true, true},
    {"autre_pc", "Connexion depuis un nouveau PC", "admins", Level::High, true, true},
    {"demande_admin", "Demande pour devenir admin", "admins", Level::High, true, true},
};

} // namespace

// ---- les droits ----
const std::vector<RightInfo>& rights() { return kRights; }

bool isRight(std::string_view id) noexcept {
    return std::any_of(kRights.begin(), kRights.end(), [&](const RightInfo& r) { return id == r.id; });
}

std::string rightLabel(std::string_view id) {
    for (const auto& r : kRights)
        if (id == r.id) return r.label;
    return std::string(id);
}

std::string groupLabel(std::string_view group) {
    if (group == "licences") return "Licences";
    if (group == "offres") return "Offres et options";
    if (group == "clients") return "Clients et demandes";
    if (group == "admins") return "Admins et s\xC3\xA9" "curit\xC3\xA9";
    return std::string(group);
}

std::vector<std::string> rightsForKind(DocKind kind) {
    switch (kind) {
        case DocKind::License: return {"licence_creer", "licence_prolonger", "licence_option"};
        case DocKind::Registry: return {"offres", "options", "annonces"};
        case DocKind::Revocations: return {"licence_revoquer"};
        case DocKind::DeviceStatus:
            return {"demandes", "licence_creer", "licence_prolonger", "licence_option", "licence_revoquer", "poste_retirer"};
        default: return {};
    }
}

std::vector<std::string> allRights() {
    std::vector<std::string> out;
    for (const auto& r : kRights) out.emplace_back(r.id);
    return out;
}

std::vector<std::string> normalizeRights(const std::vector<std::string>& in) {
    std::vector<std::string> out;
    for (const auto& r : kRights)
        if (std::find(in.begin(), in.end(), r.id) != in.end()) out.emplace_back(r.id);
    return out;
}

bool Admin::has(std::string_view right) const noexcept {
    return active() && std::find(rights.begin(), rights.end(), right) != rights.end();
}

const Admin* AdminList::byKeyId(std::string_view keyId) const noexcept {
    for (const auto& a : admins)
        if (a.id == keyId) return &a;
    return nullptr;
}

const Admin* AdminList::byKey(std::string_view keyHex) const noexcept {
    for (const auto& a : admins)
        if (a.key == keyHex) return &a;
    return nullptr;
}

// ---- la liste ----
std::string writeAdminList(const AdminList& list) {
    Value root = Value::object();
    root.set("format", 1);
    root.set("numero", list.number);
    root.set("emise", static_cast<std::int64_t>(list.issued));
    Value arr = Value::array();
    for (const auto& a : list.admins) {
        Value o = Value::object();
        o.set("id", a.id);
        o.set("cle", a.key);
        o.set("nom", a.name);
        o.set("pc", a.pc);
        o.set("poste", a.machine);
        o.set("depuis", static_cast<std::int64_t>(a.since));
        o.set("profil", a.profile);
        o.set("droits", textList(normalizeRights(a.rights)));
        o.set("retire", static_cast<std::int64_t>(a.retired));
        o.set("gardes", textList(a.kept));
        arr.push(std::move(o));
    }
    root.set("admins", std::move(arr));
    return core::json::write(root, false);
}

bool parseAdminList(std::string_view json, AdminList& out, std::string* why) {
    static const std::string kWhat = "la liste des admins";
    Value root;
    std::string err;
    if (!core::json::parse(json, root, &err) || !root.isObject()) return fail(why, kWhat + " est illisible (" + err + ")");
    if (root.integer("format", 0) != 1) return fail(why, kWhat + " n'est pas dans un format connu");
    AdminList l;
    std::uint64_t n = 0, issued = 0;
    if (!getCount(root, "numero", n, true) || n == 0 || n > 2000000000ull) return fail(why, kWhat + " : le num\xC3\xA9ro manque");
    if (!getCount(root, "emise", issued, true)) return fail(why, kWhat + " : la date manque");
    l.number = static_cast<int>(n);
    l.issued = static_cast<Seconds>(issued);
    const Value* arr = root.find("admins");
    if (!arr || !arr->isArray() || arr->items().size() > kMaxAdmins) return fail(why, kWhat + " : la liste manque ou est trop longue");
    std::set<std::string> seen;
    for (std::size_t i = 0; i < arr->items().size(); ++i) {
        const Value& o = arr->items()[i];
        const std::string where = kWhat + " : l'admin n\xC2\xB0 " + std::to_string(i + 1);
        if (!o.isObject()) return fail(why, where + " n'est pas un objet");
        Admin a;
        std::uint64_t since = 0, retired = 0;
        if (!getText(o, "cle", a.key, true, 64) || !isHexLower(a.key, 64)) return fail(why, where + " : sa cl\xC3\xA9 n'est pas valide");
        if (!getText(o, "id", a.id, true, 20)) return fail(why, where + " : son identifiant manque");
        {
            crypto::Bytes raw;
            crypto::PublicKey pk{};
            if (!crypto::unhex(a.key, raw, 32)) return fail(why, where + " : sa cl\xC3\xA9 n'est pas valide");
            std::copy(raw.begin(), raw.end(), pk.begin());
            if (keyIdOf(pk) != a.id) return fail(why, where + " : son identifiant ne correspond pas \xC3\xA0 sa cl\xC3\xA9");
        }
        if (!seen.insert(a.key).second) return fail(why, where + " : sa cl\xC3\xA9 est d\xC3\xA9j\xC3\xA0 dans la liste");
        if (!getText(o, "nom", a.name, true, 60)) return fail(why, where + " : son nom manque");
        if (!getText(o, "pc", a.pc, false, 60)) return fail(why, where + " : son PC est mal \xC3\xA9" "crit");
        if (!getText(o, "poste", a.machine, false, 32) || (!a.machine.empty() && !isHexLower(a.machine, 32)))
            return fail(why, where + " : l'identifiant de son poste est mal \xC3\xA9" "crit");
        if (!getCount(o, "depuis", since, false) || !getCount(o, "retire", retired, false)) return fail(why, where + " : une date est mal \xC3\xA9" "crite");
        a.since = static_cast<Seconds>(since);
        a.retired = static_cast<Seconds>(retired);
        if (!getText(o, "profil", a.profile, false, 60)) return fail(why, where + " : son profil est mal \xC3\xA9" "crit");
        if (!getTextList(o, "droits", a.rights, 32, 40)) return fail(why, where + " : ses droits sont mal \xC3\xA9" "crits");
        for (const auto& r : a.rights)
            if (!isRight(r)) return fail(why, where + " : le droit \xC2\xAB " + r + " \xC2\xBB est inconnu");
        if (!getTextList(o, "gardes", a.kept, kMaxKept, 64)) return fail(why, where + " : ses documents gard\xC3\xA9s sont mal \xC3\xA9" "crits");
        for (const auto& h : a.kept)
            if (!isHexLower(h, 64)) return fail(why, where + " : une empreinte gard\xC3\xA9" "e n'est pas valide");
        l.admins.push_back(std::move(a));
    }
    out = std::move(l);
    return true;
}

std::string fingerprint(std::string_view keyHex) {
    crypto::Bytes raw;
    if (!crypto::unhex(keyHex, raw, 32)) return {};
    std::string h = crypto::sha256Hex(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
    std::string out;
    for (std::size_t i = 0; i < 24; ++i) {
        if (i && i % 4 == 0) out += ' ';
        const char c = h[i];
        out += (c >= 'a' && c <= 'f') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    return out;
}

std::string documentHash(std::string_view document) { return crypto::sha256Hex(document); }

bool verifyAdminBlock(const AdminList& list, const SignedBlock& block, std::string* why, const Admin** who) {
    const Admin* a = list.byKeyId(block.keyId);
    if (!a) return fail(why, "la signature de " + docKindLabel(block.kind) + " vient d'une cl\xC3\xA9 inconnue de ce programme (" + block.keyId + ")");
    crypto::Bytes raw;
    crypto::PublicKey pk{};
    if (!crypto::unhex(a->key, raw, 32)) return fail(why, "la cl\xC3\xA9 de l'admin " + a->name + " est illisible");
    std::copy(raw.begin(), raw.end(), pk.begin());
    if (!verifyBlock(block, pk))
        return fail(why, "la signature de " + docKindLabel(block.kind) + " est fausse : le document a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9");
    if (who) *who = a;
    // Un document garde reste valable quoi qu'il arrive a son admin (retire, droits retires) : l'admin+ y met ce qu'il
    // a signe et qui court encore, quand il retire l'admin ou lui retire un droit.
    if (!a->kept.empty()) {
        const std::string h = documentHash(block.document);
        if (std::find(a->kept.begin(), a->kept.end(), h) != a->kept.end()) return true;
    }
    if (!a->active())
        return fail(why, docKindLabel(block.kind) + " est sign\xC3\xA9" "e par " + a->name + ", un admin retir\xC3\xA9 : elle n'est plus valable");
    const auto need = rightsForKind(block.kind);
    if (need.empty()) return fail(why, docKindLabel(block.kind) + " ne peut \xC3\xAAtre sign\xC3\xA9" "e que par la cl\xC3\xA9 de l'\xC3\xA9" "diteur");
    for (const auto& r : need)
        if (a->has(r)) return true;
    return fail(why, docKindLabel(block.kind) + " est sign\xC3\xA9" "e par " + a->name + ", qui n'en a pas le droit");
}

// ---- les profils ----
std::vector<Profile> defaultProfiles() {
    Profile p;
    p.name = kAllRightsProfile;
    p.rights = allRights();
    p.builtin = true;
    return {p};
}

std::string writeProfiles(const std::vector<Profile>& profiles) {
    Value root = Value::object();
    root.set("format", 1);
    Value arr = Value::array();
    for (const auto& p : profiles) {
        if (p.builtin) continue;
        Value o = Value::object();
        o.set("nom", p.name);
        o.set("droits", textList(normalizeRights(p.rights)));
        o.set("modifie", static_cast<std::int64_t>(p.changed));
        arr.push(std::move(o));
    }
    root.set("profils", std::move(arr));
    return core::json::write(root, true);
}

bool parseProfiles(std::string_view json, std::vector<Profile>& out, std::string* why) {
    Value root;
    std::string err;
    if (!core::json::parse(json, root, &err) || !root.isObject()) return fail(why, "les profils sont illisibles (" + err + ")");
    std::vector<Profile> v = defaultProfiles();
    const Value* arr = root.find("profils");
    if (arr && arr->isArray()) {
        for (const auto& o : arr->items()) {
            if (!o.isObject()) continue;
            Profile p;
            std::uint64_t changed = 0;
            if (!getText(o, "nom", p.name, true, 60) || p.name == kAllRightsProfile) continue;
            if (std::any_of(v.begin(), v.end(), [&](const Profile& q) { return q.name == p.name; })) continue;
            (void)getTextList(o, "droits", p.rights, 32, 40);
            p.rights = normalizeRights(p.rights);
            (void)getCount(o, "modifie", changed, false);
            p.changed = static_cast<Seconds>(changed);
            v.push_back(std::move(p));
            if (v.size() >= 50) break;
        }
    }
    out = std::move(v);
    return true;
}

// ---- le journal ----
const char* levelKey(Level l) noexcept {
    switch (l) {
        case Level::Info: return "info";
        case Level::Medium: return "moyenne";
        case Level::High: return "haute";
        case Level::Critical: return "critique";
    }
    return "info";
}

std::string levelLabel(Level l) {
    switch (l) {
        case Level::Info: return "Info";
        case Level::Medium: return "Moyenne";
        case Level::High: return "Haute";
        case Level::Critical: return "Critique";
    }
    return "Info";
}

bool parseLevel(std::string_view key, Level& out) noexcept {
    for (Level l : {Level::Info, Level::Medium, Level::High, Level::Critical})
        if (key == levelKey(l)) {
            out = l;
            return true;
        }
    return false;
}

const std::vector<ActionType>& actionTypes() { return kActions; }

const ActionType* actionType(std::string_view id) noexcept {
    for (const auto& a : kActions)
        if (id == a.id) return &a;
    return nullptr;
}

std::string writeJournalEntry(const JournalEntry& e) {
    Value o = Value::object();
    o.set("quand", static_cast<std::int64_t>(e.when));
    o.set("action", e.action);
    o.set("objet", e.object);
    o.set("resume", e.summary);
    o.set("detail", objectOf(e.detail));
    o.set("avant", objectOf(e.before));
    o.set("apres", objectOf(e.after));
    o.set("pc", e.pc);
    o.set("poste", e.machine);
    o.set("logiciel", e.software);
    o.set("nonce", e.nonce);
    return core::json::write(o, false);
}

bool parseJournalEntry(std::string_view json, JournalEntry& out, std::string* why) {
    Value o;
    std::string err;
    if (json.size() > 16384 || !core::json::parse(json, o, &err) || !o.isObject()) return fail(why, "l'entr\xC3\xA9" "e du journal est illisible");
    JournalEntry e;
    std::uint64_t when = 0;
    if (!getCount(o, "quand", when, true)) return fail(why, "l'entr\xC3\xA9" "e du journal n'a pas de date");
    e.when = static_cast<Seconds>(when);
    if (!getText(o, "action", e.action, true, 40)) return fail(why, "l'entr\xC3\xA9" "e du journal n'a pas d'action");
    (void)getText(o, "objet", e.object, false, 200);
    (void)getText(o, "resume", e.summary, false, 400);
    (void)getText(o, "pc", e.pc, false, 60);
    (void)getText(o, "poste", e.machine, false, 32);
    (void)getText(o, "logiciel", e.software, false, 40);
    (void)getText(o, "nonce", e.nonce, false, 64);
    if (const Value* d = o.find("detail"); d && d->isObject()) e.detail = core::json::write(*d, false);
    if (const Value* d = o.find("avant"); d && d->isObject()) e.before = core::json::write(*d, false);
    if (const Value* d = o.find("apres"); d && d->isObject()) e.after = core::json::write(*d, false);
    out = std::move(e);
    return true;
}

std::string journalMessage(std::string_view entry) {
    std::string m(kJournalContext);
    m.append(entry.data(), entry.size());
    return m;
}

bool parseJournalLine(std::string_view text, JournalLine& out, std::string* why) {
    Value o;
    std::string err;
    if (text.size() > 64 * 1024 || !core::json::parse(text, o, &err) || !o.isObject()) return fail(why, "une ligne du journal est illisible");
    JournalLine l;
    std::uint64_t num = 0, rec = 0;
    if (!getCount(o, "numero", num, true) || num == 0) return fail(why, "une ligne du journal n'a pas de num\xC3\xA9ro");
    l.number = num;
    if (!getText(o, "precedent", l.previous, false, 64) || (!l.previous.empty() && !isHexLower(l.previous, 64)))
        return fail(why, "la ligne n\xC2\xB0 " + std::to_string(num) + " du journal n'a pas d'empreinte valide");
    (void)getCount(o, "recu", rec, false);
    l.received = static_cast<Seconds>(rec);
    if (!getText(o, "cle", l.key, true, 64) || !isHexLower(l.key, 64)) return fail(why, "la ligne n\xC2\xB0 " + std::to_string(num) + " du journal n'a pas de cl\xC3\xA9");
    if (!getText(o, "entree", l.entry, true, 16384)) return fail(why, "la ligne n\xC2\xB0 " + std::to_string(num) + " du journal n'a pas d'entr\xC3\xA9" "e");
    if (!getText(o, "signature", l.signature, true, 128) || !isHexLower(l.signature, 128))
        return fail(why, "la ligne n\xC2\xB0 " + std::to_string(num) + " du journal n'a pas de signature");
    if (!parseJournalEntry(l.entry, l.parsed, why)) return false;
    l.text = std::string(text);
    l.hash = crypto::sha256Hex(text);
    out = std::move(l);
    return true;
}

bool verifyJournalSignature(const JournalLine& line) noexcept {
    crypto::Bytes k, s;
    if (!crypto::unhex(line.key, k, 32) || !crypto::unhex(line.signature, s, 64)) return false;
    crypto::PublicKey pk{};
    crypto::Signature sig{};
    std::copy(k.begin(), k.end(), pk.begin());
    std::copy(s.begin(), s.end(), sig.begin());
    return crypto::verify(pk, journalMessage(line.entry), sig);
}

// ---- les fichiers ----
bool safeFileName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 120 || name.front() == '.' || name.front() == ' ' || name.back() == ' ') return false;
    if (name.find("..") != std::string_view::npos) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ' ' || c == '-';
    });
}

std::string writeFileManifest(const FileManifest& m) {
    Value o = Value::object();
    o.set("format", 1);
    o.set("id", m.id);
    o.set("nom", m.name);
    o.set("taille", m.size);
    o.set("sha256", m.sha256);
    o.set("sorte", m.kind);
    o.set("version", m.version);
    o.set("acces", m.access);
    o.set("morceau", m.chunk);
    o.set("emis", static_cast<std::int64_t>(m.issued));
    return core::json::write(o, false);
}

bool parseFileManifest(std::string_view json, FileManifest& out, std::string* why) {
    Value o;
    std::string err;
    if (!core::json::parse(json, o, &err) || !o.isObject() || o.integer("format", 0) != 1) return fail(why, "le manifeste du fichier est illisible");
    FileManifest m;
    std::uint64_t issued = 0;
    if (!getText(o, "id", m.id, true, 32) || !isHexLower(m.id, 32)) return fail(why, "le manifeste du fichier n'a pas d'identifiant valide");
    if (!getText(o, "nom", m.name, true, 120) || !safeFileName(m.name)) return fail(why, "le nom du fichier n'est pas valide");
    if (!getCount(o, "taille", m.size, true) || m.size == 0 || m.size > kMaxFileBytes) return fail(why, "la taille du fichier n'est pas valide");
    if (!getText(o, "sha256", m.sha256, true, 64) || !isHexLower(m.sha256, 64)) return fail(why, "l'empreinte du fichier n'est pas valide");
    if (!getText(o, "sorte", m.kind, true, 10) || (m.kind != "admin" && m.kind != "studios" && m.kind != "autre"))
        return fail(why, "la sorte du fichier n'est pas valide");
    (void)getText(o, "version", m.version, false, 20);
    if (!getText(o, "acces", m.access, true, 10) || (m.access != "public" && m.access != "admins" && m.access != "jeton"))
        return fail(why, "l'acc\xC3\xA8s au fichier n'est pas valide");
    if (!getCount(o, "morceau", m.chunk, true) || m.chunk != 1048576) return fail(why, "la taille des morceaux du fichier n'est pas valide");
    (void)getCount(o, "emis", issued, false);
    m.issued = static_cast<Seconds>(issued);
    out = std::move(m);
    return true;
}

// ---- les versions ----
bool parseSemver(std::string_view text, int& major, int& minor, int& patch) noexcept {
    int parts[3] = {0, 0, 0};
    int idx = 0, digits = 0;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            if (++digits > 5) return false;
            parts[idx] = parts[idx] * 10 + (c - '0');
        } else if (c == '.' && digits > 0 && idx < 2) {
            ++idx;
            digits = 0;
        } else {
            return false;
        }
    }
    if (idx != 2 || digits == 0) return false;
    major = parts[0];
    minor = parts[1];
    patch = parts[2];
    return true;
}

int compareSemver(std::string_view a, std::string_view b) noexcept {
    int a1 = 0, a2 = 0, a3 = 0, b1 = 0, b2 = 0, b3 = 0;
    const bool okA = parseSemver(a, a1, a2, a3), okB = parseSemver(b, b1, b2, b3);
    if (!okA || !okB) return okA == okB ? 0 : (okA ? 1 : -1);
    if (a1 != b1) return a1 < b1 ? -1 : 1;
    if (a2 != b2) return a2 < b2 ? -1 : 1;
    if (a3 != b3) return a3 < b3 ? -1 : 1;
    return 0;
}

const VersionFile* VersionManifest::file(std::string_view kind) const noexcept {
    for (const auto& f : files)
        if (f.kind == kind) return &f;
    return nullptr;
}

std::string writeVersion(const VersionManifest& v) {
    Value o = Value::object();
    o.set("format", 1);
    o.set("version", v.version);
    o.set("publiee", static_cast<std::int64_t>(v.published));
    o.set("importante", v.important);
    o.set("notes", textList(v.notes));
    Value arr = Value::array();
    for (const auto& f : v.files) {
        Value x = Value::object();
        x.set("id", f.id);
        x.set("nom", f.name);
        x.set("taille", f.size);
        x.set("sha256", f.sha256);
        x.set("sorte", f.kind);
        x.set("pour", f.audience);
        arr.push(std::move(x));
    }
    o.set("fichiers", std::move(arr));
    return core::json::write(o, false);
}

bool parseVersion(std::string_view json, VersionManifest& out, std::string* why) {
    Value o;
    std::string err;
    if (!core::json::parse(json, o, &err) || !o.isObject() || o.integer("format", 0) != 1) return fail(why, "la version publi\xC3\xA9" "e est illisible");
    VersionManifest v;
    int a = 0, b = 0, c = 0;
    std::uint64_t pub = 0;
    if (!getText(o, "version", v.version, true, 20) || !parseSemver(v.version, a, b, c)) return fail(why, "le num\xC3\xA9ro de version n'est pas valide");
    (void)getCount(o, "publiee", pub, false);
    v.published = static_cast<Seconds>(pub);
    v.important = o.flag("importante", false);
    if (!getTextList(o, "notes", v.notes, 40, 400)) return fail(why, "les notes de la version sont mal \xC3\xA9" "crites");
    const Value* arr = o.find("fichiers");
    if (!arr || !arr->isArray() || arr->items().empty() || arr->items().size() > 4) return fail(why, "la version n'a pas de fichier");
    for (const auto& x : arr->items()) {
        if (!x.isObject()) return fail(why, "un fichier de la version est mal \xC3\xA9" "crit");
        VersionFile f;
        if (!getText(x, "id", f.id, true, 32) || !isHexLower(f.id, 32) || !getText(x, "nom", f.name, true, 120) || !safeFileName(f.name)
            || !getCount(x, "taille", f.size, true) || f.size == 0 || f.size > kMaxFileBytes || !getText(x, "sha256", f.sha256, true, 64)
            || !isHexLower(f.sha256, 64) || !getText(x, "sorte", f.kind, true, 10) || (f.kind != "studios" && f.kind != "admin")
            || !getText(x, "pour", f.audience, true, 10) || (f.audience != "tous" && f.audience != "admins"))
            return fail(why, "un fichier de la version est mal \xC3\xA9" "crit");
        v.files.push_back(std::move(f));
    }
    out = std::move(v);
    return true;
}

// ---- les messages ----
std::string writeMessage(const Message& m) {
    Value o = Value::object();
    o.set("format", 1);
    o.set("poste", m.device);
    o.set("titre", m.title);
    o.set("de", m.from);
    o.set("emis", static_cast<std::int64_t>(m.issued));
    o.set("scelle", m.sealed);
    return core::json::write(o, false);
}

bool parseMessage(std::string_view json, Message& out, std::string* why) {
    Value o;
    std::string err;
    if (!core::json::parse(json, o, &err) || !o.isObject() || o.integer("format", 0) != 1) return fail(why, "le message est illisible");
    Message m;
    std::uint64_t issued = 0;
    if (!getText(o, "poste", m.device, true, 32) || !isHexLower(m.device, 32)) return fail(why, "le message n'est pas adress\xC3\xA9 \xC3\xA0 un PC");
    if (!getText(o, "titre", m.title, true, 120)) return fail(why, "le message n'a pas de titre");
    (void)getText(o, "de", m.from, false, 60);
    (void)getCount(o, "emis", issued, false);
    m.issued = static_cast<Seconds>(issued);
    if (!getText(o, "scelle", m.sealed, false, 60000)) return fail(why, "le contenu du message est mal \xC3\xA9" "crit");
    out = std::move(m);
    return true;
}

std::string writeMessageContent(const MessageContent& c) {
    Value o = Value::object();
    o.set("texte", c.text);
    Value arr = Value::array();
    for (const auto& f : c.files) {
        Value x = Value::object();
        x.set("id", f.id);
        x.set("nom", f.name);
        x.set("taille", f.size);
        x.set("sha256", f.sha256);
        x.set("version", f.version);
        x.set("jeton", f.token);
        arr.push(std::move(x));
    }
    o.set("fichiers", std::move(arr));
    o.set("action", c.action);
    return core::json::write(o, false);
}

bool parseMessageContent(std::string_view json, MessageContent& out, std::string* why) {
    Value o;
    std::string err;
    if (!core::json::parse(json, o, &err) || !o.isObject()) return fail(why, "le contenu du message est illisible");
    MessageContent c;
    (void)getText(o, "texte", c.text, false, 4000);
    (void)getText(o, "action", c.action, false, 20);
    if (const Value* arr = o.find("fichiers"); arr && arr->isArray()) {
        if (arr->items().size() > 4) return fail(why, "le message a trop de fichiers");
        for (const auto& x : arr->items()) {
            MessageFile f;
            if (!x.isObject() || !getText(x, "id", f.id, true, 32) || !isHexLower(f.id, 32) || !getText(x, "nom", f.name, true, 120)
                || !safeFileName(f.name) || !getCount(x, "taille", f.size, true) || f.size == 0 || f.size > kMaxFileBytes
                || !getText(x, "sha256", f.sha256, true, 64) || !isHexLower(f.sha256, 64))
                return fail(why, "un fichier joint au message est mal \xC3\xA9" "crit");
            (void)getText(x, "version", f.version, false, 20);
            (void)getText(x, "jeton", f.token, false, 64);
            c.files.push_back(std::move(f));
        }
    }
    out = std::move(c);
    return true;
}

// ---- le jeton d'admin ----
std::string writeAdminToken(const AdminToken& t) {
    Value o = Value::object();
    o.set("cle", t.key);
    o.set("poste", t.machine);
    o.set("expire", static_cast<std::int64_t>(t.expires));
    o.set("version", t.version);
    o.set("nom", t.name);
    return core::json::write(o, false);
}

bool parseAdminToken(std::string_view json, AdminToken& out, std::string* why) {
    Value o;
    std::string err;
    if (!core::json::parse(json, o, &err) || !o.isObject()) return fail(why, "le jeton d'admin est illisible");
    AdminToken t;
    std::uint64_t exp = 0;
    if (!getText(o, "cle", t.key, true, 64) || !isHexLower(t.key, 64)) return fail(why, "le jeton d'admin n'a pas de cl\xC3\xA9");
    (void)getText(o, "poste", t.machine, false, 32);
    if (!getCount(o, "expire", exp, true)) return fail(why, "le jeton d'admin n'a pas de fin");
    t.expires = static_cast<Seconds>(exp);
    (void)getText(o, "version", t.version, false, 20);
    (void)getText(o, "nom", t.name, false, 60);
    out = std::move(t);
    return true;
}

} // namespace licensing::admins
