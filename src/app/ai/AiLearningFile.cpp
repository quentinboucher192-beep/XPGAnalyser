// =============================================================================
//  app/ai/AiLearningFile.cpp - voir AiLearningFile.hpp
// -----------------------------------------------------------------------------
//  LE FICHIER (.xia) : du JSON, en UTF-8 -
//    { "format": "xia", "version": 1, "logiciel": "1.17.0", "date": "...", "pc": "...",
//      "connaissances": [ <chaque fichier connaissances/<id>.json, tel quel> ],
//      "etat": { "integreesInactives": [ <id>... ] },                (states)
//      "reglages": { "fournisseur", "service", "adresse", "modele", (settings - JAMAIS la cle)
//                    "integreeDabord", "avertirReponses", "appliquerMalgreErreurs" },
//      "conversations": [ <conversation> ] }                         (conversations)
//  1.21.0 : "revision": 2 et "modelesFichiers": <nombre> - les MODELES DE FICHIERS (AiFileModels.hpp) sont des
//  connaissances de "connaissances" (categorie "modele-fichier", leur modele dans "donnees") ; la version reste 1 pour
//  qu'une version precedente lise encore le fichier (elle refuse une version plus grande), et un .xia d'avant se lit
//  tel quel.
//
//  LA FUSION : une connaissance de meme identifiant est gardee (ou remplacee si
//  `replaceSame`) ; un identifiant suspect (un chemin, des caracteres hors
//  [A-Za-z0-9._-]) est ignore ; rien ne s'ecrit hors du dossier IA. Les reglages
//  du fichier ne s'appliquent que si l'Assistant n'est encore relie a aucune IA
//  (l'IA integree ou la Demonstration) : un reglage fait a la main reste.
//  SECURITE : le fournisseur, le service et l'adresse d'un fichier ne s'importent
//  JAMAIS (un .xia ne choisit pas a qui partent les demandes et la cle) - l'apercu
//  dit qu'ils sont ignores. Une conversation importee perd ses chemins (l'original
//  sur l'autre PC, la copie et le texte extrait : ses pieces ne voyagent pas) ; une
//  connaissance importee est marquee "importee" (le mode Direct ne s'applique pas
//  aux reponses qui l'ont lue).
// =============================================================================
#include "AiLearningFile.hpp"

#include "AiKnowledge.hpp"
#include "AiMemory.hpp"
#include "AiMemoryJson.hpp"
#include "AiPaths.hpp"
#include "AiStore.hpp"
#include "../Dossiers.hpp"
#include "../../core/Version.hpp"
#include "../../hmi/HmiNetInfo.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <system_error>
#include <vector>

// Apres les autres en-tetes : les macros de windows.h ne touchent que ce fichier.
#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <aclapi.h>
#else
#    include <sys/stat.h>
#    include <unistd.h>
#endif

