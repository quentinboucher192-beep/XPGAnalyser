// =============================================================================
//  license_admin/gui/WorkspaceAdmins.cpp - 1.44.0 : PLUSIEURS ADMINS, vus par le
//  Workspace (docs/ADMINS.md ; voir Workspace.hpp)
// -----------------------------------------------------------------------------
//  Pourquoi un fichier a part : tout ce qui suit est neuf en 1.44.0 et se lit
//  d'un bloc - le role du poste (l'admin+, un admin, en cours d'ajout, retire),
//  la cle qui signe, les droits, la liste des admins (gardee, verifiee, relevee),
//  devenir admin par le code d'invitation (1.51.0 : plus de demande d'admin - la
//  cle s'inscrit avec son code, l'Admin+ la signe d'office), le trousseau (la cle
//  de synchro scellee pour chaque admin), la gestion des admins par l'admin+
//  (inviter, retirer, les droits, les profils), le journal des admins (chaque
//  ligne signee, envoyee, relevee et verifiee par l'admin+ : la chaine), sa
//  criticite, le jeton d'admin.
//
//  LES FICHIERS (dans le dossier de la cle) :
//    cle-admin.cle            la cle de l'admin (Admin.hpp : adminKeySpec)
//    admins.xpgadm            la liste des admins en vigueur (le bloc signe par l'editeur)
//    admins-vus.jsonl         chaque liste vue (une ligne par numero) : le journal reconnait
//                             la cle d'un admin, meme retire ou parti de la liste
//    cles-editeur.json        les cles de l'editeur retenues du service (un admin : a son
//                             acceptation, une fois), en plus de celles compilees
//    inscription-admin.json   1.51.0 : l'inscription par code (nom, courriel, invitation, date) ;
//                             demande-admin.json : la demande d'avant la 1.51 (lue pour le courriel)
//    journal-a-envoyer.jsonl  les lignes du journal signees, pas encore envoyees
//    journal-admins.jsonl     l'admin+ : le journal releve (les lignes telles quelles)
//    journal-admins-etat.json l'admin+ : le dernier numero, son empreinte, les ruptures vues
//    profils-admins.json, criticite.json, journal-notes.json : les reglages de l'admin+,
//                             partages par l'archive (ArchiveSync : sharedSettingsFiles)
//    jeton-admin.xpgjet       le jeton d'admin (genre jetonadmin) lu par le Studio du compte
//
//  Les fonctions LENTES (le reseau) sont const : elles relisent la cle et les
//  fichiers (detail::loadCaller), jamais l'etat du Workspace ; l'ecran appelle
//  reload() ensuite.
// =============================================================================
#include "Workspace.hpp"

#include "../Mail.hpp"
#include "../../core/Json.hpp"
#include "../../deploy/Crypto.hpp"
#include "../../deploy/DevKey.hpp"
#include "../../deploy/Paths.hpp"
#include "../../licensing/Gateway.hpp"
#include "../../licensing/Service.hpp"
#include "../../licensing/SignedFile.hpp"
#include "../../licensing/Store.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <system_error>

namespace license_admin::gui {

namespace {

namespace fs = std::filesystem;
namespace crypto = deploy::crypto;
namespace adm = licensing::admins;
namespace gw = licensing::gateway;
using core::json::Value;
using licensing::DocKind;
using licensing::SignedBlock;

constexpr const char* kAdminsFile = "admins.xpgadm";
constexpr const char* kAdminsSeenFile = "admins-vus.jsonl";
constexpr const char* kEditorKeysFile = "cles-editeur.json";
constexpr const char* kAdminRequestFile = "demande-admin.json";   // avant la 1.51 (lu seulement : le courriel)
constexpr const char* kJoinFile = "inscription-admin.json";        // 1.51.0 : l'inscription par code d'invitation
constexpr const char* kInvitationFile = "invitation.txt";   // 1.49.0 : le code d'invitation pose par l'installateur XPG Admin
constexpr const char* kOutboxFile = "journal-a-envoyer.jsonl";
constexpr const char* kJournalFile = "journal-admins.jsonl";
constexpr const char* kJournalStateFile = "journal-admins-etat.json";
constexpr const char* kAdminTokenFile = "jeton-admin.xpgjet";
constexpr const char* kRegistryRel = "registre/registre.xpgreg";
constexpr const char* kRevocationsRel = "revocations/revocations.xpgrev";
constexpr Seconds kDay = 86400;
constexpr Seconds kTokenDays = 30;
constexpr std::size_t kJournalPage = 500;   // GET /admin/journal : 500 lignes au plus par lecture

// ---- les fichiers ----
fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string at(const std::string& folder, const char* name) { return deploy::joinPath(folder, name); }
bool isFile(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(P(path), ec);
}
bool readText(const std::string& path, std::string& out, std::string* why) {
    crypto::Bytes raw;
    if (!crypto::readFile(path, raw, why, 64u * 1024u * 1024u)) return false;
    out.assign(raw.begin(), raw.end());
    return true;
}
bool writeText(const std::string& path, const std::string& text, std::string* why) {
    std::error_code ec;
    fs::create_directories(P(path).parent_path(), ec);
    return crypto::writePrivateFile(path, crypto::Bytes(text.begin(), text.end()), why);
}
// Ajouter une ligne a la fin (le fichier n'est jamais reecrit).
bool appendLine(const std::string& path, const std::string& line, std::string* why) {
    std::error_code ec;
    fs::create_directories(P(path).parent_path(), ec);
    std::ofstream out(P(path), std::ios::binary | std::ios::app);
    out << line << '\n';
    out.flush();
    if (!out) {
        if (why) *why = "impossible d'\xC3\xA9" "crire " + path;
        return false;
    }
    return true;
}
std::vector<std::string> linesOf(std::string_view text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty()) out.emplace_back(line);
        pos = end + 1;
    }
    return out;
}
bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}
std::string plural(std::size_t n, const std::string& one, const std::string& many) { return std::to_string(n) + " " + (n > 1 ? many : one); }
std::string cite(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }
std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    std::size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
    return s.substr(a);
}
// Couper un texte UTF-8 a `max` octets au plus, sans couper un caractere.
std::string clip(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) --n;
    s.resize(n);
    return s;
}
bool keyFromHex(const std::string& hex, crypto::PublicKey& out) {
    crypto::Bytes raw;
    if (!licensing::isKeyHex(hex) || !crypto::unhex(hex, raw, 32) || raw.size() != 32) return false;
    std::copy(raw.begin(), raw.end(), out.begin());
    return true;
}
licensing::Signer signerOf(const licensing::SigningKey& key) {
    return [&key](std::string_view m) { return key.sign(m); };
}
OpResult fromResult(const online::Result& x) {
    OpResult r;
    r.ok = x.ok;
    r.message = x.message;
    r.details = x.details;
    return r;
}

// ---- ce poste (le journal, la demande, le jeton) ----
std::string thisMachine() {
    std::string id = licensing::systemMachineId();
    if (id.empty()) id = "pc:" + deploy::DevKey::computerName();   // jamais vide : le service demande 32 hexadecimaux
    return licensing::service::machineHash(id);
}
std::string thisPc() { return clip(deploy::DevKey::computerName(), 60); }
std::string softwareHeader() { return std::string("XPG-License-Admin/") + kAppVersion; }
// Le nom de l'admin+ sans la marque d'une cle d'essai (<< CLE D'ESSAI ... (Camille Essai) >> -> << Camille Essai >>).
std::string keyOwner(const std::string& keyName) {
    if (keyName.rfind("CLE D'ESSAI", 0) != 0) return keyName;
    const auto a = keyName.rfind('('), b = keyName.rfind(')');
    if (a != std::string::npos && b != std::string::npos && b > a + 1) return keyName.substr(a + 1, b - a - 1);
    return "l'admin+ (cl\xC3\xA9 d'essai)";
}

// ---- 1.47.0 : les invitations de l'admin+ (invitations-admins.json, sur son poste) ; 1.51.0 : leurs droits, la cle
// signee d'office pour chacune (une seule fois) ----
constexpr const char* kInvitationsFile = "invitations-admins.json";
constexpr const char* kInvitationsFormat = "xpg-invitations-admins/1";
constexpr std::size_t kMaxInvitations = 100;
bool writeInvitationsFile(const std::string& folder, std::vector<AdminInvitation> list, std::string* why);
std::string newNonce() {
    std::uint8_t b[16];
    crypto::randomBytes(b, sizeof b);
    return crypto::hex(b, sizeof b);
}

// ---- les cles de l'editeur que ce poste croit ----
std::vector<crypto::PublicKey> pinnedEditorKeys(const std::string& folder) {
    std::vector<crypto::PublicKey> out;
    std::string text;
    Value v;
    if (!isFile(at(folder, kEditorKeysFile)) || !readText(at(folder, kEditorKeysFile), text, nullptr) || !core::json::parse(text, v, nullptr) || !v.isObject())
        return out;
    if (const Value* arr = v.find("cles"); arr && arr->isArray())
        for (const auto& x : arr->items()) {
            crypto::PublicKey k{};
            if (x.isString() && keyFromHex(x.asString(), k)) out.push_back(k);
        }
    return out;
}

// Retenir les cles de l'editeur que nomme le service (une fois : a l'acceptation d'un admin). Vrai : retenues.
bool pinEditorKeys(const std::string& folder, const std::vector<std::string>& hexes, std::string* why) {
    if (isFile(at(folder, kEditorKeysFile)) || hexes.empty()) return false;
    Value v = Value::object();
    v.set("format", "xpg-cles-editeur/1");
    Value arr = Value::array();
    for (const auto& h : hexes) arr.push(h);
    v.set("cles", std::move(arr));
    return writeText(at(folder, kEditorKeysFile), core::json::write(v, true) + "\n", why);
}

std::vector<crypto::PublicKey> knownEditorKeys(const std::string& folder, const licensing::SigningKey* editor) {
    std::vector<crypto::PublicKey> out;
    const auto add = [&](const crypto::PublicKey& k) {
        if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
    };
    if (editor) add(editor->publicKey());
    for (const auto& t : licensing::compiledPublisherKeys()) add(t.key);
    for (const auto& k : pinnedEditorKeys(folder)) add(k);
    return out;
}

// ---- la liste des admins ----
// Un bloc << admins >> signe par une des cles de l'editeur `editors`. Faux : `why`.
bool readListBlock(std::string_view text, const std::vector<crypto::PublicKey>& editors, adm::AdminList& out, SignedBlock* block, std::string* why) {
    std::vector<SignedBlock> blocks;
    std::string w;
    if (!licensing::readBlocks(text, blocks, &w) || blocks.size() != 1 || blocks[0].kind != DocKind::Admins)
        return fail(why, "ce n'est pas une liste des admins" + (w.empty() ? std::string() : " (" + w + ")"));
    const crypto::PublicKey* signer = nullptr;
    for (const auto& k : editors)
        if (licensing::keyIdOf(k) == blocks[0].keyId && licensing::verifyBlock(blocks[0], k)) signer = &k;
    if (!signer)
        return fail(why, "la liste des admins n'est pas sign\xC3\xA9" "e par une cl\xC3\xA9 de l'\xC3\xA9" "diteur que ce poste conna\xC3\xAEt (" + blocks[0].keyId + ")");
    if (!adm::parseAdminList(blocks[0].document, out, why)) return false;
    if (block) *block = std::move(blocks[0]);
    return true;
}

bool loadAdminList(const std::string& folder, const std::vector<crypto::PublicKey>& editors, adm::AdminList& out, std::string* why) {
    std::string text;
    if (!isFile(at(folder, kAdminsFile))) return fail(why, "");
    if (!readText(at(folder, kAdminsFile), text, why)) return false;
    return readListBlock(text, editors, out, nullptr, why);
}

// Chaque liste vue (admins-vus.jsonl) : une ligne {"numero","bloc"} par numero.
void noteSeen(const std::string& folder, const std::string& blockText, int number) {
    std::string text;
    if (isFile(at(folder, kAdminsSeenFile)) && readText(at(folder, kAdminsSeenFile), text, nullptr))
        for (const auto& line : linesOf(text)) {
            Value v;
            if (core::json::parse(line, v, nullptr) && v.isObject() && v.integer("numero") == number && v.str("bloc") == blockText) return;
        }
    Value v = Value::object();
    v.set("numero", number);
    v.set("bloc", blockText);
    (void)appendLine(at(folder, kAdminsSeenFile), core::json::write(v, false), nullptr);
}

// Garder une liste verifiee si elle est plus recente que celle du poste. `adopted` : elle est gardee.
bool storeAdminList(const std::string& folder, const std::string& blockText, const adm::AdminList& list, bool* adopted, std::string* why) {
    if (adopted) *adopted = false;
    std::string current;
    if (isFile(at(folder, kAdminsFile)) && readText(at(folder, kAdminsFile), current, nullptr)) {
        std::vector<SignedBlock> blocks;
        adm::AdminList cur;
        if (licensing::readBlocks(current, blocks, nullptr) && blocks.size() == 1 && adm::parseAdminList(blocks[0].document, cur, nullptr)) {
            if (list.number < cur.number) return true;   // plus ancienne : celle du poste reste
            if (list.number == cur.number) {
                if (current == blockText) return true;
                return fail(why, "deux listes des admins diff\xC3\xA9rentes portent le n\xC2\xB0 " + std::to_string(list.number));
            }
        }
    }
    if (!writeText(at(folder, kAdminsFile), blockText, why)) return false;
    noteSeen(folder, blockText, list.number);
    if (adopted) *adopted = true;
    return true;
}

// Les cles qui sont ou ont ete dans une liste vue (verifiee) -> le nom de l'admin.
std::map<std::string, std::string> seenAdminKeys(const std::string& folder, const std::vector<crypto::PublicKey>& editors) {
    std::map<std::string, std::string> out;
    std::string text;
    if (isFile(at(folder, kAdminsSeenFile)) && readText(at(folder, kAdminsSeenFile), text, nullptr))
        for (const auto& line : linesOf(text)) {
            Value v;
            adm::AdminList l;
            if (!core::json::parse(line, v, nullptr) || !v.isObject() || !readListBlock(v.str("bloc"), editors, l, nullptr, nullptr)) continue;
            for (const auto& a : l.admins) out[a.key] = a.name;
        }
    adm::AdminList cur;
    if (loadAdminList(folder, editors, cur, nullptr))
        for (const auto& a : cur.admins) out[a.key] = a.name;
    return out;
}

// ---- les phrases des refus ----
std::string rightSentence(std::string_view right) {
    return "tu n'as pas le droit " + cite(adm::rightLabel(right)) + " : demande-le \xC3\xA0 l'admin+";
}
const char* kPlusOnly = "r\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+ (le poste qui a la cl\xC3\xA9 de l'\xC3\xA9" "diteur)";
// 1.51.0 : plus de demande d'admin - une cle inscrite par son code d'invitation attend seulement que l'Admin+ la signe.
const char* kPendingWhy = "ta cl\xC3\xA9 n'est pas encore dans la liste sign\xC3\xA9" "e des admins : l'Admin+ l'y ajoute d'office (ton code d'invitation "
                          "accept\xC3\xA9), d\xC3\xA8s qu'il est en ligne";
