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
//
//  LA FUSION : une connaissance de meme identifiant est gardee (ou remplacee si
//  `replaceSame`) ; un identifiant suspect (un chemin, des caracteres hors
//  [A-Za-z0-9._-]) est ignore ; rien ne s'ecrit hors du dossier IA. Les reglages
//  du fichier ne s'appliquent que si l'Assistant n'est encore relie a aucune IA
//  (l'IA integree ou la Demonstration) : un reglage fait a la main reste.
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
        s.provider = rs->str("fournisseur", cur.provider);
        s.preset = rs->str("service", cur.preset);
        s.endpoint = rs->str("adresse", cur.endpoint);
        s.model = rs->str("modele", cur.model);
        s.localFirst = rs->flag("integreeDabord", cur.localFirst);
        s.notifyReplies = rs->flag("avertirReponses", cur.notifyReplies);
        s.applyWithErrors = rs->flag("appliquerMalgreErreurs", cur.applyWithErrors);
        const bool known = std::any_of(providers().begin(), providers().end(), [&](const ProviderInfo& p) { return p.key == s.provider; });
        if (!known) {
            report.lines.push_back("r\xC3\xA9glages : ignor\xC3\xA9s (fournisseur inconnu : " + s.provider + ")");
        } else if (cur.provider == "integree" || cur.provider == "demo") {
            plan.applySettings = true;
            plan.settings = s;
            report.lines.push_back("r\xC3\xA9glages : " + providerText(s) + " (appliqu\xC3\xA9s ; la cl\xC3\xA9 API, elle, ne voyage jamais)");
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
            plan.conversations.push_back(toStore(c));
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
    file.set("logiciel", Json::makeString(XPG_ANALYZER_VERSION));
    file.set("date", Json::makeString(isoDate(now())));
    file.set("pc", Json::makeString(hmi::netinfo::computerName()));
    int count = 0;
    if (options.knowledge) {
        memory::Registry reg;
        reg.load();
        Json list = Json::makeArray();
        for (const auto& k : reg.all()) {
            if (k.builtIn || !safeId(k.id)) continue;
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
    std::string err;
    if (!memory::writeText(path, memory::writeJson(file, true), &err)) return fail(why, err.empty() ? "\xC3\xA9" "criture impossible : " + path : err);
    (void)memory::appendActivity("connaissance", "Apprentissage export\xC3\xA9 : " + pluralize(count, "connaissance", "connaissances") + " ("
                                                      + dossiers::utf8De(dossiers::cheminDe(path).filename()) + ")");
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

bool importFrom(const std::string& path, ImportReport& report, bool replaceSame, std::string* why) {
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
        if (!memory::writeText(knowledgeDir() + "/" + it.id + ".json", memory::writeJson(it.raw, true), &err)) {
            report.lines.push_back("non \xC3\xA9" "crite : " + it.name + (err.empty() ? std::string{} : " (" + err + ")"));
            (it.what == Plan::Item::What::Add ? report.added : report.replaced)--;
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
                                                      + (report.kept ? ", " + pluralize(report.kept, "gard\xC3\xA9" "e", "gard\xC3\xA9" "es") : std::string{}));
    (void)written;
    return true;
}

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
    if (const std::string xia = inst.str("apprentissage"); !xia.empty() && xia.find("..") == std::string::npos) {
        const std::string path = xia.find_first_of("/\\") == std::string::npos ? dir + "/" + xia : xia;
        ImportReport r;
        std::string why;
        if (importFrom(path, r, false, &why))
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