namespace app::ai::learning {

namespace fs = std::filesystem;
using memory::Json;

namespace {

constexpr int         kFormatVersion = 1;
constexpr std::size_t kMaxBytes = 64ull * 1024ull * 1024ull;   // un fichier plus gros : refuse

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

std::string pluralize(int n, std::string_view one, std::string_view many) {
    return std::to_string(n) + " " + std::string(n > 1 ? many : one);
}

// Un identifiant de connaissance sur : il devient un nom de fichier du dossier connaissances/.
bool safeId(const std::string& id) {
    if (id.empty() || id.size() > 96 || id == "." || id == "..") return false;
    if (id.find("..") != std::string::npos) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
}

// Lire et verifier le fichier (taille, JSON, format, version).
bool readFile(const std::string& path, Json& out, std::string* why) {
    std::error_code ec;
    const fs::path p = dossiers::cheminDe(path);
    if (!fs::is_regular_file(p, ec)) return fail(why, "fichier introuvable : " + path);
    const auto size = fs::file_size(p, ec);
    if (ec) return fail(why, "fichier illisible : " + path);
    if (size > kMaxBytes) return fail(why, "fichier trop gros (" + std::to_string(size / (1024 * 1024)) + " Mo ; 64 Mo au plus)");
    std::string text;
    if (!memory::readText(path, text)) return fail(why, "fichier illisible : " + path);
    std::string err;
    if (!memory::parseJson(text, out, &err) || !out.isObject())
        return fail(why, "ce n'est pas un fichier d'apprentissage lisible" + (err.empty() ? std::string{} : " (" + err + ")"));
    if (out.str("format") != "xia") return fail(why, "ce n'est pas un fichier d'apprentissage (.xia)");
    const int version = static_cast<int>(out.num("version", 0));
    if (version < 1) return fail(why, "fichier d'apprentissage sans version");
    if (version > kFormatVersion)
        return fail(why, "fichier fait par une version plus r\xC3\xA9" "cente du logiciel (" + out.str("logiciel", "?") + ") : mets ce PC \xC3\xA0 jour");
    return true;
}

// json::Value (AiStore) <-> memory::Json : par le texte (les deux lisent le meme JSON).
Json toMemory(const json::Value& v) {
    Json out;
    (void)memory::parseJson(json::write(v, false), out);
    return out;
}
json::Value toStore(const Json& j) {
    json::Value out;
    (void)json::parse(memory::writeJson(j, false), out);
    return out;
}

std::string fromLine(const Json& file) {
    std::string from = file.str("pc", "un autre PC");
    if (const std::string d = file.str("date"); !d.empty()) from += ", " + shortDate(d);
    return from;
}

// Les reglages qu'un fichier (ou l'installation) donne a l'Assistant, en clair pour l'utilisateur.
std::string providerText(const Settings& s) {
    if (s.provider == "integree") return "l'IA int\xC3\xA9gr\xC3\xA9" "e";
    if (s.provider == "demo") return "la D\xC3\xA9monstration";
    std::string name = s.provider == "anthropic" ? std::string("Anthropic (Claude)")
                     : s.preset == "ollama"      ? std::string("Ollama")
                     : s.preset == "lmstudio"    ? std::string("LM Studio")
                     : s.preset.empty() || s.preset == "autre" ? std::string("un service compatible OpenAI")
                                                 : s.preset;
    if (!s.model.empty()) name += " \xE2\x80\x93 " + s.model;
    return name;
}

// Un texte venu d'un fichier, montre dans l'apercu : une ligne, borne.
std::string shown(std::string_view s) {
    std::string out;
    for (const char c : s) out += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
    if (out.size() > 160) out = out.substr(0, 160) + "\xE2\x80\xA6";
    return out;
}

// Une conversation importee : ses pieces jointes ne voyagent pas dans un .xia - le chemin d'origine (un chemin de l'autre
// PC), la copie et le texte extrait sont oublies (ils designeraient des fichiers de CE poste) ; la piece est dite manquante.
Json withoutPaths(Json c) {
    if (Json* msgs = c.get("messages"); msgs && msgs->isArray())
        for (auto& m : msgs->items)
            if (Json* pieces = m.isObject() ? m.get("pieces") : nullptr; pieces && pieces->isArray())
                for (auto& a : pieces->items) {
                    if (!a.isObject()) continue;
                    for (const char* key : {"cheminOrigine", "source", "fichier"})
                        while (a.erase(key)) {}   // un champ en double aussi
                    if (Json* an = a.get("analyses"); an && an->isArray())
                        for (auto& r : an->items)
                            if (r.isObject())
                                while (r.erase("texte")) {}
                }
    return c;
}

// Le dossier commun ou l'installateur depose installation.json et le .xia.
std::string installationDir() {
    if (const char* forced = std::getenv("XPG_IA_INSTALLATION"); forced && *forced) return forced;   // les essais (Linux)
#if defined(_WIN32)
    const auto c = dossiers::contexteSysteme(fs::path{});
    if (c.programData.empty()) return {};
    return c.programData + "\\XPGAnalyser\\IA";
#else
    return {};
#endif
}

// Un fichier du dossier commun n'est cru que s'il appartient a l'administrateur : Windows, BUILTIN\Administrateurs ou
// SYSTEM (ce que l'installateur ecrit) - un utilisateur ordinaire peut creer des fichiers sous ProgramData - ou au compte
// qui lit (une installation pour lui seul) ; ailleurs, root (ou, pour les essais seulement - le dossier force par
// XPG_IA_INSTALLATION -, l'utilisateur lui-meme).
bool ownedByAdministrator(const std::string& path) {
#if defined(_WIN32)
    std::wstring w = dossiers::cheminDe(path).wstring();
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(w.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &sd) != ERROR_SUCCESS)
        return false;
    bool ok = owner && IsValidSid(owner) && (IsWellKnownSid(owner, WinBuiltinAdministratorsSid) || IsWellKnownSid(owner, WinLocalSystemSid));
    // 1.32.0 : une installation "pour moi seul" (sans droits d'administrateur) ecrit ce fichier sous le compte qui
    // installe : il est cru pour ce compte seulement - un autre compte ne peut rien y imposer qu'a lui-meme.
    if (!ok && owner && IsValidSid(owner)) {
        HANDLE token = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            DWORD size = 0;
            (void)GetTokenInformation(token, TokenUser, nullptr, 0, &size);
            std::vector<unsigned char> buf(size ? size : 1);
            if (size && GetTokenInformation(token, TokenUser, buf.data(), size, &size)) {
                const auto* user = reinterpret_cast<const TOKEN_USER*>(buf.data());
                ok = user->User.Sid && EqualSid(owner, user->User.Sid);
            }
            CloseHandle(token);
        }
    }
    if (sd) LocalFree(sd);
    return ok;
#else
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (st.st_uid == 0) return true;
    const char* forced = std::getenv("XPG_IA_INSTALLATION");
    return forced && *forced && st.st_uid == ::geteuid();
#endif
}