const char* kRetiredWhy = "ton acc\xC3\xA8s d'admin a \xC3\xA9t\xC3\xA9 retir\xC3\xA9 : ta cl\xC3\xA9 ne signe plus rien";   // 1.44.0 : sans genre

// La phrase d'un refus, en tete de bouton grise (une majuscule, un point).
std::string sentence(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    return s + ".";
}

// ---- un registre et ses parties (le droit de chaque partie qui change) ----
const char* partRight(const std::string& member) {
    if (member == "editions") return "offres";
    if (member == "options") return "options";
    if (member == "announcements" || member == "annonces") return "annonces";
    return "offres";
}

// ---- le journal releve (l'admin+) ----
struct JournalState {
    std::uint64_t            last{0};
    std::string              hash;
    std::string              serviceKey;   // la cle du service (lue avec son certificat, signe par l'editeur)
    std::vector<std::string> problems;     // les ruptures vues au releve
};
JournalState readJournalState(const std::string& folder) {
    JournalState st;
    std::string text;
    Value v;
    if (!isFile(at(folder, kJournalStateFile)) || !readText(at(folder, kJournalStateFile), text, nullptr) || !core::json::parse(text, v, nullptr) ||
        !v.isObject())
        return st;
    st.last = static_cast<std::uint64_t>(v.integer("dernier"));
    st.hash = v.str("empreinte");
    st.serviceKey = v.str("cle_service");
    if (const Value* p = v.find("problemes"); p && p->isArray())
        for (const auto& x : p->items())
            if (x.isString()) st.problems.push_back(x.asString());
    return st;
}
bool writeJournalState(const std::string& folder, const JournalState& st, std::string* why) {
    Value v = Value::object();
    v.set("format", "xpg-journal-admins/1");
    v.set("dernier", static_cast<std::uint64_t>(st.last));
    v.set("empreinte", st.hash);
    v.set("cle_service", st.serviceKey);
    Value p = Value::array();
    for (const auto& x : st.problems) p.push(x);
    v.set("problemes", std::move(p));
    return writeText(at(folder, kJournalStateFile), core::json::write(v, true) + "\n", why);
}

// ---- les reglages partages (un "maj" en tete : ArchiveSync garde le plus recent) ----
bool writeSetting(const std::string& folder, const char* name, Value v, Seconds now, std::string* why) {
    v.set("maj", static_cast<std::int64_t>(now));
    return writeText(at(folder, name), core::json::write(v, true) + "\n", why);
}
Value readSetting(const std::string& folder, const char* name) {
    std::string text;
    Value v;
    if (!isFile(at(folder, name)) || !readText(at(folder, name), text, nullptr) || !core::json::parse(text, v, nullptr) || !v.isObject()) return Value::object();
    return v;
}

Criticality criticalityOf(const std::string& folder) {
    Criticality c;
    for (const auto& t : adm::actionTypes()) c.actions[t.id] = ActionSetting{t.level, t.notify};
    const Value v = readSetting(folder, archive::kCriticalityFile);
    c.emailCritical = v.flag("courriel_critique");
    if (const Value* types = v.find("types"); types && types->isObject())
        for (const auto& [id, o] : types->members()) {
            auto it = c.actions.find(id);
            if (it == c.actions.end() || !o.isObject()) continue;
            adm::Level l;
            if (adm::parseLevel(o.str("niveau"), l)) it->second.level = l;
            if (const Value* n = o.find("prevenir"); n && n->isBool()) it->second.notify = n->asBool();
        }
    return c;
}

struct LineNote {
    adm::Level  level{adm::Level::Info};
    std::string who;
    Seconds     when{0};
};
std::map<std::uint64_t, LineNote> lineNotesOf(const std::string& folder) {
    std::map<std::uint64_t, LineNote> out;
    const Value v = readSetting(folder, archive::kJournalNotesFile);
    if (const Value* notes = v.find("notes"); notes && notes->isObject())
        for (const auto& [k, o] : notes->members()) {
            if (!o.isObject() || k.empty() || k.size() > 15 || !std::all_of(k.begin(), k.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
            LineNote n;
            if (!adm::parseLevel(o.str("criticite"), n.level)) continue;
            n.who = o.str("qui");
            n.when = static_cast<Seconds>(o.integer("quand"));
            out[std::stoull(k)] = std::move(n);
        }
    return out;
}

// ---- le trousseau ----
// Deposer la cle de synchro de ce poste, scellee pour l'admin `keyHex` (PUT /admin/trousseau).
bool giveSyncKeyWith(online::Client& c, const std::string& folder, const std::string& keyHex, std::string* why) {
    crypto::PublicKey pk{};
    if (!keyFromHex(keyHex, pk)) return fail(why, "la cl\xC3\xA9 de l'admin est illisible");
    archive::SyncKey sk{};
    if (!archive::loadSyncKey(folder, sk, why)) return false;
    const std::string sealed = gw::sealFor(pk, std::string_view(reinterpret_cast<const char*>(sk.data()), sk.size()));
    crypto::wipe(sk);
    if (sealed.empty()) return fail(why, "impossible de sceller la cl\xC3\xA9 de synchro pour cet admin (sa cl\xC3\xA9 est refus\xC3\xA9" "e)");
    Value body = Value::object();
    body.set("cle", keyHex);
    body.set("scelle", sealed);
    const online::Reply r = c.call("PUT", "/admin/trousseau", core::json::write(body, false));
    if (r.status != 200 || !r.error.empty()) return fail(why, c.explain(r, "d\xC3\xA9poser la cl\xC3\xA9 de synchro dans le trousseau"));
    return true;
}

// Prendre la cle de synchro du trousseau (GET /admin/trousseau, ouverte par la cle d'admin) et la garder sur ce poste.
bool receiveSyncKeyWith(online::Client& c, const std::string& folder, const licensing::SigningKey& key, std::string* id, std::string* why) {
    const online::Reply r = c.call("GET", "/admin/trousseau");
    if (r.error.empty() && r.status == 404)
        return fail(why, "le trousseau n'a rien pour ta cl\xC3\xA9 : demande \xC3\xA0 l'admin+ de te donner la cl\xC3\xA9 de synchro (Administrateurs)");
    if (r.status != 200 || !r.error.empty() || !r.json.isObject()) return fail(why, c.explain(r, "lire le trousseau"));
    std::string plain;
    if (!gw::openWith(key, r.json.str("scelle"), plain, why)) return false;
    archive::SyncKey sk{};
    if (plain.size() != sk.size()) {
        crypto::wipe(plain.data(), plain.size());
        return fail(why, "le trousseau ne contient pas une cl\xC3\xA9 de synchro");
    }
    std::copy(plain.begin(), plain.end(), reinterpret_cast<char*>(sk.data()));
    crypto::wipe(plain.data(), plain.size());
    std::string code = archive::recoveryCode(sk);
    archive::SyncKey stored{};
    const bool ok = archive::restoreSyncKey(folder, code, stored, why, false);
    crypto::wipe(code.data(), code.size());
    if (ok && id) *id = archive::syncKeyId(stored);
    crypto::wipe(sk);
    crypto::wipe(stored);
    return ok;
}

// ---- la liste a modifier (l'admin+) : la plus recente de celle du poste et de celle du service ----
bool baseList(online::Client& c, const detail::Caller& caller, adm::AdminList& out, std::string* why) {
    out = adm::AdminList{};
    if (caller.admins) out = *caller.admins;
    const online::Reply r = c.call("GET", "/v1/admins", {}, false);
    if (r.error.empty() && r.status == 404) return true;   // aucune liste encore
    if (r.status != 200 || !r.error.empty()) return fail(why, c.explain(r, "lire la liste des admins"));
    adm::AdminList theirs;
    if (!readListBlock(r.body, caller.editorKeys, theirs, nullptr, why)) return false;
    if (theirs.number > out.number) out = std::move(theirs);
    return true;
}

// Signer la liste (numero + 1, par la cle de l'editeur), la publier (PUT /admin/admins), la garder sur ce poste.
bool publishList(online::Client& c, const std::string& folder, const licensing::SigningKey& editor, adm::AdminList list, Seconds now, int& number,
                 std::string* why) {
    list.number += 1;
    list.issued = now;
    const std::string doc = adm::writeAdminList(list);
    if (doc.size() > licensing::docKindMaxBytes(DocKind::Admins)) return fail(why, "la liste des admins serait trop grande (64 Kio au plus)");
    const std::string text = licensing::writeBlock(licensing::signBlock(DocKind::Admins, doc, editor.publicKey(), signerOf(editor)));
    Value body = Value::object();
    body.set("admins", text);
    const online::Reply r = c.call("PUT", "/admin/admins", core::json::write(body, false));
    if (r.status != 200 || !r.error.empty()) return fail(why, c.explain(r, "publier la liste des admins n\xC2\xB0 " + std::to_string(list.number)));
    bool adopted = false;
    if (!storeAdminList(folder, text, list, &adopted, why)) return false;
    number = list.number;
    return true;
}

// ---- les documents signes par un admin, dans l'archive ----
struct SignedDoc {
    std::string              licenseId;
    std::string              hash;          // admins::documentHash
    std::vector<std::string> devices;       // les PC lies
    bool                     online{false};
};

// Les licences de l'archive signees par cette cle (keyId) qui sont encore EN VIGUEUR (ni finies, ni revoquees).
std::vector<SignedDoc> licencesInForceBy(const std::string& folder, const std::string& keyId, Seconds now) {
    std::vector<SignedDoc> out;
    licensing::RevocationList rev;
    {
        const std::string text = archive::archivedText(folder, kRevocationsRel);
        std::vector<SignedBlock> blocks;
        if (!text.empty() && licensing::readBlocks(text, blocks, nullptr) && !blocks.empty()) (void)licensing::parseRevocations(blocks[0].document, rev, nullptr);
    }
    const fs::path dir = P(archive::archiveFolder(folder)) / "licences";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".xpglic") continue;
        std::string text;
        if (!readText(U(e.path()), text, nullptr)) continue;
        std::vector<SignedBlock> blocks;
        if (!licensing::readBlocks(text, blocks, nullptr)) continue;
        for (const auto& b : blocks) {
            if (b.kind != DocKind::License || b.keyId != keyId) continue;
            licensing::License l;
            if (!licensing::parseLicense(b.document, l, nullptr)) break;
            if (l.expiresAt != 0 && now >= l.expiresAt) break;
            const bool revoked = std::any_of(rev.revoked.begin(), rev.revoked.end(), [&](const licensing::Revocation& r) {
                return r.licenseId == l.licenseId && r.device.empty() && (r.issuedBefore == 0 || l.issuedAt < r.issuedBefore);
            });
            if (revoked) break;
            SignedDoc d;
            d.licenseId = l.licenseId;
            d.hash = adm::documentHash(b.document);
            for (const auto& dv : l.devices) d.devices.push_back(dv.key);
            d.online = !l.service.empty();
            out.push_back(std::move(d));
            break;
        }
    }
    return out;
}

// Le bloc d'un fichier de l'archive (un seul bloc : le registre, la liste de revocation).
bool archivedBlock(const std::string& folder, const char* relative, DocKind kind, SignedBlock& out) {
    const std::string text = archive::archivedText(folder, relative);
    std::vector<SignedBlock> blocks;
    if (text.empty() || !licensing::readBlocks(text, blocks, nullptr) || blocks.size() != 1 || blocks[0].kind != kind) return false;
    out = std::move(blocks[0]);
    return true;
}

// Le JSON d'entree de signRevocations pour cette liste (meme forme que Workspace::signRevocationList).
std::string revocationsInput(const licensing::RevocationList& list, std::uint64_t number) {
    Value root = Value::object();
    root.set("number", static_cast<std::uint64_t>(number));
    Value arr = Value::array();
    for (const auto& x : list.revoked) {
        Value e = Value::object();
        e.set("license_id", x.licenseId);
        e.set("reason", x.reason);
        if (!x.device.empty()) e.set("device", x.device);
        if (x.revokedAt) e.set("revoked_at", licensing::isoTime(x.revokedAt));
        if (x.issuedBefore) e.set("issued_before", licensing::isoTime(x.issuedBefore));
        arr.push(std::move(e));
    }
    root.set("revoked", std::move(arr));
    return core::json::write(root);
}

// Garder les documents d'un admin qui perd un droit (ou est retire) : `kinds` les genres qui ne sont plus permis.
// Faux : plus de 512 empreintes gardees (`why`).
bool keepDocuments(const std::string& folder, adm::Admin& a, const std::vector<DocKind>& kinds, Seconds now, std::vector<std::string>& details,
                   std::string* why) {
    std::set<std::string> kept(a.kept.begin(), a.kept.end());
    std::size_t added = 0;
    const auto keep = [&](const std::string& h) {
        if (kept.insert(h).second) {
            a.kept.push_back(h);
            ++added;
        }
    };
    for (const DocKind k : kinds) {
        if (k == DocKind::License) {
            for (const auto& d : licencesInForceBy(folder, a.id, now)) keep(d.hash);
        } else {
            SignedBlock b;
            if (archivedBlock(folder, k == DocKind::Registry ? kRegistryRel : kRevocationsRel, k, b) && b.keyId == a.id) keep(adm::documentHash(b.document));
        }
    }
    if (a.kept.size() > adm::kMaxKept)
        return fail(why, a.name + " a sign\xC3\xA9 trop de documents encore en vigueur (" + std::to_string(a.kept.size()) + " \xC3\xA0 garder, " +
                             std::to_string(adm::kMaxKept) + " au plus) : re-signe d'abord ses anciennes licences avec ta cl\xC3\xA9 (prolonge-les), puis recommence");
    if (added) details.push_back(plural(added, "document sign\xC3\xA9 par " + a.name + " reste valable (gard\xC3\xA9)", "documents sign\xC3\xA9s par " + a.name + " restent valables (gard\xC3\xA9s)") + ".");
    return true;
}

// Les genres de document qu'un admin ne peut plus signer quand ses droits passent de `before` a `after`.
std::vector<DocKind> lostKinds(const std::vector<std::string>& before, const std::vector<std::string>& after) {
    std::vector<DocKind> out;
    for (const DocKind k : {DocKind::License, DocKind::Registry, DocKind::Revocations}) {
        const auto need = adm::rightsForKind(k);
        const auto any = [&](const std::vector<std::string>& v) {
            return std::any_of(need.begin(), need.end(), [&](const std::string& r) { return std::find(v.begin(), v.end(), r) != v.end(); });
        };
        if (any(before) && !any(after)) out.push_back(k);
    }
    return out;
}

// Le jeton d'admin (jeton-admin.xpgjet), signe par `key`.
bool writeTokenWith(const std::string& folder, const licensing::SigningKey& key, const std::string& name, Seconds now, bool force, bool* written,
                    std::string* why) {
    if (written) *written = false;
    const std::string path = at(folder, kAdminTokenFile);
    if (!force && isFile(path)) {
        std::string text;
        std::vector<SignedBlock> blocks;
        adm::AdminToken t;
        if (readText(path, text, nullptr) && licensing::readBlocks(text, blocks, nullptr) && blocks.size() == 1 && blocks[0].kind == DocKind::AdminToken &&
            licensing::verifyBlock(blocks[0], key.publicKey()) && adm::parseAdminToken(blocks[0].document, t, nullptr) && t.key == key.publicKeyHex() &&
            t.version == kAppVersion && t.name == name && t.expires - kTokenDays * kDay + kDay > now)
            return true;   // a jour (moins d'un jour)
    }
    adm::AdminToken t;
    t.key = key.publicKeyHex();
    t.machine = thisMachine();
    t.expires = now + kTokenDays * kDay;
    t.version = kAppVersion;
    t.name = clip(name, 60);
    const std::string text = licensing::writeBlock(licensing::signBlock(DocKind::AdminToken, adm::writeAdminToken(t), key.publicKey(), signerOf(key)));
    if (!writeText(path, text, why)) return false;
    if (written) *written = true;
    return true;
}

std::string tokenName(const detail::Caller& c) {
    if (c.role == Role::Plus) return "admin+";
    const adm::Admin* m = c.me();
    return m ? m->name : std::string("admin");
}

// Le mutex du journal a envoyer (le fil de l'interface y ajoute, un travail lent l'envoie).
std::mutex& outboxMutex() {
    static std::mutex m;
    return m;
}

} // namespace

// ============================================================================================ les roles
const char* roleKey(Role r) noexcept {
    switch (r) {
        case Role::None: return "aucun";
        case Role::Plus: return "admin+";
        case Role::Admin: return "admin";
        case Role::Pending: return "attente";
        case Role::Retired: return "retire";
    }
    return "aucun";
}

std::string roleLabel(Role r) {
    switch (r) {
        case Role::None: return "Pas encore admin";
        case Role::Plus: return "Admin+";
        case Role::Admin: return "Admin";
        case Role::Pending: return "Admin (ajout en cours)";   // 1.51.0 : inscrit par son code, l'Admin+ le signe d'office
        case Role::Retired: return "Admin retir\xC3\xA9";
    }
    return "Pas encore admin";
}

// ============================================================================================ detail
namespace detail {

const adm::Admin* Caller::me() const {
    if (!key || !admins) return nullptr;
    return admins->byKey(key->publicKeyHex());
}

bool Caller::can(std::string_view right) const {
    if (role == Role::Plus) return true;
    if (role != Role::Admin) return false;
    const adm::Admin* m = me();
    return m && m->has(right);
}

bool loadCaller(const std::string& folder, Caller& out, std::string* why) {
    out = Caller{};
    std::string w;
    if (!license_admin::classique() && isFile(publisherKeyPath(folder))) {   // 1.49.0 : XPG Admin, jamais la cle de l'editeur
        out.key = loadPublisherKey(folder, &w);
        if (!out.key) return fail(why, "la cl\xC3\xA9 de l'\xC3\xA9" "diteur ne se relit pas (elle signe chaque appel au service) : " + w);
        out.role = Role::Plus;
    } else if (isFile(adminKeyPath(folder))) {
        out.key = loadAdminKey(folder, &w);
        if (!out.key) return fail(why, "ta cl\xC3\xA9 d'admin ne se relit pas (elle signe chaque appel au service) : " + w);
        out.role = Role::Pending;
    } else {
        return fail(why, "il faut la cl\xC3\xA9 de l'\xC3\xA9" "diteur, ou une cl\xC3\xA9 d'admin (elle signe chaque appel au service) : ce poste n'en a pas "
                         "(l'Admin+ : Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9, cr\xC3\xA9" "er la cl\xC3\xA9 ; XPG Admin : ton code d'invitation)");
    }
    out.editorKeys = knownEditorKeys(folder, out.role == Role::Plus ? out.key.get() : nullptr);
    adm::AdminList l;
    if (loadAdminList(folder, out.editorKeys, l, nullptr)) out.admins = std::move(l);
    if (out.role != Role::Plus) {
        const adm::Admin* m = out.me();
        out.role = !m ? Role::Pending : m->active() ? Role::Admin : Role::Retired;
    }
    return true;
}

std::optional<OpResult> callerDenied(const Caller& c, std::string_view right) {
    if (c.role == Role::Plus) return std::nullopt;
    if (right.empty()) return OpResult::fail(license_admin::classique() ? kClassicRefusal : kPlusOnly);   // 1.49.0 : XPG Admin le dit
    switch (c.role) {
        case Role::Admin: return c.can(right) ? std::nullopt : std::optional<OpResult>(OpResult::fail(rightSentence(right)));
        case Role::Pending: return OpResult::fail(kPendingWhy);
        case Role::Retired: return OpResult::fail(kRetiredWhy);
        default: return OpResult::fail("ce poste n'a pas de cl\xC3\xA9 qui signe (ni l'admin+, ni un admin de la liste)");
    }
}

online::Options optionsFor(const Caller& c, Seconds now) {
    online::Options o;
    o.now = now;
    o.tool = std::string("XPG-License-Manager-Admin/") + kAppVersion;
    o.editorKeys = c.editorKeys;
    o.admins = c.admins;
    o.software = softwareHeader();
    return o;
}

bool queueJournal(const std::string& folder, const licensing::SigningKey& key, Seconds now, const std::string& action, const std::string& object,
                  const std::string& summary, const std::string& detailJson, const std::string& beforeJson, const std::string& afterJson,
                  std::string* why) {
    const adm::ActionType* type = adm::actionType(action);
    if (!type || type->service) return fail(why, "type d'action inconnu au journal des admins : " + action);
    adm::JournalEntry e;
    e.when = now;
    e.action = action;
    e.object = clip(object, 200);
    e.summary = clip(summary, 400);
    e.detail = detailJson;
    e.before = beforeJson;
    e.after = afterJson;
    e.pc = thisPc();
    e.machine = thisMachine();
    e.software = kAppVersion;
    e.nonce = newNonce();
    std::string entry = adm::writeJournalEntry(e);
    if (entry.size() > 16 * 1024) {   // 16 Kio au plus : le detail est coupe, la ligne reste
        e.detail.clear();
        e.before.clear();
        e.after.clear();
        entry = adm::writeJournalEntry(e);
    }
    const crypto::Signature sig = key.sign(adm::journalMessage(entry));
    Value line = Value::object();
    line.set("cle", key.publicKeyHex());
    line.set("entree", entry);
    line.set("signature", crypto::hex(sig));
    std::lock_guard<std::mutex> lock(outboxMutex());
    const bool ok = appendLine(at(folder, kOutboxFile), core::json::write(line, false), why);
    if (ok) changes::bump();   // 1.48.1 : une action journalisee - toutes les vues se relisent
    return ok;
}

int flushJournalWith(const std::string& folder, online::Client& client, std::string* why) {
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(outboxMutex());
        std::string text;
        if (!isFile(at(folder, kOutboxFile)) || !readText(at(folder, kOutboxFile), text, nullptr)) return 0;
        lines = linesOf(text);
    }
    if (lines.empty()) return 0;
    const std::string mine = client.key().publicKeyHex();
    std::set<std::string> done;
    int sent = 0;
    for (const auto& line : lines) {
        Value v;
        if (!core::json::parse(line, v, nullptr) || !v.isObject()) {
            done.insert(line);   // illisible : jamais envoyable
            continue;
        }
        if (v.str("cle") != mine) continue;   // signee par une autre cle de ce poste : elle attend la sienne
        Value body = Value::object();
        body.set("entree", v.str("entree"));
        body.set("signature", v.str("signature"));
        const online::Reply r = client.call("POST", "/admin/journal", core::json::write(body, false));
        if (r.error.empty() && (r.status == 200 || r.status == 201)) {
            done.insert(line);
            ++sent;
        } else if (r.error.empty() && r.status == 400) {
            done.insert(line);   // refusee pour sa forme : la renvoyer n'y changerait rien
            if (why) *why = client.explain(r, "envoyer une ligne du journal des admins");
        } else {
            if (why) *why = client.explain(r, "envoyer le journal des admins");
            break;
        }
    }
    if (!done.empty()) {
        std::lock_guard<std::mutex> lock(outboxMutex());
        std::string text;
        (void)readText(at(folder, kOutboxFile), text, nullptr);   // relu : une ligne ajoutee entre-temps reste
        std::string rest;
        for (const auto& line : linesOf(text))
            if (!done.count(line)) rest += line + "\n";
        (void)writeText(at(folder, kOutboxFile), rest, nullptr);
    }
    return sent;
}

int flushJournalQuiet(const std::string& folder, const Caller& c, Seconds now) {
    if (!c.key || (c.role != Role::Plus && c.role != Role::Admin)) return 0;
    {
        std::lock_guard<std::mutex> lock(outboxMutex());
        std::string text;
        if (!isFile(at(folder, kOutboxFile)) || !readText(at(folder, kOutboxFile), text, nullptr) || linesOf(text).empty()) return 0;
    }
    std::unique_ptr<online::Client> client;
    if (!online::openClient(folder, *c.key, optionsFor(c, now), client, nullptr)) return 0;
    return flushJournalWith(folder, *client, nullptr);
}

std::vector<std::string> registryRights(const licensing::Registry* before, const licensing::Registry& after) {
    static const std::set<std::string> outside = {"format", "version", "number", "issued_at"};
    Value a = Value::object(), b;
    if (before) (void)core::json::parse(licensing::writeRegistry(*before), a, nullptr);
    if (!core::json::parse(licensing::writeRegistry(after), b, nullptr) || !b.isObject()) return {"offres"};
    if (!a.isObject()) a = Value::object();
    std::set<std::string> keys;
    for (const auto& [k, v] : a.members()) keys.insert(k);
    for (const auto& [k, v] : b.members()) keys.insert(k);
    std::vector<std::string> need;
    for (const auto& k : keys) {
        if (outside.count(k)) continue;
        const Value* x = a.find(k);
        const Value* y = b.find(k);
        const std::string sx = x ? core::json::write(*x, false) : std::string();
        const std::string sy = y ? core::json::write(*y, false) : std::string();
        if (sx != sy) need.emplace_back(partRight(k));
    }
    return adm::normalizeRights(need);
}

} // namespace detail

// ============================================================================================ le role
void Workspace::loadAdmins() {
    adminSigning_.reset();
    adminKey_ = AdminKeyStatus{};
    adminList_.reset();
    adminKey_.path = adminKeyPath(folder_);
    if (isFile(adminKey_.path)) {
        std::string why;
        adminSigning_ = loadAdminKey(folder_, &why);
        if (adminSigning_) {
            adminKey_.present = true;
            adminKey_.keyHex = adminSigning_->publicKeyHex();
            adminKey_.id = adminSigning_->keyId();
            adminKey_.name = adminSigning_->name();
            adminKey_.fingerprint = adm::fingerprint(adminKey_.keyHex);
        } else {
            adminKey_.problem = why;
        }
    } else {
        adminKey_.problem = "pas de cl\xC3\xA9 d'admin sur ce poste";
    }
    editorKeys_ = knownEditorKeys(folder_, signing_.get());
    adm::AdminList l;
    if (loadAdminList(folder_, editorKeys_, l, nullptr)) adminList_ = std::move(l);
    if (signing_) role_ = Role::Plus;
    else if (adminSigning_) {
        const adm::Admin* m = me();
        role_ = !m ? Role::Pending : m->active() ? Role::Admin : Role::Retired;
    } else role_ = Role::None;
    if (!alertsPrimed_) {   // le premier chargement : les lignes deja relevees ne sont pas de nouvelles alertes
        alertedUpTo_ = readJournalState(folder_).last;
        alertsPrimed_ = true;
    }
}

const licensing::SigningKey* Workspace::signer() const noexcept {
    if (role_ == Role::Plus) return signing_.get();
    if (role_ == Role::Admin) return adminSigning_.get();
    return nullptr;
}

const adm::Admin* Workspace::me() const {
    if (!adminList_ || !adminSigning_) return nullptr;
    return adminList_->byKey(adminKey_.keyHex);
}

std::vector<std::string> Workspace::myRights() const {
    if (role_ == Role::Plus) return adm::allRights();
    if (role_ == Role::Admin)
        if (const adm::Admin* m = me()) return adm::normalizeRights(m->rights);
    return {};
}

bool Workspace::can(std::string_view right) const {
    if (role_ == Role::Plus) return true;
    if (role_ != Role::Admin) return false;
    const adm::Admin* m = me();
    return m && m->has(right);
}

std::string Workspace::whyNot(std::string_view right) const {
    switch (role_) {
        case Role::Plus: return {};
        case Role::Admin: return can(right) ? std::string() : sentence(rightSentence(right));
        case Role::Pending: return sentence(kPendingWhy);
        case Role::Retired: return sentence(kRetiredWhy);
        case Role::None: break;
    }
    return sentence(key_.problem.empty() ? std::string("pas encore de cl\xC3\xA9 sur ce poste") : key_.problem);
}

std::optional<OpResult> Workspace::denied(std::string_view right) const {
    if (role_ == Role::Plus) return std::nullopt;
    if (role_ == Role::None) return OpResult::fail(key_.problem);   // comme avant la 1.44.0 : pas de cle
    if (role_ == Role::Admin && can(right)) return std::nullopt;
    if (role_ == Role::Admin) return OpResult::fail(rightSentence(right));
    return OpResult::fail(role_ == Role::Pending ? kPendingWhy : kRetiredWhy);
}

std::string Workspace::licenceRight(const LicenceDraft& d) const {
    if (!d.extend) return "licence_creer";
    if (!d.dropDevices.empty()) return "poste_retirer";
    const LicenceEntry* old = licence(d.licenseId);
    if (d.options && old && d.options->size() > old->licence.options.size()) return "licence_option";
    return "licence_prolonger";
}

std::function<bool(const SignedBlock&)> Workspace::alsoTrusted() const {
    auto editors = editorKeys_;
    auto list = adminList_;
    return [editors = std::move(editors), list = std::move(list)](const SignedBlock& b) {
        for (const auto& k : editors)
            if (licensing::keyIdOf(k) == b.keyId && licensing::verifyBlock(b, k)) return true;
        return list && adm::verifyAdminBlock(*list, b, nullptr);
    };
}