// La plan d'une fusion : ce que ferait l'import (peek), puis l'import lui-meme (importFrom).
struct Plan {
    struct Item {
        std::string id, name;
        Json        raw;
        enum class What : std::uint8_t { Add, Replace, Keep, Skip } what{What::Add};
    };
    std::vector<Item>        items;
    std::vector<std::string> statesOff;        // les integrees a desactiver (pas deja desactivees)
    bool                     hasSettings{false};
    bool                     applySettings{false};
    Settings                 settings;
    std::vector<json::Value> conversations;    // les conversations a ajouter
    int                      conversationsKept{0};
};

Plan makePlan(const Json& file, bool replaceSame, ImportReport& report) {
    Plan plan;
    report = ImportReport{};
    report.from = fromLine(file);
    memory::Registry reg;
    reg.load();
    if (const Json* list = file.get("connaissances"); list && list->isArray()) {
        std::set<std::string> seen;
        for (const auto& k : list->items) {
            Plan::Item it;
            it.raw = k;
            it.id = k.str("id");
            it.name = k.str("nom", it.id);
            if (!k.isObject() || !safeId(it.id) || !seen.insert(it.id).second) {
                it.what = Plan::Item::What::Skip;
                ++report.skipped;
                report.lines.push_back("ignor\xC3\xA9" "e : " + (it.name.empty() ? std::string("(sans nom)") : it.name)
                                       + (k.isObject() && !it.id.empty() && seen.count(it.id) && safeId(it.id) ? " (en double dans le fichier)"
                                                                                                                 : " (identifiant refus\xC3\xA9)"));
                plan.items.push_back(std::move(it));
                continue;
            }
            const auto* mine = reg.find(it.id);
            if (mine && mine->builtIn) {
                it.what = Plan::Item::What::Skip;
                ++report.skipped;
                report.lines.push_back("ignor\xC3\xA9" "e : " + it.name + " (c'est une connaissance int\xC3\xA9gr\xC3\xA9" "e)");
            } else if (mine && mine->content == k.str("contenu") && mine->name == k.str("nom", mine->name)) {
                it.what = Plan::Item::What::Keep;
                ++report.kept;
                report.lines.push_back("gard\xC3\xA9" "e : " + it.name + " (identique)");
            } else if (mine) {
                it.what = replaceSame ? Plan::Item::What::Replace : Plan::Item::What::Keep;
                ++(replaceSame ? report.replaced : report.kept);
                report.lines.push_back((replaceSame ? "remplac\xC3\xA9" "e : " : "gard\xC3\xA9" "e : ") + it.name
                                       + (replaceSame ? " (celle du fichier)" : " (une connaissance de ce nom existe d\xC3\xA9j\xC3\xA0 ici)"));
            } else {
                it.what = Plan::Item::What::Add;
                ++report.added;
                report.lines.push_back("ajout\xC3\xA9" "e : " + it.name);
            }
            // 1.21.0 : un modele de fichier (une liste d'E/S, un fichier reseau, des reports deja vus) - dit comme tel
            if (k.str("categorie") == "modele-fichier" && it.what != Plan::Item::What::Skip) {
                report.lines.back() += " (mod\xC3\xA8le de fichier)";
                if (it.what == Plan::Item::What::Add || it.what == Plan::Item::What::Replace) ++report.models;
            }
            plan.items.push_back(std::move(it));
        }
    }
    if (const Json* st = file.get("etat"); st && st->isObject())
        if (const Json* off = st->get("integreesInactives"); off && off->isArray()) {
            const memory::State mine = memory::readState();
            for (const auto& v : off->items)
                if (v.isString() && reg.find(v.text) && std::find(mine.builtInOff.begin(), mine.builtInOff.end(), v.text) == mine.builtInOff.end())
                    plan.statesOff.push_back(v.text);
            if (!plan.statesOff.empty())
                report.lines.push_back("\xC3\xA9tat : " + pluralize(static_cast<int>(plan.statesOff.size()), "connaissance int\xC3\xA9gr\xC3\xA9" "e d\xC3\xA9sactiv\xC3\xA9" "e",
                                                                      "connaissances int\xC3\xA9gr\xC3\xA9" "es d\xC3\xA9sactiv\xC3\xA9" "es")
                                       + " comme sur l'autre PC");
        }
    if (const Json* rs = file.get("reglages"); rs && rs->isObject()) {
        plan.hasSettings = true;
        const Settings cur = loadSettings();
        Settings s = cur;
        // SECURITE : le fournisseur, le service et l'adresse ne viennent jamais d'un fichier (ils decident a qui partent les
        // demandes, et la cle) - ignores, et dit ; le reste (le modele, les preferences) suit la regle d'avant.
        const std::string provider = rs->str("fournisseur"), preset = rs->str("service"), address = rs->str("adresse");
        if ((!provider.empty() && provider != cur.provider) || (!preset.empty() && preset != cur.preset) || (!address.empty() && address != cur.endpoint)) {
            std::string what = "r\xC3\xA9glages : le fournisseur et l'adresse du fichier sont ignor\xC3\xA9s (";
            what += provider.empty() ? std::string("fournisseur non dit") : "\xC2\xAB " + shown(provider) + " \xC2\xBB";
            if (!preset.empty()) what += ", service \xC2\xAB " + shown(preset) + " \xC2\xBB";
            if (!address.empty()) what += ", adresse \xC2\xAB " + shown(address) + " \xC2\xBB";
            what += ") : ils se choisissent dans les Param\xC3\xA8tres de l'Assistant IA";
            report.lines.push_back(std::move(what));
        }
        s.model = rs->str("modele", cur.model);
        s.localFirst = rs->flag("integreeDabord", cur.localFirst);
        s.notifyReplies = rs->flag("avertirReponses", cur.notifyReplies);
        s.applyWithErrors = rs->flag("appliquerMalgreErreurs", cur.applyWithErrors);
        if (cur.provider == "integree" || cur.provider == "demo") {
            plan.applySettings = true;
            plan.settings = s;
            report.lines.push_back("r\xC3\xA9glages : pr\xC3\xA9" "f\xC3\xA9rences appliqu\xC3\xA9" "es (mod\xC3\xA8le, IA int\xC3\xA9gr\xC3\xA9" "e d'abord, "
                                   "avertissements) ; la cl\xC3\xA9 API, elle, ne voyage jamais");
        } else {
            report.lines.push_back("r\xC3\xA9glages : gard\xC3\xA9s (l'Assistant est d\xC3\xA9j\xC3\xA0 r\xC3\xA9gl\xC3\xA9 sur " + providerText(cur) + ")");
        }
    }
    if (const Json* cs = file.get("conversations"); cs && cs->isArray()) {
        std::set<std::string> mine;
        for (const auto& c : loadConversations()) mine.insert(c.id);
        for (const auto& c : cs->items) {
            const std::string id = c.str("id");
            if (!c.isObject() || !safeId(id)) continue;
            if (mine.count(id)) {
                ++plan.conversationsKept;
                continue;
            }
            plan.conversations.push_back(toStore(withoutPaths(c)));
        }
        if (!plan.conversations.empty() || plan.conversationsKept > 0)
            report.lines.push_back("conversations : " + pluralize(static_cast<int>(plan.conversations.size()), "ajout\xC3\xA9" "e", "ajout\xC3\xA9" "es")
                                   + (plan.conversationsKept > 0 ? ", " + pluralize(plan.conversationsKept, "d\xC3\xA9j\xC3\xA0 l\xC3\xA0", "d\xC3\xA9j\xC3\xA0 l\xC3\xA0")
                                                                 : std::string{}));
    }
    return plan;
}

} // namespace