std::string Workspace::withAdminList(const std::string& signedText) const {
    if (!adminList_) return signedText;
    std::vector<SignedBlock> blocks;
    if (!licensing::readBlocks(signedText, blocks, nullptr)) return signedText;
    // Un bloc signe par un admin de la liste (pas par une cle de l'editeur) : la liste en vigueur va devant.
    const bool byAdmin = std::any_of(blocks.begin(), blocks.end(), [&](const SignedBlock& b) {
        return b.kind != DocKind::Admins && adminList_->byKeyId(b.keyId) != nullptr;
    });
    if (!byAdmin) return signedText;
    std::string list;
    if (!readText(at(folder_, kAdminsFile), list, nullptr) || list.empty()) return signedText;
    std::string out = list;
    if (out.back() != '\n') out += '\n';
    for (const auto& b : blocks)
        if (b.kind != DocKind::Admins) out += licensing::writeBlock(b);   // une liste plus ancienne, deja dans le fichier : remplacee
    return out;
}

// ============================================================================================ devenir admin
// 1.51.0 (docs/ADMINS.md, par. 4) : PLUS DE DEMANDE D'ADMIN. Le code d'invitation pose par l'installateur
// (invitation.txt, une ligne) ou tape au premier lancement INSCRIT la cle d'un coup ; l'Admin+ la signe d'office.
std::string Workspace::pendingInvitationCode() const {
    std::string text;
    if (!isFile(at(folder_, kInvitationFile)) || !readText(at(folder_, kInvitationFile), text, nullptr) || text.size() > 400) return {};
    const auto lines = linesOf(text);
    return lines.empty() ? std::string() : adm::normalizeInvitationCode(lines.front());
}

OpResult Workspace::joinWithInvitation(const std::string& invitationIn, bool* refused) const {
    if (refused) *refused = false;
    // Le code se verifie ici avant de partir : un code mal ecrit se retape (la cle n'est pas creee pour rien).
    const std::string invitation = adm::normalizeInvitationCode(invitationIn);
    if (invitation.empty()) {
        if (refused) *refused = true;
        return OpResult::fail(trimmed(invitationIn).empty()
                                  ? std::string("tape ton code d'invitation (INV-XXXX-XXXX) : il est dans le message de l'admin+")
                                  : std::string("ce code d'invitation n'est pas \xC3\xA9" "crit comme il faut : INV- puis 8 caract\xC3\xA8res (ex. INV-7K3Q-M9TX), comme dans le message de l'admin+"));
    }
    if (isFile(publisherKeyPath(folder_))) return OpResult::fail("ce poste a la cl\xC3\xA9 de l'\xC3\xA9" "diteur : il est l'admin+, il fait d\xC3\xA9j\xC3\xA0 tout");
    // Sans adresse du service, rien ne part : la cle d'admin n'est pas creee.
    if (!online::configured(folder_))
        return OpResult::fail("il faut d'abord l'adresse du service de l'admin+ : XPG Admin la prend de la connexion sign\xC3\xA9" "e (\xC3\xA0 c\xC3\xB4t\xC3\xA9 de "
                              "l'installateur, ou du Studio de ce PC) ; sinon Service en ligne \xE2\x80\xBA Utiliser une connexion\xE2\x80\xA6 (le fichier que l'admin+ t'envoie)");
    std::string why;
    auto key = isFile(adminKeyPath(folder_)) ? loadAdminKey(folder_, &why) : createAdminKey(folder_, {}, &why);
    if (!key) return OpResult::fail(why);
    online::Options o;
    o.now = now();
    o.tool = std::string("XPG-License-Manager-Admin/") + kAppVersion;
    o.software = softwareHeader();
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *key, o, c, &why)) return OpResult::fail(why);
    const std::string mine = adm::fingerprint(key->publicKeyHex());
    Value body = Value::object();
    body.set("invitation", invitation);
    body.set("pc", thisPc());
    body.set("poste", thisMachine());
    body.set("logiciel", std::string(license_admin::classique() ? "XPG-Admin " : "XPG-License-Admin ") + kAppVersion);
    const online::Reply r = c->call("POST", "/v1/admins/inscription", core::json::write(body, false));
    // Le service a repondu (accepte ou refuse) : invitation.txt a servi - efface (un code refuse se retape).
    if (r.error.empty() && r.status > 0 && isFile(at(folder_, kInvitationFile))) {
        std::error_code ec;
        fs::remove(P(at(folder_, kInvitationFile)), ec);
    }
    if (r.error.empty() && r.status == 403 && r.code() == "invitation") {
        if (refused) *refused = true;
        const std::string said = r.json.isObject() ? r.json.str("message") : std::string();
        return OpResult::fail(said.empty() ? std::string("le service refuse ce code d'invitation") : said);
    }
    if (r.error.empty() && r.status == 404)
        return OpResult::fail("le service de l'admin+ ne conna\xC3\xAEt pas encore l'inscription par code (un service d'avant la 1.51) : "
                              "demande \xC3\xA0 l'admin+ de le mettre \xC3\xA0 jour (D\xC3\xA9ployer le service), puis retape ton code");
    if (r.error.empty() && r.status == 200 && r.json.isObject() && r.json.str("etat") == "admis") {
        OpResult res = OpResult::done("Ta cl\xC3\xA9 est d\xC3\xA9j\xC3\xA0 celle d'un admin de la liste : tu es admin.");
        res.output = "admis";
        return res;
    }
    if (r.status != 201 || !r.error.empty() || !r.json.isObject()) return OpResult::fail(c->explain(r, "t'inscrire avec ton code d'invitation"));
    const std::string theirs = r.json.str("empreinte");
    if (theirs != mine)
        return OpResult::fail("l'empreinte rendue par le service (" + theirs + ") n'est pas celle de ta cl\xC3\xA9 (" + mine +
                              ") : pr\xC3\xA9viens l'admin+ (le service n'est peut-\xC3\xAAtre pas le sien)");
    const Value* inv = r.json.find("invitation");
    Value note = Value::object();
    note.set("format", "xpg-inscription-admin/1");
    if (inv && inv->isObject()) {
        note.set("nom", clip(inv->str("nom"), 60));
        note.set("courriel", clip(inv->str("courriel"), 120));
        note.set("profil", clip(inv->str("profil"), 60));
        note.set("invitation", clip(inv->str("id"), 40));
    }
    note.set("empreinte", mine);
    note.set("inscrit", static_cast<std::int64_t>(now()));
    (void)writeText(at(folder_, kJoinFile), core::json::write(note, true) + "\n", nullptr);
    const std::string profile = inv && inv->isObject() ? inv->str("profil") : std::string();
    OpResult res = OpResult::done("Code d'invitation accept\xC3\xA9 : tu es admin" + (profile.empty() ? std::string() : " (profil " + cite(profile) + ")") +
                                  ". L'Admin+ ajoute ta cl\xC3\xA9 \xC3\xA0 la liste sign\xC3\xA9" "e d'office : tes droits s'ouvrent tout seuls.");
    res.details.push_back("Ta cl\xC3\xA9 d'admin est n\xC3\xA9" "e sur ce poste (" + key->keyId() + ") et ne le quitte jamais.");
    res.details.push_back("Rien d'autre \xC3\xA0 faire : ni demande, ni code \xC3\xA0 dire \xC3\xA0 l'admin+.");
    res.output = "inscrit";
    return res;
}

OpResult Workspace::joinState(std::string& state) const {
    state.clear();
    std::string why;
    auto key = loadAdminKey(folder_, &why);
    if (!key) return OpResult::fail("pas de cl\xC3\xA9 d'admin sur ce poste : tape d'abord ton code d'invitation (" + why + ")");
    online::Options o;
    o.now = now();
    o.tool = std::string("XPG-License-Manager-Admin/") + kAppVersion;
    o.software = softwareHeader();
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *key, o, c, &why)) return OpResult::fail(why);
    const online::Reply r = c->call("GET", "/v1/admins/inscription");
    if (r.error.empty() && r.status == 404) {
        state = "aucune";
        return OpResult::fail("le service n'a pas d'inscription pour ta cl\xC3\xA9 : tape le code d'invitation que t'envoie l'admin+ (Administrateurs)");
    }
    if (r.status != 200 || !r.error.empty() || !r.json.isObject()) return OpResult::fail(c->explain(r, "lire o\xC3\xB9 en est ton inscription d'admin"));
    state = r.json.str("etat");
    if (state == "admis") return OpResult::done("Tu es admin.");
    if (state == "retire") return OpResult::done("Tu as \xC3\xA9t\xC3\xA9 retir\xC3\xA9 de la liste des admins.");
    state = "inscrit";
    return OpResult::done("Ton code d'invitation est accept\xC3\xA9 : l'Admin+ ajoute ta cl\xC3\xA9 \xC3\xA0 la liste sign\xC3\xA9" "e d\xC3\xA8s qu'il est en ligne.");
}

// 1.49.0 (docs/ADMINS.md, par. 12) : l'invitation par codes - creee sur le service (l'admin+), notee ici sans le code.
OpResult Workspace::inviteAdmin(const std::string& nameIn, const std::string& emailIn, const std::string& profileIn, std::string& code,
                                AdminInvitation* noted, const std::vector<std::string>* rightsIn) const {
    code.clear();
    const std::string name = clip(trimmed(nameIn), 60), email = trimmed(emailIn), profile = clip(trimmed(profileIn), 60);
    if (name.empty()) return OpResult::fail("son nom : il va avec l'invitation (c'est son nom dans la liste des admins)");
    if (!email.empty() && (email.size() > 120 || email.find('@') == std::string::npos || email.find(' ') != std::string::npos))
        return OpResult::fail("son courriel n'est pas une adresse valable");
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    // 1.51.0 : les droits de l'invitation (une copie du profil, ou ceux donnes) - la cle qui servira le code sera admin
    // avec eux, d'office : ils sont notes ICI avec l'invitation (jamais sur le service).
    std::vector<std::string> rights;
    if (rightsIn) {
        rights = adm::normalizeRights(*rightsIn);
    } else if (!profile.empty()) {
        const auto all = profiles();
        const auto it = std::find_if(all.begin(), all.end(), [&](const adm::Profile& p) { return p.name == profile; });
        if (it == all.end()) return OpResult::fail("le profil " + cite(profile) + " n'existe pas (Administrateurs \xE2\x80\xBA Droits)");
        rights = adm::normalizeRights(it->rights);
    }
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    Value body = Value::object();
    body.set("nom", name);
    body.set("courriel", email);
    body.set("profil", profile);
    const online::Reply r = c->call("POST", "/admin/admins/invitation", core::json::write(body, false));
    if (r.status != 201 || !r.error.empty() || !r.json.isObject()) return OpResult::fail(c->explain(r, "cr\xC3\xA9" "er le code d'invitation"));
    const std::string got = adm::normalizeInvitationCode(r.json.str("code"));
    const std::string id = clip(r.json.str("id"), 40);
    const Seconds expires = static_cast<Seconds>(r.json.integer("expire"));
    if (got.empty() || got != r.json.str("code") || id.empty()) return OpResult::fail("le service rend un code d'invitation illisible");
    code = got;
    AdminInvitation inv{name, email, profile, "code", now(), id, expires};
    inv.rights = rights;
    inv.rightsSet = true;
    const OpResult kept = noteAdminInvitation(inv);
    if (!kept.ok)   // sans elle, l'Admin+ ne signerait jamais la cle qui servira le code
        return OpResult::fail("l'invitation n'est pas not\xC3\xA9" "e sur ce poste (" + kept.message + ") : ce code ne servirait \xC3\xA0 rien, ne l'envoie pas");
    if (noted) *noted = inv;
    Value d = Value::object();
    d.set("nom", name);
    if (!email.empty()) d.set("courriel", email);
    d.set("profil", profile);
    d.set("invitation", id);
    d.set("expire", static_cast<std::int64_t>(expires));
    Value after = Value::object();
    Value rl = Value::array();
    for (const auto& x : rights) rl.push(x);
    after.set("droits", std::move(rl));
    (void)detail::queueJournal(folder_, *caller.key, now(), "admin_inviter", id, "a invit\xC3\xA9 " + name + " \xC3\xA0 devenir admin (code de 48 h)",
                               core::json::write(d, false), "{}", core::json::write(after, false), nullptr);
    (void)detail::flushJournalWith(folder_, *c);
    OpResult res = OpResult::done("Code d'invitation pour " + name + " : " + code + " (valable 48 h, une seule fois).");
    res.details.push_back("Il le saisit dans l'installateur XPG Admin : il est admin d\xC3\xA8s que son code est accept\xC3\xA9 (" +
                          (profile.empty() ? std::to_string(rights.size()) + " droits sur 13" : "profil " + cite(profile)) + "), sans rien d'autre \xC3\xA0 faire.");
    res.details.push_back("Le service n'en garde que l'empreinte : ce code ne se revoit plus.");
    res.output = code;
    return res;
}

OpResult Workspace::refreshAdminList(AdminRefresh* outRefresh) const {
    AdminRefresh res;
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    OpResult out = OpResult::done("");
    // Un admin accepte : les cles de l'editeur que nomme son service, retenues une fois (un Admin construit sans elles).
    if (caller.role != Role::Plus && !isFile(at(folder_, kEditorKeysFile))) {
        const online::Reply r = c->call("GET", "/admin/admins");
        if (r.status == 200 && r.error.empty() && r.json.isObject()) {
            std::vector<std::string> hexes;
            if (const Value* eds = r.json.find("editeurs"); eds && eds->isArray())
                for (const auto& e : eds->items())
                    if (licensing::isKeyHex(e.str("cle"))) hexes.push_back(e.str("cle"));
            if (pinEditorKeys(folder_, hexes, &why))
                out.details.push_back("Cl\xC3\xA9 de l'\xC3\xA9" "diteur retenue (celle que nomme le service) : " + plural(hexes.size(), "cl\xC3\xA9", "cl\xC3\xA9s") + ".");
        }
    }
    const auto editors = knownEditorKeys(folder_, caller.role == Role::Plus ? caller.key.get() : nullptr);
    const online::Reply r = c->call("GET", "/v1/admins", {}, false);
    if (r.error.empty() && r.status == 404) {
        out.message = "Le service n'a pas encore de liste des admins.";
        if (outRefresh) *outRefresh = res;
        return out;
    }
    if (r.status != 200 || !r.error.empty()) return OpResult::fail(c->explain(r, "lire la liste des admins"));
    adm::AdminList list;
    SignedBlock block;
    if (!readListBlock(r.body, editors, list, &block, &why)) return OpResult::fail(why);
    bool adopted = false;
    if (!storeAdminList(folder_, licensing::writeBlock(block), list, &adopted, &why)) return OpResult::fail(why);
    res.listAdopted = adopted;
    res.listNumber = list.number;
    out.message = adopted ? "Liste des admins n\xC2\xB0 " + std::to_string(list.number) + " gard\xC3\xA9" "e sur ce poste."
                          : "Liste des admins \xC3\xA0 jour (n\xC2\xB0 " + std::to_string(std::max(list.number, caller.admins ? caller.admins->number : 0)) + ").";
    if (caller.role != Role::Plus) {
        const adm::Admin* m = list.byKey(caller.key->publicKeyHex());
        if (m && m->active()) {
            res.admitted = caller.role == Role::Pending;
            if (res.admitted) out.details.push_back("Tu es admin : " + (m->rights.empty() ? std::string("aucun droit pour l'instant") : plural(m->rights.size(), "droit", "droits")) + ".");
            if (!archive::hasSyncKey(folder_)) {
                std::string id, w;
                if (receiveSyncKeyWith(*c, folder_, *caller.key, &id, &w)) {
                    res.syncKeyReceived = true;
                    out.details.push_back("Cl\xC3\xA9 de synchro re\xC3\xA7ue par le trousseau : " + id + " (Service en ligne \xE2\x80\xBA Recevoir l'archive).");
                } else {
                    out.details.push_back("Cl\xC3\xA9 de synchro : " + w + ".");
                }
            }
            bool written = false;
            (void)writeTokenWith(folder_, *caller.key, m->name, now(), false, &written, nullptr);
        } else if (m) {
            out.details.push_back(sentence(kRetiredWhy));
        }
    }
    if (outRefresh) *outRefresh = res;
    return out;
}