// ---------------------------------------------------------------------- exporter --
bool exportTo(const std::string& path, const FileOptions& options, std::string* why) {
    if (root().empty()) return fail(why, "pas de dossier pour l'Assistant IA");
    if (path.empty()) return fail(why, "pas de fichier");
    Json file = Json::makeObject();
    file.set("format", Json::makeString("xia"));
    file.set("version", Json::makeNumber(kFormatVersion));
    file.set("revision", Json::makeNumber(kFormatRevision));   // 1.21.0 : les modeles de fichiers (la version reste 1)
    file.set("logiciel", Json::makeString(XPG_ANALYZER_VERSION));
    file.set("date", Json::makeString(isoDate(now())));
    file.set("pc", Json::makeString(hmi::netinfo::computerName()));
    int count = 0, models = 0;
    // 1.21.0 : sans les connaissances, les modeles de fichiers seuls (s'ils sont demandes)
    if (!options.knowledge && options.fileModels) {
        memory::Registry reg;
        reg.load();
        Json list = Json::makeArray();
        for (const auto& k : reg.all()) {
            if (k.builtIn || !safeId(k.id) || k.category != "modele-fichier") continue;
            Json raw = k.raw.isObject() ? k.raw : Json::makeObject();
            if (!raw.get("id")) raw.set("id", Json::makeString(k.id));
            list.items.push_back(std::move(raw));
            ++count;
            ++models;
        }
        file.set("connaissances", std::move(list));
    }
    if (options.knowledge) {
        memory::Registry reg;
        reg.load();
        Json list = Json::makeArray();
        for (const auto& k : reg.all()) {
            if (k.builtIn || !safeId(k.id)) continue;
            // 1.21.0 : les modeles de fichiers partent avec les connaissances, sauf si on les retire
            if (k.category == "modele-fichier" && !options.fileModels) continue;
            if (k.category == "modele-fichier") ++models;
            Json raw = k.raw.isObject() ? k.raw : Json::makeObject();
            if (!raw.get("id")) raw.set("id", Json::makeString(k.id));
            list.items.push_back(std::move(raw));
            ++count;
        }
        file.set("connaissances", std::move(list));
    }
    if (options.states) {
        Json st = Json::makeObject();
        Json off = Json::makeArray();
        for (const auto& id : memory::readState().builtInOff) off.items.push_back(Json::makeString(id));
        st.set("integreesInactives", std::move(off));
        file.set("etat", std::move(st));
    }
    if (options.settings) {
        // JAMAIS la cle : elle n'est pas dans Settings (AiSecret la garde), et rien d'autre ne la porte ici.
        const Settings s = loadSettings();
        Json rs = Json::makeObject();
        rs.set("fournisseur", Json::makeString(s.provider));
        rs.set("service", Json::makeString(s.preset));
        rs.set("adresse", Json::makeString(s.endpoint));
        rs.set("modele", Json::makeString(s.model));
        rs.set("integreeDabord", Json::makeBool(s.localFirst));
        rs.set("avertirReponses", Json::makeBool(s.notifyReplies));
        rs.set("appliquerMalgreErreurs", Json::makeBool(s.applyWithErrors));
        file.set("reglages", std::move(rs));
    }
    if (options.conversations) {
        Json list = Json::makeArray();
        for (const auto& c : loadConversations()) list.items.push_back(toMemory(toJson(c)));
        file.set("conversations", std::move(list));
    }
    file.set("modelesFichiers", Json::makeNumber(models));
    std::string err;
    if (!memory::writeText(path, memory::writeJson(file, true), &err)) return fail(why, err.empty() ? "\xC3\xA9" "criture impossible : " + path : err);
    (void)memory::appendActivity("connaissance", "Apprentissage export\xC3\xA9 : " + pluralize(count, "connaissance", "connaissances")
                                                      + (models ? " dont " + pluralize(models, "mod\xC3\xA8le de fichier", "mod\xC3\xA8les de fichiers") : std::string{})
                                                      + " (" + dossiers::utf8De(dossiers::cheminDe(path).filename()) + ")");
    return true;
}

// ----------------------------------------------------------------- voir, importer --
bool peek(const std::string& path, ImportReport& preview, std::string* why) {
    preview = ImportReport{};
    if (root().empty()) return fail(why, "pas de dossier pour l'Assistant IA");
    Json file;
    if (!readFile(path, file, why)) return false;
    (void)makePlan(file, false, preview);
    return true;
}

namespace {
// `fromInstaller` : le .xia de l'installation (depose par l'administrateur) ; sinon un fichier choisi par l'utilisateur,
// dont chaque connaissance est marquee "importee" (une donnee d'un autre poste : AiPrompt PromptParts::imported).
bool importFile(const std::string& path, ImportReport& report, bool replaceSame, std::string* why, bool fromInstaller);
} // namespace

bool importFrom(const std::string& path, ImportReport& report, bool replaceSame, std::string* why) {
    return importFile(path, report, replaceSame, why, false);
}

namespace {
bool importFile(const std::string& path, ImportReport& report, bool replaceSame, std::string* why, bool fromInstaller) {
    report = ImportReport{};
    if (root().empty()) return fail(why, "pas de dossier pour l'Assistant IA");
    Json file;
    if (!readFile(path, file, why)) return false;
    Plan plan = makePlan(file, replaceSame, report);
    // Les connaissances : chacune son fichier, dans connaissances/ (le nom vient de l'identifiant, verifie).
    std::error_code ec;
    fs::create_directories(dossiers::cheminDe(knowledgeDir()), ec);
    int written = 0;
    for (const auto& it : plan.items) {
        if (it.what != Plan::Item::What::Add && it.what != Plan::Item::What::Replace) continue;
        std::string err;
        Json raw = it.raw;
        if (!fromInstaller) raw.set("importee", Json::makeBool(true));
        if (!memory::writeText(knowledgeDir() + "/" + it.id + ".json", memory::writeJson(raw, true), &err)) {
            report.lines.push_back("non \xC3\xA9" "crite : " + it.name + (err.empty() ? std::string{} : " (" + err + ")"));
            (it.what == Plan::Item::What::Add ? report.added : report.replaced)--;
            if (it.raw.str("categorie") == "modele-fichier") --report.models;
            ++report.skipped;
            continue;
        }
        ++written;
    }
    if (!plan.statesOff.empty()) {
        memory::State st = memory::readState();
        st.builtInOff.insert(st.builtInOff.end(), plan.statesOff.begin(), plan.statesOff.end());
        (void)memory::writeState(st);
    }
    if (plan.applySettings) (void)saveSettings(plan.settings);
    for (const auto& c : plan.conversations) (void)saveConversation(conversationFromJson(c));
    (void)memory::appendActivity("connaissance", "Apprentissage import\xC3\xA9 de " + report.from + " : "
                                                      + pluralize(report.added, "connaissance ajout\xC3\xA9" "e", "connaissances ajout\xC3\xA9" "es")
                                                      + (report.replaced ? ", " + pluralize(report.replaced, "remplac\xC3\xA9" "e", "remplac\xC3\xA9" "es") : std::string{})
                                                      + (report.kept ? ", " + pluralize(report.kept, "gard\xC3\xA9" "e", "gard\xC3\xA9" "es") : std::string{})
                                                      + (report.models ? " ; " + pluralize(report.models, "mod\xC3\xA8le de fichier", "mod\xC3\xA8les de fichiers") : std::string{}));
    (void)written;
    return true;
}
} // namespace