OpResult Workspace::receiveSyncKey() const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (caller.role == Role::Plus) return OpResult::fail("l'admin+ cr\xC3\xA9" "e la cl\xC3\xA9 de synchro (ou la redonne par son code de secours) : le trousseau est pour les admins");
    if (const auto deny = detail::callerDenied(caller, "demandes"); deny && caller.role != Role::Admin) return *deny;
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    std::string id;
    if (!receiveSyncKeyWith(*c, folder_, *caller.key, &id, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Cl\xC3\xA9 de synchro re\xC3\xA7ue par le trousseau : " + id + ".");
    r.details.push_back("Ensuite : Recevoir l'archive (Service en ligne).");
    return r;
}

OpResult Workspace::refreshAdminState(const online::AdminWatch* etat, AdminRefresh* outRefresh) const {
    AdminRefresh res;
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    OpResult out = OpResult::done("Admins : rien de neuf.");
    const auto merge = [&](const OpResult& r, const AdminRefresh& x) {
        res.listAdopted = res.listAdopted || x.listAdopted;
        res.listNumber = std::max(res.listNumber, x.listNumber);
        res.admitted = res.admitted || x.admitted;
        res.syncKeyReceived = res.syncKeyReceived || x.syncKeyReceived;
        if (!r.message.empty()) out.details.push_back(r.message);
        for (const auto& d : r.details) out.details.push_back(d);
        if (!r.ok) out.ok = false;
    };
    if (caller.role == Role::Pending) {   // en cours d'ajout : l'inscription (l'admin n'a pas encore acces a /admin/etat)
        std::string state;
        const OpResult s = joinState(state);
        res.joinState = state;
        if (state == "admis") {
            AdminRefresh x;
            const OpResult r = refreshAdminList(&x);
            merge(r, x);
            out.message = x.admitted ? "Tu es admin." : r.message;
        } else {
            out = s;
        }
        if (outRefresh) *outRefresh = res;
        return out;
    }
    online::AdminWatch watched;
    if (!etat) {   // pas de regard donne : le lire (un admin retire : 403 retire, la liste publique le dit)
        const OpResult w = fromResult(online::watchState(folder_, *caller.key, detail::optionsFor(caller, now()), watched));
        if (!w.ok) {
            AdminRefresh x;
            const OpResult r = refreshAdminList(&x);
            merge(r, x);
            out.ok = false;
            out.message = w.message;
            if (outRefresh) *outRefresh = res;
            return out;
        }
        etat = &watched;
    }
    const int local = caller.admins ? caller.admins->number : 0;
    bool needList = etat->admins > local;
    if (caller.role == Role::Admin && etat->role == "admin")
        if (const adm::Admin* m = caller.me(); m && adm::normalizeRights(etat->rights) != adm::normalizeRights(m->rights)) needList = true;
    if (caller.role == Role::Retired) needList = true;
    if (needList) {
        AdminRefresh x;
        const OpResult r = refreshAdminList(&x);
        merge(r, x);
        if (x.listAdopted) out.message = "Liste des admins n\xC2\xB0 " + std::to_string(x.listNumber) + " gard\xC3\xA9" "e.";
    }
    // 1.51.0 : les admins inscrits par leur code d'invitation - signes d'office (plus de demande a accepter).
    if (caller.role == Role::Plus && etat->invitedWaiting > 0) {
        std::vector<std::string> names;
        const OpResult a = admitInvitedAdmins(&names);
        res.invitedAdmitted = names;
        if (!names.empty()) {
            res.listAdopted = true;
            out.message = a.message;
        }
        if (!a.ok) out.details.push_back(a.message);
        for (const auto& d : a.details) out.details.push_back(d);
    }
    if (caller.role == Role::Plus && etat->journal > static_cast<std::int64_t>(readJournalState(folder_).last)) {
        int added = 0;
        const OpResult j = fetchJournal(&added);
        res.journalNew = added;
        if (!j.ok) out.details.push_back(j.message);
        else if (added) out.message = plural(static_cast<std::size_t>(added), "ligne relev\xC3\xA9" "e", "lignes relev\xC3\xA9" "es") + " au journal des admins.";
    }
    if (caller.role == Role::Admin && !archive::hasSyncKey(folder_)) {
        const OpResult k = receiveSyncKey();
        res.syncKeyReceived = k.ok;
        out.details.push_back(k.message);
    }
    (void)detail::flushJournalQuiet(folder_, caller, now());
    if (caller.role == Role::Plus || caller.role == Role::Admin) (void)writeTokenWith(folder_, *caller.key, tokenName(caller), now(), false, nullptr, nullptr);
    if (outRefresh) *outRefresh = res;
    return out;
}

// ============================================================================================ gerer les admins
OpResult Workspace::serviceAdmins(AdminsView& view) const {
    view = AdminsView{};
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (caller.role != Role::Plus && caller.role != Role::Admin) return *detail::callerDenied(caller, "demandes");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    const online::Reply r = c->call("GET", "/admin/admins");
    if (r.status != 200 || !r.error.empty() || !r.json.isObject()) return OpResult::fail(c->explain(r, "lire les admins"));
    view.number = static_cast<int>(r.json.integer("numero"));
    const auto info = [](const Value& o) {
        AdminInfo i;
        i.admin.id = o.str("id");
        i.admin.key = o.str("cle");
        i.admin.name = o.str("nom");
        i.admin.pc = o.str("pc");
        i.admin.machine = o.str("poste");
        i.admin.since = static_cast<Seconds>(o.integer("depuis"));
        i.admin.profile = o.str("profil");
        i.admin.retired = static_cast<Seconds>(o.integer("retire"));
        if (const Value* d = o.find("droits"); d && d->isArray())
            for (const auto& x : d->items())
                if (x.isString()) i.admin.rights.push_back(x.asString());
        i.admin.rights = adm::normalizeRights(i.admin.rights);
        i.fingerprint = adm::fingerprint(i.admin.key);
        i.lastAction = static_cast<Seconds>(o.integer("derniere"));
        i.software = o.str("logiciel");
        i.version = o.str("version");
        i.seen = static_cast<Seconds>(o.integer("vu"));
        return i;
    };
    if (const Value* a = r.json.find("admins"); a && a->isArray())
        for (const auto& o : a->items())
            if (o.isObject()) view.admins.push_back(info(o));
    if (const Value* e = r.json.find("editeurs"); e && e->isArray())
        for (const auto& o : e->items())
            if (o.isObject()) view.editors.push_back(info(o));
    // Les empreintes gardees ne sont que dans la liste signee : celle de ce poste
    if (caller.admins)
        for (auto& i : view.admins)
            if (const adm::Admin* m = caller.admins->byKey(i.admin.key)) i.admin.kept = m->kept;
    return OpResult::done(plural(view.admins.size(), "admin", "admins") + " (liste n\xC2\xB0 " + std::to_string(view.number) + ").");
}

// 1.51.0 (docs/ADMINS.md, par. 4) : LES ADMINS INSCRITS PAR LEUR CODE, SIGNES D'OFFICE. Plus de demande, ni d'empreinte,
// ni de 6 chiffres : l'admin+ a tout decide en creant l'invitation (le nom, le profil, les droits, notes sur CE poste).
// Le service ne fait que dire quelle cle a servi quel code ; il ne peut ni changer les droits (ils viennent d'ici), ni
// faire signer une cle pour une invitation que ce poste n'a pas creee, ni pour une invitation deja servie par une autre.
OpResult Workspace::admitInvitedAdmins(std::vector<std::string>* namesOut) const {
    if (namesOut) namesOut->clear();
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    const online::Reply q = c->call("GET", "/admin/admins/inscrits");
    if (q.error.empty() && q.status == 404)
        return OpResult::done("Le service ne conna\xC3\xAEt pas encore l'inscription par code d'invitation (un service d'avant la 1.51) : d\xC3\xA9ploie-le.");
    if (q.status != 200 || !q.error.empty() || !q.json.isObject()) return OpResult::fail(c->explain(q, "lire les admins inscrits par leur code d'invitation"));
    struct Item {
        std::string              key, id, name, email, pc, machine, invitation, by, profile;
        Seconds                  joined{0};
        std::vector<std::string> rights;
    };
    auto invitations = adminInvitations();
    const auto all = profiles();
    std::vector<Item> todo;
    std::vector<std::string> skipped;
    const Value* list0 = q.json.find("inscrits");
    if (list0 && list0->isArray())
        for (const auto& o : list0->items()) {
            if (!o.isObject() || !licensing::isKeyHex(o.str("cle"))) continue;
            const Value* inv = o.find("invitation");
            const std::string invId = inv && inv->isObject() ? clip(inv->str("id"), 40) : std::string();
            const std::string shown = clip(trimmed(o.str("nom")), 60).empty() ? adm::fingerprint(o.str("cle")) : clip(trimmed(o.str("nom")), 60);
            const auto local = std::find_if(invitations.begin(), invitations.end(), [&](const AdminInvitation& i) { return !invId.empty() && i.id == invId; });
            if (local == invitations.end()) {
                skipped.push_back(shown + " : l'invitation " + (invId.empty() ? std::string("(sans identifiant)") : invId) +
                                  " n'a pas \xC3\xA9t\xC3\xA9 cr\xC3\xA9\xC3\xA9" "e sur ce poste : rien n'est sign\xC3\xA9 (retire-la : Administrateurs)");
                continue;
            }
            if (!local->admittedKey.empty() && local->admittedKey != o.str("cle")) {
                skipped.push_back(shown + " : l'invitation " + invId + " a d\xC3\xA9j\xC3\xA0 servi pour une autre cl\xC3\xA9 : rien n'est sign\xC3\xA9");
                continue;
            }
            const Seconds served = inv ? static_cast<Seconds>(inv->integer("servie")) : 0;
            if (local->expires && served > local->expires) {
                skipped.push_back(shown + " : le code de l'invitation " + invId + " a servi apr\xC3\xA8s ses 48 h : rien n'est sign\xC3\xA9");
                continue;
            }
            Item it;
            it.key = o.str("cle");
            crypto::PublicKey pk{};
            if (!keyFromHex(it.key, pk)) continue;
            it.id = licensing::keyIdOf(pk);
            it.name = clip(trimmed(local->name), 60);   // le nom tape par l'admin+ (l'invitation de ce poste)
            if (it.name.empty()) it.name = clip(trimmed(o.str("nom")), 60);
            if (it.name.empty()) it.name = "Admin " + it.id.substr(4, 6);
            it.email = clip(local->email, 120);
            it.pc = clip(o.str("pc"), 60);
            it.machine = o.str("poste").size() == 32 ? o.str("poste") : std::string();
            it.invitation = invId;
            it.by = inv ? clip(inv->str("par"), 40) : std::string();
            it.joined = static_cast<Seconds>(o.integer("inscrit"));
            it.profile = clip(local->profile, 60);
            if (local->rightsSet) {
                it.rights = adm::normalizeRights(local->rights);
            } else if (!it.profile.empty()) {   // une invitation d'avant la 1.51 : le profil par son nom, sinon << Admin LV1 >>
                auto p = std::find_if(all.begin(), all.end(), [&](const adm::Profile& x) { return x.name == it.profile; });
                if (p == all.end()) p = std::find_if(all.begin(), all.end(), [](const adm::Profile& x) { return x.name == "Admin LV1"; });
                if (p != all.end()) {
                    it.profile = p->name;
                    it.rights = adm::normalizeRights(p->rights);
                }
            }
            todo.push_back(std::move(it));
        }
    if (todo.empty()) {
        OpResult r = OpResult::done(skipped.empty() ? std::string("Aucun admin inscrit \xC3\xA0 signer.")
                                                    : plural(skipped.size(), "inscription n'est pas sign\xC3\xA9" "e", "inscriptions ne sont pas sign\xC3\xA9" "es") + ".");
        r.details = skipped;
        return r;
    }
    adm::AdminList list;
    if (!baseList(*c, caller, list, &why)) return OpResult::fail(why);
    std::vector<Item> added;
    for (auto& it : todo) {
        adm::Admin* a = nullptr;
        for (auto& x : list.admins)
            if (x.key == it.key) a = &x;
        if (a && a->active()) continue;   // deja dans la liste (la liste de ce poste est deja la bonne)
        if (!a) {
            if (list.admins.size() >= adm::kMaxAdmins) {
                skipped.push_back(it.name + " : la liste a d\xC3\xA9j\xC3\xA0 " + std::to_string(adm::kMaxAdmins) + " admins (retir\xC3\xA9s compris) : rien n'est sign\xC3\xA9");
                continue;
            }
            list.admins.emplace_back();
            a = &list.admins.back();
            a->key = it.key;
            a->id = it.id;
        }
        a->name = it.name;
        a->pc = it.pc;
        a->machine = it.machine;
        a->since = now();
        a->profile = it.profile;
        a->rights = it.rights;
        a->retired = 0;
        added.push_back(it);
    }
    if (added.empty()) {
        OpResult r = OpResult::done("Aucun admin inscrit \xC3\xA0 signer.");
        r.details = skipped;
        return r;
    }
    int number = 0;
    if (!publishList(*c, folder_, *caller.key, list, now(), number, &why)) return OpResult::fail(why);
    std::string who;
    for (std::size_t i = 0; i < added.size(); ++i) who += (i ? (i + 1 == added.size() ? " et " : ", ") : "") + added[i].name;
    OpResult r = OpResult::done(added.size() == 1 ? who + " est admin (code d'invitation ; liste des admins n\xC2\xB0 " + std::to_string(number) + ")."
                                                  : who + " sont admins (codes d'invitation ; liste des admins n\xC2\xB0 " + std::to_string(number) + ").");
    const bool sync = archive::hasSyncKey(folder_);
    const std::string inviter = caller.key->name().empty() ? std::string("l'admin+") : keyOwner(caller.key->name());
    for (const auto& it : added) {
        std::string line = it.name + " : " + (it.profile.empty() ? std::string("aucun profil") : "profil " + cite(it.profile)) + " (" +
                           std::to_string(it.rights.size()) + " droits sur 13), invitation " + it.invitation;
        if (sync) line += giveSyncKeyWith(*c, folder_, it.key, &why) ? std::string(", cl\xC3\xA9 de synchro d\xC3\xA9pos\xC3\xA9" "e") : ", ATTENTION : la cl\xC3\xA9 de synchro n'est pas d\xC3\xA9pos\xC3\xA9" "e (" + why + ")";
        r.details.push_back(line + ".");
        Value d = Value::object();
        d.set("nom", it.name);
        d.set("pc", it.pc);
        d.set("empreinte", adm::fingerprint(it.key));
        d.set("profil", it.profile);
        d.set("par", "code d'invitation");
        d.set("invitation", it.invitation);
        d.set("invite_par", inviter);
        if (!it.by.empty()) d.set("invite_par_cle", it.by);
        if (it.joined) d.set("inscrit", static_cast<std::int64_t>(it.joined));
        d.set("liste", number);
        Value after = Value::object();
        Value rl = Value::array();
        for (const auto& x : it.rights) rl.push(x);
        after.set("droits", std::move(rl));
        (void)detail::queueJournal(folder_, *caller.key, now(), "admin_ajouter", it.id,
                                   "a ajout\xC3\xA9 l'admin " + it.name + " (code d'invitation, invit\xC3\xA9 par " + inviter + ")", core::json::write(d, false), "{}",
                                   core::json::write(after, false), nullptr);
        // l'invitation a servi : une seule fois (la meme cle seulement, ensuite)
        for (auto& i : invitations)
            if (i.id == it.invitation) {
                i.admittedKey = it.key;
                i.admittedAt = now();
            }
        if (namesOut) namesOut->push_back(it.name);
    }
    (void)writeInvitationsFile(folder_, invitations, nullptr);
    if (!sync) r.details.push_back("Pas de cl\xC3\xA9 de synchro sur ce poste : cr\xC3\xA9" "e-la (Service en ligne), puis Donner la cl\xC3\xA9 de synchro \xC3\xA0 chacun.");
    for (const auto& x : skipped) r.details.push_back("PAS SIGN\xC3\x89 : " + x);
    (void)detail::flushJournalWith(folder_, *c);
    return r;
}

OpResult Workspace::giveSyncKey(const std::string& keyHex) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    if (!giveSyncKeyWith(*c, folder_, keyHex, &why)) return OpResult::fail(why);
    return OpResult::done("Cl\xC3\xA9 de synchro d\xC3\xA9pos\xC3\xA9" "e dans le trousseau de cet admin.");
}