// ------------------------------------------------------------ apres l'installation --
//  installation.json (depose par l'installateur, dans le dossier commun) :
//    { "tampon": "<unique>", "ia": "integree|ollama|anthropic|compatible|garder", "adresse": "...", "modele": "...",
//      "dabord": true|false, "apprentissage": "apprentissage.xia" (a cote de lui ; vide : aucun) }
//  installation-appliquee.json (dans le dossier IA de l'utilisateur) : { "tampon": "<le dernier applique>" }.
bool applyInstallation(std::string* said) {
    if (said) said->clear();
    if (root().empty()) return false;
    const std::string dir = installationDir();
    if (dir.empty()) return false;
    std::string text;
    // Un installation.json qui n'appartient pas a l'administrateur (depose par un utilisateur ordinaire) : ignore.
    if (!ownedByAdministrator(dir + "/installation.json")) return false;
    if (!memory::readText(dir + "/installation.json", text)) return false;
    Json inst;
    if (!memory::parseJson(text, inst) || !inst.isObject()) return false;
    const std::string stamp = inst.str("tampon");
    if (stamp.empty()) return false;
    const std::string doneFile = root() + "/installation-appliquee.json";
    if (std::string done; memory::readText(doneFile, done)) {
        Json d;
        if (memory::parseJson(done, d) && d.isObject() && d.str("tampon") == stamp) return false;   // deja applique
    }
    std::vector<std::string> parts;
    const std::string choice = inst.str("ia", "garder");
    if (choice != "garder") {
        Settings s = loadSettings();
        const std::string address = inst.str("adresse"), model = inst.str("modele");
        bool known = true;
        if (choice == "integree") {
            s.provider = "integree";
        } else if (choice == "ollama") {
            s.provider = "openai";
            s.preset = "ollama";
            s.endpoint = address.empty() ? std::string("http://localhost:11434/v1") : address;
            if (!model.empty()) s.model = model;
        } else if (choice == "anthropic") {
            s.provider = "anthropic";
            s.preset.clear();
            if (!model.empty()) s.model = model;
            s.endpoint = address;   // vide : celle d'Anthropic ; sinon le relais de l'entreprise
        } else if (choice == "compatible") {
            s.provider = "openai";
            s.preset = "autre";
            s.endpoint = address;
            if (!model.empty()) s.model = model;
        } else {
            known = false;
        }
        if (known) {
            if (const Json* first = inst.get("dabord"); first && first->type == Json::Type::Bool) s.localFirst = first->boolean;
            if (saveSettings(s)) parts.push_back("R\xC3\xA9gl\xC3\xA9 par l'installation : " + providerText(s)
                                                   + (s.provider == "anthropic" ? std::string(" (la cl\xC3\xA9 API se saisit dans les param\xC3\xA8tres)") : std::string{}));
        }
    }
    if (const std::string xia = inst.str("apprentissage"); !xia.empty()) {
        // Un nom de fichier du dossier de l'installation, rien d'autre : ni chemin absolu, ni partage reseau (UNC),
        // ni lecteur, ni "..". Le .xia appartient lui aussi a l'administrateur.
        const bool plainName = xia.find_first_of("/\\:") == std::string::npos && xia.find("..") == std::string::npos;
        const std::string path = dir + "/" + xia;
        ImportReport r;
        std::string why;
        if (!plainName)
            parts.push_back("apprentissage non import\xC3\xA9 : \xC2\xAB " + shown(xia) + " \xC2\xBB n'est pas un fichier du dossier de l'installation");
        else if (!ownedByAdministrator(path))
            parts.push_back("apprentissage non import\xC3\xA9 : " + shown(xia) + " n'a pas \xC3\xA9t\xC3\xA9 d\xC3\xA9pos\xC3\xA9 par l'administrateur");
        else if (importFile(path, r, false, &why, true))
            parts.push_back("apprentissage import\xC3\xA9 : " + pluralize(r.added, "connaissance", "connaissances")
                            + (r.kept ? " (" + pluralize(r.kept, "gard\xC3\xA9" "e", "gard\xC3\xA9" "es") + ")" : std::string{}));
        else
            parts.push_back("apprentissage non import\xC3\xA9 : " + why);
    }
    Json done = Json::makeObject();
    done.set("tampon", Json::makeString(stamp));
    done.set("date", Json::makeString(isoDate(now())));
    (void)memory::writeText(doneFile, memory::writeJson(done, true));
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : " ; ") + p;
    if (!out.empty()) (void)memory::appendActivity("connaissance", out);
    if (said) *said = out;
    return !parts.empty();
}

} // namespace app::ai::learning