OpResult Workspace::retireAdmin(const std::string& keyHex, bool revokeLicencesToo) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    const licensing::SigningKey& editor = *caller.key;
    const online::Options o = detail::optionsFor(caller, now());
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, editor, o, c, &why)) return OpResult::fail(why);
    adm::AdminList list;
    if (!baseList(*c, caller, list, &why)) return OpResult::fail(why);
    adm::Admin* a = nullptr;
    for (auto& x : list.admins)
        if (x.key == keyHex) a = &x;
    if (!a) return OpResult::fail("cette cl\xC3\xA9 n'est pas dans la liste des admins");
    if (!a->active()) return OpResult::fail(a->name + " est d\xC3\xA9j\xC3\xA0 retir\xC3\xA9");
    const std::string name = a->name, id = a->id;
    const Seconds t = now();
    OpResult r = OpResult::done("");
    const auto licences = licencesInForceBy(folder_, id, t);
    bool revocationsDone = false;
    std::vector<std::string> revokedIds;
    if (!revokeLicencesToo) {
        if (!keepDocuments(folder_, *a, {DocKind::License}, t, r.details, &why)) return OpResult::fail(why);
    } else if (!licences.empty()) {
        // Ses licences en vigueur : revoquees par la cle de l'editeur (une liste numero + 1, rangee, deposee plus bas).
        licensing::RevocationList rev;
        SignedBlock b;
        if (archivedBlock(folder_, kRevocationsRel, DocKind::Revocations, b)) (void)licensing::parseRevocations(b.document, rev, nullptr);
        for (const auto& d : licences) {
            licensing::Revocation x;
            x.licenseId = d.licenseId;
            x.reason = clip("admin retir\xC3\xA9 : " + name, 200);
            x.revokedAt = t;
            rev.revoked.push_back(x);
            revokedIds.push_back(d.licenseId);
        }
        std::string out;
        if (!signRevocations(editor, revocationsInput(rev, rev.number + 1), t, out, &why)) return OpResult::fail(why);
        if (!archive::archiveSigned(folder_, out, {editor.publicKey()}, nullptr, nullptr, &why)) return OpResult::fail(why);
        revocationsDone = true;
        r.details.push_back(plural(licences.size(), "licence sign\xC3\xA9" "e par " + name + " r\xC3\xA9voqu\xC3\xA9" "e", "licences sign\xC3\xA9" "es par " + name + " r\xC3\xA9voqu\xC3\xA9" "es") +
                            " (liste de r\xC3\xA9vocation n\xC2\xB0 " + std::to_string(rev.number + 1) + ").");
    }
    // Le registre et la liste de revocation en place, signes par lui : re-signes par la cle de l'editeur (numero + 1),
    // AVANT la liste (un Studio qui recoit la liste d'abord refuserait le registre d'un admin retire).
    bool registryDone = false;
    {
        SignedBlock b;
        licensing::Registry reg;
        if (archivedBlock(folder_, kRegistryRel, DocKind::Registry, b) && b.keyId == id && licensing::parseRegistry(b.document, reg, nullptr)) {
            reg.number += 1;
            reg.issuedAt = t;
            std::string out;
            if (!license_admin::signRegistry(editor, licensing::writeRegistry(reg), t, out, &why)) return OpResult::fail(why);
            if (!archive::archiveSigned(folder_, out, {editor.publicKey()}, nullptr, nullptr, &why)) return OpResult::fail(why);
            registryDone = true;
            r.details.push_back("Registre re-sign\xC3\xA9 par la cl\xC3\xA9 de l'\xC3\xA9" "diteur (n\xC2\xB0 " + std::to_string(reg.number) + ").");
        }
    }
    if (!revocationsDone) {
        SignedBlock b;
        licensing::RevocationList rev;
        if (archivedBlock(folder_, kRevocationsRel, DocKind::Revocations, b) && b.keyId == id && licensing::parseRevocations(b.document, rev, nullptr)) {
            std::string out;
            if (!signRevocations(editor, revocationsInput(rev, rev.number + 1), t, out, &why)) return OpResult::fail(why);
            if (!archive::archiveSigned(folder_, out, {editor.publicKey()}, nullptr, nullptr, &why)) return OpResult::fail(why);
            revocationsDone = true;
            r.details.push_back("Liste de r\xC3\xA9vocation re-sign\xC3\xA9" "e par la cl\xC3\xA9 de l'\xC3\xA9" "diteur (n\xC2\xB0 " + std::to_string(rev.number + 1) + ").");
        }
    }
    if (registryDone) {
        const OpResult p = fromResult(online::publishRegistry(folder_, editor, o));
        r.details.push_back(p.message);
        if (!p.ok) r.ok = false;
    }
    if (revocationsDone) {
        const OpResult p = fromResult(online::publishRevocations(folder_, editor, o));
        r.details.push_back(p.message);
        if (!p.ok) r.ok = false;
        for (const auto& d : licences)   // les PC lies prevenus (<< revoquee >>)
            if (std::find(revokedIds.begin(), revokedIds.end(), d.licenseId) != revokedIds.end() && !d.devices.empty()) {
                const OpResult q = fromResult(online::publishRevocation(folder_, editor, d.licenseId, d.devices, "admin retir\xC3\xA9 : " + name, o));
                if (!q.ok) {
                    r.details.push_back(q.message);
                    for (const auto& x : q.details) r.details.push_back(x);
                }
            }
    }
    const std::vector<std::string> before = a->rights;
    a->retired = t;
    a->rights.clear();
    int number = 0;
    if (!publishList(*c, folder_, editor, list, t, number, &why)) return OpResult::fail(why);
    // Les documents re-signes (le registre, la liste de revocation) partent aussi dans l'archive en ligne : sinon les
    // autres admins y trouveraient ceux de l'admin retire (refuses : ils ne les recevraient pas).
    if ((registryDone || revocationsDone) && archive::hasSyncKey(folder_)) {
        archive::SyncOptions so;
        so.now = t;
        so.tool = std::string(kAppName) + " " + kAppVersion;
        so.editorKeys = caller.editorKeys;
        so.admins = caller.admins;
        so.software = o.software;
        const archive::SyncReport sync = archive::synchronize(folder_, editor, so);
        r.details.push_back(sync.ok ? "Archive en ligne : les documents re-sign\xC3\xA9s envoy\xC3\xA9s."
                                    : "ATTENTION : l'archive en ligne n'est pas \xC3\xA0 jour (" + sync.why + ") : Synchroniser l'archive.");
    }
    r.message = name + " n'est plus dans la liste des admins (liste n\xC2\xB0 " + std::to_string(number) + ") : sa cl\xC3\xA9 ne peut plus rien faire sur le service.";
    r.details.push_back("Son trousseau est effac\xC3\xA9 par le service ; la cl\xC3\xA9 de synchro ne change pas (\xC2\xAB Changer la cl\xC3\xA9 de synchro \xC2\xBB si tu le souhaites).");
    Value d = Value::object();
    d.set("nom", name);
    d.set("licences_gardees", static_cast<int>(revokeLicencesToo ? 0 : licences.size()));
    d.set("licences_revoquees", static_cast<int>(revokeLicencesToo ? licences.size() : 0));
    Value b = Value::object();
    b.set("droits", Value(core::json::Value::array()));
    {
        Value arr = Value::array();
        for (const auto& x : before) arr.push(x);
        b.set("droits", std::move(arr));
    }
    (void)detail::queueJournal(folder_, editor, t, "admin_retirer", id, "a retir\xC3\xA9 l'admin " + name, core::json::write(d, false), core::json::write(b, false),
                               "{\"droits\":[]}", nullptr);
    (void)detail::flushJournalWith(folder_, *c);
    return r;
}

namespace {

// Changer les droits d'un admin de `list` (et noter les documents a garder) ; les lignes du journal a ecrire.
bool changeRights(const std::string& folder, adm::Admin& a, const std::vector<std::string>& rightsIn, const std::string& profile, Seconds now,
                  std::vector<std::string>& details, std::string* why) {
    const std::vector<std::string> before = adm::normalizeRights(a.rights);
    const std::vector<std::string> after = adm::normalizeRights(rightsIn);
    const auto lost = lostKinds(before, after);
    if (!lost.empty() && !keepDocuments(folder, a, lost, now, details, why)) return false;
    a.rights = after;
    a.profile = clip(profile, 60);
    return true;
}

std::string rightsJson(const std::vector<std::string>& v) {
    Value o = Value::object();
    Value arr = Value::array();
    for (const auto& x : adm::normalizeRights(v)) arr.push(x);
    o.set("droits", std::move(arr));
    return core::json::write(o, false);
}

} // namespace

OpResult Workspace::setAdminRights(const std::string& keyHex, const std::vector<std::string>& rights, const std::string& profile) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    for (const auto& x : rights)
        if (!adm::isRight(x)) return OpResult::fail("le droit " + cite(x) + " est inconnu");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    adm::AdminList list;
    if (!baseList(*c, caller, list, &why)) return OpResult::fail(why);
    adm::Admin* a = nullptr;
    for (auto& x : list.admins)
        if (x.key == keyHex) a = &x;
    if (!a) return OpResult::fail("cette cl\xC3\xA9 n'est pas dans la liste des admins");
    if (!a->active()) return OpResult::fail(a->name + " est retir\xC3\xA9 : ses droits ne changent plus");
    const std::vector<std::string> before = adm::normalizeRights(a->rights);
    OpResult r = OpResult::done("");
    if (!changeRights(folder_, *a, rights, profile, now(), r.details, &why)) return OpResult::fail(why);
    if (before == a->rights && a->profile == clip(profile, 60)) {
        r.message = "Les droits de " + a->name + " ne changent pas.";
        return r;
    }
    const std::string name = a->name, id = a->id;
    const std::vector<std::string> after = a->rights;
    int number = 0;
    if (!publishList(*c, folder_, *caller.key, list, now(), number, &why)) return OpResult::fail(why);
    r.message = "Droits de " + name + " chang\xC3\xA9s (liste des admins n\xC2\xB0 " + std::to_string(number) + ") : " + std::to_string(after.size()) + " sur 13.";
    Value d = Value::object();
    d.set("nom", name);
    d.set("profil", profile);
    (void)detail::queueJournal(folder_, *caller.key, now(), "admin_droits", id, "a chang\xC3\xA9 les droits de " + name, core::json::write(d, false), rightsJson(before),
                               rightsJson(after), nullptr);
    (void)detail::flushJournalWith(folder_, *c);
    return r;
}

// ============================================================================================ les profils
std::vector<adm::Profile> Workspace::profiles() const {
    std::vector<adm::Profile> out;
    std::string text;
    if (isFile(at(folder_, archive::kProfilesFile)) && readText(at(folder_, archive::kProfilesFile), text, nullptr) && adm::parseProfiles(text, out, nullptr))
        return out;
    return adm::defaultProfiles();
}

namespace {
bool writeProfilesFile(const std::string& folder, const std::vector<adm::Profile>& list, Seconds now, std::string* why) {
    Value v;
    if (!core::json::parse(adm::writeProfiles(list), v, nullptr) || !v.isObject()) return fail(why, "les profils ne s'\xC3\xA9" "crivent pas");
    return writeSetting(folder, archive::kProfilesFile, std::move(v), now, why);
}
} // namespace

OpResult Workspace::saveProfile(const adm::Profile& p) {
    if (role_ != Role::Plus) return OpResult::fail(kPlusOnly);
    const std::string name = trimmed(p.name);
    if (name.empty() || name.size() > 60) return OpResult::fail("le nom du profil (60 caract\xC3\xA8res au plus)");
    if (name == adm::kAllRightsProfile) return OpResult::fail("le profil " + cite(name) + " ne se modifie pas");
    for (const auto& x : p.rights)
        if (!adm::isRight(x)) return OpResult::fail("le droit " + cite(x) + " est inconnu");
    auto list = profiles();
    auto it = std::find_if(list.begin(), list.end(), [&](const adm::Profile& q) { return q.name == name; });
    const bool fresh = it == list.end();
    if (fresh && list.size() >= 50) return OpResult::fail("50 profils au plus");
    const std::vector<std::string> before = fresh ? std::vector<std::string>{} : it->rights;
    adm::Profile q;
    q.name = name;
    q.rights = adm::normalizeRights(p.rights);
    q.changed = now();
    if (fresh) list.push_back(q);
    else *it = q;
    std::string why;
    if (!writeProfilesFile(folder_, list, now(), &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Profil " + cite(name) + (fresh ? " cr\xC3\xA9\xC3\xA9" : " enregistr\xC3\xA9") + " (" + std::to_string(q.rights.size()) + " droits sur 13).");
    r.details.push_back("Un profil copie ses droits vers un admin (pas un lien) : les admins d\xC3\xA9j\xC3\xA0 l\xC3\xA0 gardent les leurs.");
    const OpResult j = journal("admin_profil", name, fresh ? "a cr\xC3\xA9\xC3\xA9 le profil " + name : "a modifi\xC3\xA9 le profil " + name, {}, rightsJson(before), rightsJson(q.rights));
    if (!j.ok) r.details.push_back("ATTENTION : journal des admins : " + j.message);
    return r;
}

OpResult Workspace::renameProfile(const std::string& fromIn, const std::string& toIn) {
    if (role_ != Role::Plus) return OpResult::fail(kPlusOnly);
    const std::string from = trimmed(fromIn), to = trimmed(toIn);
    if (from == adm::kAllRightsProfile) return OpResult::fail("le profil " + cite(from) + " ne se renomme pas");
    if (to.empty() || to.size() > 60 || to == adm::kAllRightsProfile) return OpResult::fail("ce nom de profil n'est pas permis");
    auto list = profiles();
    auto it = std::find_if(list.begin(), list.end(), [&](const adm::Profile& q) { return q.name == from; });
    if (it == list.end()) return OpResult::fail("le profil " + cite(from) + " n'existe pas");
    if (std::any_of(list.begin(), list.end(), [&](const adm::Profile& q) { return q.name == to; })) return OpResult::fail("un profil " + cite(to) + " existe d\xC3\xA9j\xC3\xA0");
    it->name = to;
    it->changed = now();
    std::string why;
    if (!writeProfilesFile(folder_, list, now(), &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Profil " + cite(from) + " renomm\xC3\xA9 " + cite(to) + ".");
    r.details.push_back("Les admins qui en viennent gardent le nom d'origine dans la liste sign\xC3\xA9" "e jusqu'\xC3\xA0 leur prochain changement.");
    if (const OpResult j = journal("admin_profil", to, "a renomm\xC3\xA9 le profil " + from + " en " + to); !j.ok) r.details.push_back("ATTENTION : journal des admins : " + j.message);
    return r;
}

OpResult Workspace::deleteProfile(const std::string& nameIn) {
    if (role_ != Role::Plus) return OpResult::fail(kPlusOnly);
    const std::string name = trimmed(nameIn);
    if (name == adm::kAllRightsProfile) return OpResult::fail("le profil " + cite(name) + " ne se supprime pas");
    auto list = profiles();
    const auto before = list.size();
    std::erase_if(list, [&](const adm::Profile& q) { return q.name == name; });
    if (list.size() == before) return OpResult::fail("le profil " + cite(name) + " n'existe pas");
    std::string why;
    if (!writeProfilesFile(folder_, list, now(), &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Profil " + cite(name) + " supprim\xC3\xA9 (les admins qui en viennent gardent leurs droits).");
    if (const OpResult j = journal("admin_profil", name, "a supprim\xC3\xA9 le profil " + name); !j.ok) r.details.push_back("ATTENTION : journal des admins : " + j.message);
    return r;
}

OpResult Workspace::copyProfileTo(const std::string& profileName, const std::vector<std::string>& adminKeys) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    if (adminKeys.empty()) return OpResult::fail("vers quel admin ?");
    const auto all = profiles();
    const auto it = std::find_if(all.begin(), all.end(), [&](const adm::Profile& p) { return p.name == profileName; });
    if (it == all.end()) return OpResult::fail("le profil " + cite(profileName) + " n'existe pas");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    adm::AdminList list;
    if (!baseList(*c, caller, list, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("");
    struct Change {
        std::string id, name;
        std::vector<std::string> before, after;
    };
    std::vector<Change> changes;
    for (const auto& k : adminKeys) {
        adm::Admin* a = nullptr;
        for (auto& x : list.admins)
            if (x.key == k) a = &x;
        if (!a) return OpResult::fail("une de ces cl\xC3\xA9s n'est pas dans la liste des admins");
        if (!a->active()) return OpResult::fail(a->name + " est retir\xC3\xA9 : ses droits ne changent plus");
        Change ch{a->id, a->name, adm::normalizeRights(a->rights), {}};
        if (!changeRights(folder_, *a, it->rights, it->name, now(), r.details, &why)) return OpResult::fail(why);
        ch.after = a->rights;
        changes.push_back(std::move(ch));
    }
    int number = 0;
    if (!publishList(*c, folder_, *caller.key, list, now(), number, &why)) return OpResult::fail(why);
    r.message = "Profil " + cite(it->name) + " copi\xC3\xA9 vers " + plural(changes.size(), "admin", "admins") + " (liste des admins n\xC2\xB0 " + std::to_string(number) + ").";
    for (const auto& ch : changes) {
        Value d = Value::object();
        d.set("nom", ch.name);
        d.set("profil", it->name);
        (void)detail::queueJournal(folder_, *caller.key, now(), "admin_droits", ch.id, "a copi\xC3\xA9 le profil " + it->name + " vers " + ch.name, core::json::write(d, false),
                                   rightsJson(ch.before), rightsJson(ch.after), nullptr);
    }
    (void)detail::flushJournalWith(folder_, *c);
    return r;
}

// ============================================================================================ le journal
OpResult Workspace::journal(const std::string& action, const std::string& object, const std::string& summary, const std::string& detailJson,
                            const std::string& beforeJson, const std::string& afterJson) const {
    const licensing::SigningKey* s = signer();
    if (!s) return OpResult::fail("ce poste n'a pas de cl\xC3\xA9 qui signe (ni l'admin+, ni un admin de la liste) : rien n'est \xC3\xA9" "crit au journal");
    std::string why;
    if (!detail::queueJournal(folder_, *s, now(), action, object, summary, detailJson, beforeJson, afterJson, &why)) return OpResult::fail(why);
    return OpResult::done("Journal des admins : " + summary + " (envoy\xC3\xA9 au prochain appel au service).");
}

int Workspace::journalWaiting() const {
    std::lock_guard<std::mutex> lock(outboxMutex());
    std::string text;
    if (!isFile(at(folder_, kOutboxFile)) || !readText(at(folder_, kOutboxFile), text, nullptr)) return 0;
    return static_cast<int>(linesOf(text).size());
}

OpResult Workspace::flushJournal() const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (caller.role != Role::Plus && caller.role != Role::Admin) return *detail::callerDenied(caller, "demandes");
    if (journalWaiting() == 0) return OpResult::done("Journal des admins : rien \xC3\xA0 envoyer.");
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    why.clear();
    const int sent = detail::flushJournalWith(folder_, *c, &why);
    const int left = journalWaiting();
    OpResult r = OpResult::done("Journal des admins : " + plural(static_cast<std::size_t>(sent), "ligne envoy\xC3\xA9" "e", "lignes envoy\xC3\xA9" "es") + ".");
    if (left) {
        r.ok = why.empty();
        r.details.push_back(plural(static_cast<std::size_t>(left), "ligne attend", "lignes attendent") + " (" + (why.empty() ? std::string("une autre cl\xC3\xA9") : why) + ").");
    }
    return r;
}

OpResult Workspace::fetchJournal(int* added) const {
    if (added) *added = 0;
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, {})) return *deny;
    std::unique_ptr<online::Client> c;
    if (!online::openClient(folder_, *caller.key, detail::optionsFor(caller, now()), c, &why)) return OpResult::fail(why);
    JournalState st = readJournalState(folder_);
    // La cle du service (ses lignes : signature_refusee, autre_pc, demande_admin...) : celle que nomme son certificat.
    {
        const online::Reply r = c->call("GET", "/admin/cle-service");
        licensing::service::ServiceCert cert;
        if (r.status == 200 && r.error.empty() && r.json.isObject() && r.json.find("certificat") && r.json.at("certificat").isString() &&
            licensing::service::verifyCert(r.json.at("certificat").asString(), caller.editorKeys, 0, cert, nullptr) && cert.key == r.json.str("cle"))
            st.serviceKey = cert.key;
    }
    int count = 0;
    OpResult res = OpResult::done("");
    for (int guard = 0; guard < 1000; ++guard) {
        const online::Reply r = c->call("GET", "/admin/journal?depuis=" + std::to_string(st.last));
        if (r.status != 200 || !r.error.empty() || !r.json.isObject()) {
            if (count) (void)writeJournalState(folder_, st, nullptr);
            return OpResult::fail(c->explain(r, "relever le journal des admins"));
        }
        const Value* lines = r.json.find("lignes");
        if (!lines || !lines->isArray() || lines->items().empty()) break;
        std::string batch;
        for (const auto& x : lines->items()) {
            if (!x.isString()) continue;
            adm::JournalLine line;
            if (!adm::parseJournalLine(x.asString(), line, &why)) {
                st.problems.push_back("au relev\xC3\xA9 : " + why);
                continue;
            }
            if (line.number <= st.last) continue;
            if (line.number != st.last + 1)
                st.problems.push_back("cha\xC3\xAEne rompue : apr\xC3\xA8s la ligne n\xC2\xB0 " + std::to_string(st.last) + ", le service rend la n\xC2\xB0 " +
                                      std::to_string(line.number) + " (des lignes manquent)");
            else if (line.previous != st.hash)
                st.problems.push_back("cha\xC3\xAEne rompue avant la ligne n\xC2\xB0 " + std::to_string(line.number) +
                                      " : elle ne suit pas la ligne relev\xC3\xA9" "e avant (une ligne modifi\xC3\xA9" "e ou remplac\xC3\xA9" "e sur le service)");
            batch += line.text + "\n";
            st.last = line.number;
            st.hash = line.hash;
            ++count;
        }
        if (!batch.empty()) {
            std::error_code ec;
            fs::create_directories(P(folder_), ec);
            std::ofstream out(P(at(folder_, kJournalFile)), std::ios::binary | std::ios::app);
            out << batch;
            if (!out) return OpResult::fail("impossible d'\xC3\xA9" "crire " + at(folder_, kJournalFile));
        }
        if (lines->items().size() < kJournalPage || static_cast<std::int64_t>(st.last) >= r.json.integer("dernier")) break;
    }
    if (st.problems.size() > 200) st.problems.erase(st.problems.begin(), st.problems.begin() + static_cast<std::ptrdiff_t>(st.problems.size() - 200));
    if (!writeJournalState(folder_, st, &why)) return OpResult::fail(why);
    if (added) *added = count;
    res.message = count ? "Journal des admins : " + plural(static_cast<std::size_t>(count), "ligne relev\xC3\xA9" "e", "lignes relev\xC3\xA9" "es") + " (jusqu'\xC3\xA0 la n\xC2\xB0 " +
                              std::to_string(st.last) + ")."
                        : std::string("Journal des admins \xC3\xA0 jour.");
    const JournalView v = journalLines();
    for (const auto& p : v.problems) res.details.push_back("ATTENTION : " + p);
    (void)detail::flushJournalWith(folder_, *c);
    return res;
}

JournalView Workspace::journalLines() const {
    JournalView view;
    const JournalState st = readJournalState(folder_);
    view.problems = st.problems;
    std::string text;
    if (isFile(at(folder_, kJournalFile)) && !readText(at(folder_, kJournalFile), text, nullptr))
        view.problems.push_back("la copie du journal de ce poste ne se lit pas");
    // Qui : les cles de l'editeur (l'admin+), celle du service, les admins de toutes les listes vues.
    std::set<std::string> editors;
    for (const auto& k : editorKeys_) editors.insert(crypto::hex(k));
    const auto admins = seenAdminKeys(folder_, editorKeys_);
    const Criticality crit = criticalityOf(folder_);
    const auto notes = lineNotesOf(folder_);
    std::uint64_t prevNumber = 0;
    std::string prevHash;
    for (const auto& raw : linesOf(text)) {
        JournalRow row;
        std::string why;
        if (!adm::parseJournalLine(raw, row.line, &why)) {
            view.problems.push_back("la copie de ce poste : une ligne illisible apr\xC3\xA8s la n\xC2\xB0 " + std::to_string(prevNumber) + " (modifi\xC3\xA9" "e ?)");
            prevHash.clear();
            continue;
        }
        const auto& L = row.line;
        if (prevNumber && L.number != prevNumber + 1)
            view.problems.push_back("cha\xC3\xAEne rompue : la copie de ce poste passe de la ligne n\xC2\xB0 " + std::to_string(prevNumber) + " \xC3\xA0 la n\xC2\xB0 " +
                                    std::to_string(L.number));
        else if ((prevNumber || L.number == 1) && L.previous != prevHash)
            view.problems.push_back("cha\xC3\xAEne rompue avant la ligne n\xC2\xB0 " + std::to_string(L.number) +
                                    " : la ligne d'avant a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9" "e (ou il en manque une)");
        prevNumber = L.number;
        prevHash = L.hash;
        const bool sigOk = adm::verifyJournalSignature(L);
        if (editors.count(L.key)) row.who = "admin+";
        else if (!st.serviceKey.empty() && L.key == st.serviceKey) row.who = "le service";
        else if (const auto it = admins.find(L.key); it != admins.end()) row.who = it->second;
        row.verified = sigOk && !row.who.empty();
        if (!sigOk) row.problem = "signature fausse : l'entr\xC3\xA9" "e a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9" "e";
        else if (row.who.empty()) row.problem = "sign\xC3\xA9" "e par une cl\xC3\xA9 inconnue (" + L.key.substr(0, 16) + "\xE2\x80\xA6)";
        if (!row.problem.empty()) view.problems.push_back("ligne n\xC2\xB0 " + std::to_string(L.number) + " : " + row.problem);
        if (const auto c = crit.actions.find(L.parsed.action); c != crit.actions.end()) {
            row.level = c->second.level;
            row.notify = c->second.notify;
        }
        if (const auto n = notes.find(L.number); n != notes.end()) {
            row.level = n->second.level;
            row.overridden = true;
            row.overrideBy = n->second.who;
            row.overrideAt = n->second.when;
        }
        view.rows.push_back(std::move(row));
    }
    view.last = prevNumber;
    view.lastHash = prevHash;
    if (st.last && (st.last != prevNumber || st.hash != prevHash))
        view.problems.push_back("la copie du journal de ce poste a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9" "e : sa derni\xC3\xA8re ligne n'est plus celle relev\xC3\xA9" "e (n\xC2\xB0 " +
                                std::to_string(st.last) + ")");
    return view;
}

Criticality Workspace::criticality() const { return criticalityOf(folder_); }

namespace {
Value criticalityJson(const Criticality& c) {
    Value root = Value::object();
    root.set("format", 1);
    Value types = Value::object();
    for (const auto& [id, s] : c.actions) {
        Value o = Value::object();
        o.set("niveau", adm::levelKey(s.level));
        o.set("prevenir", s.notify);
        types.set(id, std::move(o));
    }
    root.set("types", std::move(types));
    root.set("courriel_critique", c.emailCritical);
    return root;
}
} // namespace

OpResult Workspace::setCriticality(const std::string& action, adm::Level level, bool notify) {
    if (role_ != Role::Plus) return OpResult::fail(kPlusOnly);
    if (!adm::actionType(action)) return OpResult::fail("type d'action inconnu : " + action);
    Criticality c = criticality();
    const ActionSetting before = c.actions[action];
    c.actions[action] = ActionSetting{level, notify};
    std::string why;
    if (!writeSetting(folder_, archive::kCriticalityFile, criticalityJson(c), now(), &why)) return OpResult::fail(why);
    const adm::ActionType* t = adm::actionType(action);
    OpResult r = OpResult::done("Criticit\xC3\xA9 de " + cite(t->label) + " : " + adm::levelLabel(level) + (notify ? ", te pr\xC3\xA9venir tout de suite." : "."));
    Value b = Value::object(), a = Value::object();
    b.set("niveau", adm::levelKey(before.level));
    b.set("prevenir", before.notify);
    a.set("niveau", adm::levelKey(level));
    a.set("prevenir", notify);
    if (const OpResult j = journal("criticite", action, "a chang\xC3\xA9 la criticit\xC3\xA9 de " + std::string(t->label), {}, core::json::write(b, false), core::json::write(a, false));
        !j.ok)
        r.details.push_back("ATTENTION : journal des admins : " + j.message);
    return r;
}

OpResult Workspace::setEmailCritical(bool on) {
    if (role_ != Role::Plus) return OpResult::fail(kPlusOnly);
    Criticality c = criticality();
    c.emailCritical = on;
    std::string why;
    if (!writeSetting(folder_, archive::kCriticalityFile, criticalityJson(c), now(), &why)) return OpResult::fail(why);
    OpResult r = OpResult::done(on ? "Les lignes critiques te seront aussi envoy\xC3\xA9" "es par courriel." : "Plus de courriel pour les lignes critiques.");
    if (const OpResult j = journal("criticite", "courriel", on ? "a demand\xC3\xA9 un courriel pour chaque ligne critique" : "a arr\xC3\xAAt\xC3\xA9 les courriels des lignes critiques");
        !j.ok)
        r.details.push_back("ATTENTION : journal des admins : " + j.message);
    return r;
}

OpResult Workspace::setLineLevel(std::uint64_t number, adm::Level level, bool reset) {
    if (role_ != Role::Plus) return OpResult::fail(kPlusOnly);
    if (number == 0) return OpResult::fail("quelle ligne ?");
    Value v = readSetting(folder_, archive::kJournalNotesFile);
    v.erase("maj");
    v.set("format", 1);
    Value notes = v.has("notes") && v.at("notes").isObject() ? v.at("notes") : Value::object();
    const std::string k = std::to_string(number);
    if (reset) {
        notes.erase(k);
    } else {
        Value o = Value::object();
        o.set("criticite", adm::levelKey(level));
        o.set("qui", key_.name.empty() ? std::string("admin+") : "admin+ (" + clip(key_.name, 60) + ")");
        o.set("quand", static_cast<std::int64_t>(now()));
        notes.set(k, std::move(o));
    }
    v.set("notes", std::move(notes));
    std::string why;
    if (!writeSetting(folder_, archive::kJournalNotesFile, std::move(v), now(), &why)) return OpResult::fail(why);
    OpResult r = OpResult::done(reset ? "La ligne n\xC2\xB0 " + k + " reprend la criticit\xC3\xA9 de son type."
                                      : "La ligne n\xC2\xB0 " + k + " passe en " + adm::levelLabel(level) + " (la ligne sign\xC3\xA9" "e ne change pas).");
    if (const OpResult j = journal("criticite", "ligne " + k, reset ? "a rendu \xC3\xA0 la ligne n\xC2\xB0 " + k + " la criticit\xC3\xA9 de son type"
                                                                    : "a mis la ligne n\xC2\xB0 " + k + " en " + adm::levelLabel(level));
        !j.ok)
        r.details.push_back("ATTENTION : journal des admins : " + j.message);
    return r;
}

std::vector<JournalRow> Workspace::newAlerts() {
    std::vector<JournalRow> out;
    if (role_ != Role::Plus) return out;
    JournalView v = journalLines();
    for (auto& row : v.rows)
        if (row.line.number > alertedUpTo_ && row.notify) out.push_back(std::move(row));
    alertedUpTo_ = std::max(alertedUpTo_, v.last);
    return out;
}

// ============================================================================================ les droits << l'Admin >>
OpResult Workspace::revealCode(const std::string& licenseId, std::string& code) const {
    code.clear();
    if (const auto deny = denied("codes")) return *deny;
    std::string why;
    if (!online::readCode(folder_, licenseId, code, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done("Code d'activation de la licence " + licenseId + " : montr\xC3\xA9 une fois, jamais copi\xC3\xA9 ailleurs.");
    std::string customer;
    if (const LicenceEntry* l = licence(licenseId)) customer = l->licence.customer;
    Value d = Value::object();
    if (!customer.empty()) d.set("client", customer);
    if (const OpResult j = journal("code_revoir", licenseId, "a revu le code d'activation de la licence " + licenseId, core::json::write(d, false)); !j.ok)
        r.details.push_back("ATTENTION : journal des admins : " + j.message);
    return r;
}

OpResult Workspace::sendClientMail(const std::string& to, const std::string& toName, const std::string& subject, const std::string& body) const {
    std::string why;
    detail::Caller caller;
    if (!detail::loadCaller(folder_, caller, &why)) return OpResult::fail(why);
    if (const auto deny = detail::callerDenied(caller, "courriels")) return *deny;
    mail::Message m;
    m.to = to;
    m.toName = toName;
    m.subject = subject;
    m.body = body;
    const mail::SendResult s = mail::send(folder_, m);
    if (!s.ok) return OpResult::fail(s.message);
    Value d = Value::object();
    d.set("objet", clip(subject, 200));
    (void)detail::queueJournal(folder_, *caller.key, now(), "courriel", clip(to, 120), "a \xC3\xA9" "crit \xC3\xA0 " + (toName.empty() ? to : toName), core::json::write(d, false), {},
                               {}, nullptr);
    (void)detail::flushJournalQuiet(folder_, caller, now());
    return OpResult::done(s.message);
}

// ============================================================================================ le jeton d'admin
OpResult Workspace::writeAdminToken(bool force) const {
    const licensing::SigningKey* s = signer();
    if (!s) return OpResult::fail("ce poste n'est ni l'admin+ ni un admin de la liste : pas de jeton d'admin");
    std::string name = "admin+";
    if (role_ == Role::Admin)
        if (const adm::Admin* m = me()) name = m->name;
    std::string why;
    bool written = false;
    if (!writeTokenWith(folder_, *s, name, now(), force, &written, &why)) return OpResult::fail(why);
    OpResult r = OpResult::done(written ? "Jeton d'admin \xC3\xA9" "crit (30 jours) : " + at(folder_, kAdminTokenFile) : std::string("Jeton d'admin \xC3\xA0 jour."));
    r.output = at(folder_, kAdminTokenFile);
    return r;
}

// ============================================================================================ les invitations (l'admin+)
namespace {
// Ecrire les invitations (les 100 plus recentes) : << droits >> (1.51.0) et << admis >> ne sont lus que par la 1.51 ; une
// 1.50 relit le reste et ignore ces champs.
bool writeInvitationsFile(const std::string& folder, std::vector<AdminInvitation> list, std::string* why) {
    if (list.size() > kMaxInvitations) list.resize(kMaxInvitations);
    Value v = Value::object();
    v.set("format", kInvitationsFormat);
    Value a = Value::array();
    for (const auto& x : list) {
        Value o = Value::object();
        o.set("nom", x.name);
        o.set("courriel", x.email);
        o.set("profil", x.profile);
        o.set("par", x.via);
        o.set("le", static_cast<std::int64_t>(x.at));
        if (!x.id.empty()) o.set("id", x.id);
        if (x.expires) o.set("expire", static_cast<std::int64_t>(x.expires));
        if (x.rightsSet) {
            Value r = Value::array();
            for (const auto& d : x.rights) r.push(d);
            o.set("droits", std::move(r));
        }
        if (!x.admittedKey.empty()) {
            o.set("admis", x.admittedKey);
            o.set("admis_le", static_cast<std::int64_t>(x.admittedAt));
        }
        a.push(std::move(o));
    }
    v.set("invitations", std::move(a));
    return writeText(at(folder, kInvitationsFile), core::json::write(v, true) + "\n", why);
}
} // namespace

std::vector<AdminInvitation> Workspace::adminInvitations() const {
    std::vector<AdminInvitation> out;
    std::string text;
    Value v;
    if (!isFile(at(folder_, kInvitationsFile)) || !readText(at(folder_, kInvitationsFile), text, nullptr) || !core::json::parse(text, v, nullptr) ||
        !v.isObject() || v.str("format") != kInvitationsFormat)
        return out;
    if (const Value* a = v.find("invitations"); a && a->isArray())
        for (const auto& o : a->items())
            if (o.isObject()) {
                AdminInvitation i{clip(o.str("nom"), 60), clip(o.str("courriel"), 120), clip(o.str("profil"), 60), clip(o.str("par"), 20),
                                  static_cast<Seconds>(o.integer("le")), clip(o.str("id"), 40), static_cast<Seconds>(o.integer("expire"))};
                if (const Value* d = o.find("droits"); d && d->isArray()) {   // 1.51.0 : les droits notes a la creation
                    i.rightsSet = true;
                    for (const auto& x : d->items())
                        if (x.isString()) i.rights.push_back(x.asString());
                    i.rights = adm::normalizeRights(i.rights);
                }
                if (licensing::isKeyHex(o.str("admis"))) {
                    i.admittedKey = o.str("admis");
                    i.admittedAt = static_cast<Seconds>(o.integer("admis_le"));
                }
                out.push_back(std::move(i));
            }
    std::sort(out.begin(), out.end(), [](const AdminInvitation& a, const AdminInvitation& b) { return a.at > b.at; });
    return out;
}

OpResult Workspace::noteAdminInvitation(const AdminInvitation& in) const {
    if (role_ != Role::Plus) return OpResult::fail("seul l'admin+ invite un admin");
    if (trimmed(in.name).empty() && trimmed(in.email).empty()) return OpResult::fail("une invitation a un nom ou un courriel");
    auto list = adminInvitations();
    AdminInvitation i = in;
    i.name = clip(trimmed(i.name), 60);
    i.email = clip(trimmed(i.email), 120);
    i.profile = clip(trimmed(i.profile), 60);
    if (i.at == 0) i.at = now();
    list.insert(list.begin(), i);
    std::string why;
    if (!writeInvitationsFile(folder_, std::move(list), &why)) return OpResult::fail(why);
    return OpResult::done("Invitation not\xC3\xA9" "e : " + (i.name.empty() ? i.email : i.name) + ".");
}

AccountInfo Workspace::account() const {
    AccountInfo a;
    a.role = role_;
    a.pc = thisPc();
    a.machine = thisMachine();
    a.listNumber = adminList_ ? adminList_->number : 0;
    if (role_ == Role::Plus && signing_) {
        a.name = key_.name;
        a.keyHex = signing_->publicKeyHex();
        a.keyId = signing_->keyId();
        a.testKey = key_.test;
        a.rights = adm::allRights();
    } else if (adminKey_.present) {
        a.name = adminKey_.name;
        a.keyHex = adminKey_.keyHex;
        a.keyId = adminKey_.id;
        if (const adm::Admin* m = me()) {
            a.name = m->name;
            a.since = m->since;
            a.retired = m->retired;
            a.profile = m->profile;
            a.listPc = m->pc;
            if (m->active()) a.rights = adm::normalizeRights(m->rights);
        }
        // les listes vues ici (la premiere qui porte sa cle ; l'histoire de ses droits)
        std::string text;
        std::map<int, AccountStep> steps;
        if (isFile(at(folder_, kAdminsSeenFile)) && readText(at(folder_, kAdminsSeenFile), text, nullptr))
            for (const auto& line : linesOf(text)) {
                Value v;
                adm::AdminList l;
                if (!core::json::parse(line, v, nullptr) || !v.isObject() || !readListBlock(v.str("bloc"), editorKeys_, l, nullptr, nullptr)) continue;
                AccountStep st;
                st.number = l.number;
                st.issued = l.issued;
                if (const adm::Admin* m = l.byKey(a.keyHex)) {
                    st.listed = true;
                    st.active = m->active();
                    st.profile = m->profile;
                    st.rights = adm::normalizeRights(m->rights);
                    if (a.addedInList == 0 || l.number < a.addedInList) a.addedInList = l.number;
                }
                steps[l.number] = std::move(st);
            }
        if (adminList_ && !steps.count(adminList_->number)) {   // la liste en vigueur, si elle n'est pas notee
            AccountStep st;
            st.number = adminList_->number;
            st.issued = adminList_->issued;
            if (const adm::Admin* m = adminList_->byKey(a.keyHex)) {
                st.listed = true;
                st.active = m->active();
                st.profile = m->profile;
                st.rights = adm::normalizeRights(m->rights);
                if (a.addedInList == 0 || st.number < a.addedInList) a.addedInList = st.number;
            }
            steps[st.number] = std::move(st);
        }
        for (auto& [n, st] : steps) a.steps.push_back(std::move(st));
        // 1.51.0 : son inscription par code d'invitation (le nom et le courriel de l'invitation) ; sinon la demande d'avant
        Value note;
        if (isFile(at(folder_, kJoinFile)) && readText(at(folder_, kJoinFile), text, nullptr) && core::json::parse(text, note, nullptr) && note.isObject()) {
            a.email = clip(note.str("courriel"), 120);
            a.joinedAt = static_cast<Seconds>(note.integer("inscrit"));
            a.invitationId = clip(note.str("invitation"), 40);
            if (a.name.empty() || a.name == "Admin XPG") a.name = clip(note.str("nom"), 60);
            if (a.profile.empty()) a.profile = clip(note.str("profil"), 60);
        } else if (isFile(at(folder_, kAdminRequestFile)) && readText(at(folder_, kAdminRequestFile), text, nullptr) && core::json::parse(text, note, nullptr) &&
                   note.isObject()) {
            a.email = clip(note.str("courriel"), 120);
            if (a.name.empty()) a.name = clip(note.str("nom"), 60);
        }
    }
    if (!a.keyHex.empty()) a.fingerprint = adm::fingerprint(a.keyHex);
    return a;
}

} // namespace license_admin::gui
