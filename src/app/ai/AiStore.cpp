// =============================================================================
//  app/ai/AiStore.cpp - voir AiStore.hpp
// =============================================================================
#include "AiStore.hpp"

#include "AiPaths.hpp"
#include "../../core/AtomicFile.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>

namespace app::ai {

namespace fs = std::filesystem;

namespace {

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::function<std::time_t()>& clockRef() {
    static std::function<std::time_t()> c;
    return c;
}

std::tm localTm(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

std::string lowerExt(std::string_view name) {
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size()) return {};
    std::string e(name.substr(dot + 1));
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

json::Value toJson(const Attachment& a) {
    json::Value o = json::Value::object();
    o.set("id", a.id);
    o.set("nom", a.name);
    o.set("type", a.type);
    o.set("octets", static_cast<std::uint64_t>(a.bytes));
    o.set("fichier", a.file);
    json::addMembers(o, a.extra);
    return o;
}

Attachment attachmentFromJson(const json::Value& o) {
    Attachment a;
    a.id = o.str("id");
    a.name = o.str("nom");
    a.type = o.str("type");
    a.bytes = static_cast<std::uint64_t>(std::max<std::int64_t>(0, o.integer("octets")));
    a.file = o.str("fichier");
    a.extra = json::unknownMembers(o, {"id", "nom", "type", "octets", "fichier"});
    return a;
}

json::Value toJson(const Message& m) {
    json::Value o = json::Value::object();
    o.set("id", m.id);
    o.set("role", m.role);
    o.set("date", m.date);
    o.set("texte", m.text);
    if (!m.state.empty()) o.set("etat", m.state);
    if (!m.attachments.empty()) {
        json::Value list = json::Value::array();
        for (const auto& a : m.attachments) list.push(toJson(a));
        o.set("pieces", std::move(list));
    }
    if (m.proposal) o.set("proposition", toJson(*m.proposal));
    if (!m.note.empty()) o.set("note", m.note);
    if (!m.provider.empty()) o.set("fournisseur", m.provider);
    if (!m.model.empty()) o.set("modele", m.model);
    if (m.tool) {
        json::Value t = json::Value::object();
        t.set("id", m.tool->id);
        t.set("nom", m.tool->name);
        // L'entree telle quelle : un objet JSON (lisible a l'oeil), sinon son texte brut.
        json::Value input;
        if (json::parse(m.tool->input, input) && input.isObject()) t.set("entree", std::move(input));
        else t.set("entree", m.tool->input);
        if (!m.tool->error.empty()) t.set("erreur", m.tool->error);
        o.set("outil", std::move(t));
    }
    json::addMembers(o, m.extra);
    return o;
}

Message messageFromJson(const json::Value& o) {
    Message m;
    m.id = o.str("id");
    m.role = o.str("role");
    m.date = o.str("date");
    m.text = o.str("texte");
    m.state = o.str("etat");
    for (const auto& a : o.at("pieces").items()) m.attachments.push_back(attachmentFromJson(a));
    if (const auto* p = o.find("proposition"); p && p->isObject()) m.proposal = proposalFromJson(*p);
    m.note = o.str("note");
    m.provider = o.str("fournisseur");
    m.model = o.str("modele");
    if (const auto* t = o.find("outil"); t && t->isObject() && !t->str("id").empty()) {
        ToolCall call;
        call.id = t->str("id");
        call.name = t->str("nom");
        const auto& input = t->at("entree");
        call.input = input.isString() ? input.asString() : input.isNull() ? std::string("{}") : json::write(input, false);
        call.error = t->str("erreur");
        m.tool = std::move(call);
    }
    m.extra = json::unknownMembers(o, {"id", "role", "date", "texte", "etat", "pieces", "proposition", "note", "fournisseur", "modele", "outil"});
    return m;
}

bool writeJsonFile(const std::string& path, const json::Value& v, std::string* why) {
    std::error_code ec;
    fs::create_directories(P(path).parent_path(), ec);
    core::AtomicWrite how;
    how.keepBackup = false;
    if (auto r = core::writeFileAtomic(P(path), json::write(v), how); !r) {
        if (why) *why = "\xC3\xA9" "criture impossible : " + path;
        return false;
    }
    return true;
}

} // namespace

// ---- les fournisseurs -------------------------------------------------------------
const std::vector<ProviderInfo>& providers() {
    // 1.15.0 : Anthropic (Claude) est branche ; un relais d'entreprise passe par son adresse.
    static const std::vector<ProviderInfo> k = {
        {"demo", "D\xC3\xA9monstration (sans IA)", true},
        {"anthropic", "Anthropic (Claude)", true},
        {"service", "Autre service en ligne (\xC3\xA0 venir)", false},
        {"local", "Mod\xC3\xA8le sur ce PC (\xC3\xA0 venir)", false},
    };
    return k;
}

// ---- l'horloge ----------------------------------------------------------------------
void setClock(std::function<std::time_t()> clock) { clockRef() = std::move(clock); }
std::time_t now() { return clockRef() ? clockRef()() : std::time(nullptr); }

std::string isoDate(std::time_t t) {
    const std::tm tm = localTm(t);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                  tm.tm_sec);
    return buf;
}

std::time_t parseIso(std::string_view iso) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    const std::string text(iso);
    if (std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3) return 0;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

std::string shortDate(std::string_view iso, bool timeOnly) {
    const std::time_t t = parseIso(iso);
    if (t == 0) return std::string(iso);
    const std::tm tm = localTm(t);
    char hm[32];
    std::snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
    if (timeOnly) return hm;
    const std::tm today = localTm(now());
    if (tm.tm_year == today.tm_year && tm.tm_yday == today.tm_yday) return hm;
    if (tm.tm_year == today.tm_year && tm.tm_yday + 1 == today.tm_yday) return std::string("hier ") + hm;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%02d/%02d", tm.tm_mday, tm.tm_mon + 1);
    if (tm.tm_year != today.tm_year) std::snprintf(buf, sizeof buf, "%02d/%02d/%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    return buf;
}

std::string newId(std::string_view prefix) {
    static int counter = 0;
    const std::tm tm = localTm(now());
    char stamp[64];
    std::snprintf(stamp, sizeof stamp, "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                  tm.tm_sec);
    while (true) {
        const std::string id = std::string(prefix) + "-" + stamp + "-" + std::to_string(++counter);
        // Unique sur le disque aussi (une autre seance du meme instant).
        std::error_code ec;
        if (root().empty()) return id;
        if (!fs::exists(P(conversationsDir()) / P(id + ".json"), ec) && !fs::exists(P(attachmentsDir()) / P(id), ec)) return id;
    }
}

// ---- le JSON des structures ---------------------------------------------------------------
json::Value toJson(const Conversation& c) {
    json::Value o = json::Value::object();
    o.set("format", c.format);
    o.set("id", c.id);
    o.set("titre", c.title);
    o.set("cree", c.created);
    o.set("modifie", c.modified);
    o.set("demo", c.demo);
    json::Value pr = json::Value::object();
    pr.set("nom", c.project.name);
    pr.set("chemin", c.project.path);
    pr.set("vue", c.project.view);
    json::addMembers(pr, c.project.extra);
    o.set("projet", std::move(pr));
    json::Value msgs = json::Value::array();
    for (const auto& m : c.messages) msgs.push(toJson(m));
    o.set("messages", std::move(msgs));
    json::addMembers(o, c.extra);
    return o;
}

Conversation conversationFromJson(const json::Value& o) {
    Conversation c;
    c.format = static_cast<int>(o.integer("format", 1));
    c.id = o.str("id");
    c.title = o.str("titre");
    c.created = o.str("cree");
    c.modified = o.str("modifie", c.created);
    c.demo = o.flag("demo");
    const auto& pr = o.at("projet");
    c.project.name = pr.str("nom");
    c.project.path = pr.str("chemin");
    c.project.view = pr.str("vue");
    c.project.extra = json::unknownMembers(pr, {"nom", "chemin", "vue"});
    for (const auto& m : o.at("messages").items()) c.messages.push_back(messageFromJson(m));
    c.extra = json::unknownMembers(o, {"format", "id", "titre", "cree", "modifie", "demo", "projet", "messages"});
    return c;
}

json::Value toJson(const Settings& s) {
    json::Value o = json::Value::object();
    o.set("format", 1);
    o.set("actif", s.enabled);
    o.set("fournisseur", s.provider);
    o.set("modele", s.model);
    o.set("adresse", s.endpoint);
    o.set("delai", s.timeoutSeconds);
    o.set("effort", s.effort);
    o.set("mode", s.mode);
    json::Value keep = json::Value::object();
    keep.set("jours", s.keepDays);
    keep.set("nombre", s.keepCount);
    o.set("historique", std::move(keep));
    json::Value priv = json::Value::object();
    priv.set("vue", s.sendView);
    priv.set("selection", s.sendSelection);
    priv.set("variables", s.sendVariables);
    priv.set("pieces", s.sendAttachments);
    o.set("confidentialite", std::move(priv));
    json::addMembers(o, s.extra);
    return o;
}

Settings settingsFromJson(const json::Value& o) {
    Settings s;
    s.enabled = o.flag("actif", true);
    s.provider = o.str("fournisseur", "demo");
    s.model = o.str("modele");
    s.endpoint = o.str("adresse");
    s.timeoutSeconds = static_cast<int>(std::clamp<std::int64_t>(o.integer("delai", 60), 5, 600));
    s.effort = o.str("effort");
    if (s.effort != "low" && s.effort != "medium" && s.effort != "high") s.effort.clear();
    s.mode = o.str("mode", "previsualisation") == "direct" ? "direct" : "previsualisation";
    s.keepDays = static_cast<int>(std::max<std::int64_t>(0, o.at("historique").integer("jours")));
    s.keepCount = static_cast<int>(std::max<std::int64_t>(0, o.at("historique").integer("nombre")));
    const auto& priv = o.at("confidentialite");
    s.sendView = priv.flag("vue", true);
    s.sendSelection = priv.flag("selection", true);
    s.sendVariables = priv.flag("variables", true);
    s.sendAttachments = priv.flag("pieces", true);
    // Une cle API ecrite par erreur (un fichier retouche a la main) n'est jamais relue ni reecrite.
    s.extra = json::unknownMembers(o, {"format", "actif", "fournisseur", "modele", "adresse", "delai", "effort", "mode", "historique",
                                       "confidentialite", "cle", "cleApi", "apiKey", "api_key"});
    return s;
}

// ---- le disque ----------------------------------------------------------------------
std::vector<Conversation> loadConversations(std::vector<std::string>* problems) {
    std::vector<Conversation> out;
    if (root().empty()) return out;
    std::error_code ec;
    const fs::path dir = P(conversationsDir());
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".json") continue;
        std::string text;
        if (!core::readFileAll(e.path(), text)) {
            if (problems) problems->push_back(U(e.path().filename()) + " : illisible");
            continue;
        }
        json::Value v;
        std::string why;
        if (!json::parse(text, v, &why) || !v.isObject()) {
            if (problems) problems->push_back(U(e.path().filename()) + " : " + (why.empty() ? std::string("pas un objet JSON") : why));
            continue;
        }
        Conversation c = conversationFromJson(v);
        if (c.id.empty()) c.id = U(e.path().stem());
        out.push_back(std::move(c));
    }
    std::stable_sort(out.begin(), out.end(), [](const Conversation& a, const Conversation& b) { return a.modified > b.modified; });
    return out;
}

bool saveConversation(const Conversation& c, std::string* why) {
    if (root().empty()) return true;   // rien a ecrire : tout reste en memoire
    if (c.id.empty()) {
        if (why) *why = "conversation sans identifiant";
        return false;
    }
    return writeJsonFile(U(P(conversationsDir()) / P(c.id + ".json")), toJson(c), why);
}

bool deleteConversationFiles(const Conversation& c) {
    if (root().empty()) return true;
    std::error_code ec;
    bool ok = true;
    if (!c.id.empty()) {
        const fs::path file = P(conversationsDir()) / P(c.id + ".json");
        if (fs::exists(file, ec) && !fs::remove(file, ec)) ok = false;
    }
    for (const auto& m : c.messages)
        for (const auto& a : m.attachments)
            if (!a.id.empty()) fs::remove_all(P(attachmentsDir()) / P(a.id), ec);
    return ok;
}

Settings loadSettings(bool* found) {
    if (found) *found = false;
    if (root().empty()) return {};
    std::string text;
    if (!core::readFileAll(P(settingsFile()), text)) return {};
    json::Value v;
    if (!json::parse(text, v) || !v.isObject()) return {};
    if (found) *found = true;
    return settingsFromJson(v);
}

bool saveSettings(const Settings& s, std::string* why) {
    if (root().empty()) return true;
    return writeJsonFile(settingsFile(), toJson(s), why);
}

void appendActivity(std::string_view type, std::string_view text) {
    if (root().empty()) return;
    json::Value o = json::Value::object();
    o.set("date", isoDate(now()));
    o.set("type", std::string(type));
    o.set("texte", std::string(text));
    std::error_code ec;
    fs::create_directories(P(root()), ec);
    std::ofstream f(P(activityFile()), std::ios::binary | std::ios::app);
    if (f) f << json::write(o, false) << '\n';
}

// ---- les pieces jointes ------------------------------------------------------------------
const std::vector<AttachmentFormat>& attachmentFormats() {
    static const std::vector<AttachmentFormat> k = {
        {"png", "Image PNG"},          {"jpg", "Image JPEG"},          {"jpeg", "Image JPEG"},
        {"pdf", "Document PDF"},       {"txt", "Texte"},               {"csv", "Tableau CSV"},
        {"xlsx", "Classeur Excel"},    {"xml", "Fichier XML"},         {"json", "Fichier JSON"},
        {"xihm", "Projet IHM (archive .xihm)"}, {"xapi", "Projet API (archive .xapi)"},
        {"xpgproj", "Projet (dossier)"},
    };
    return k;
}

bool acceptedFormat(std::string_view fileName) {
    const std::string e = lowerExt(fileName);
    for (const auto& f : attachmentFormats())
        if (f.ext == e) return true;
    return false;
}

std::string attachmentFilter() {
    std::string out;
    for (const auto& f : attachmentFormats()) {
        if (!out.empty()) out += ';';
        out += f.ext;
    }
    return out;
}

std::string formatBytes(std::uint64_t b) {
    char buf[32];
    if (b < 1024) return std::to_string(b) + " o";
    if (b < 1024ull * 1024) {
        std::snprintf(buf, sizeof buf, "%.0f Ko", static_cast<double>(b) / 1024.0);
        return buf;
    }
    if (b < 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f Mo", static_cast<double>(b) / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof buf, "%.2f Go", static_cast<double>(b) / (1024.0 * 1024.0 * 1024.0));
    std::string s = buf;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

bool addAttachment(const std::string& sourcePath, Attachment& out, std::string* why) {
    const fs::path src = P(sourcePath);
    const std::string name = U(src.filename());
    if (!acceptedFormat(name)) {
        std::string list;
        for (const auto& f : attachmentFormats()) {
            if (f.ext == "jpeg") continue;
            if (!list.empty()) list += ", ";
            list += f.ext;
        }
        if (why) *why = "\xC2\xAB " + name + " \xC2\xBB : ce format n'est pas pris en charge (accept\xC3\xA9s : " + list + ").";
        return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) {
        if (why) *why = "\xC2\xAB " + name + " \xC2\xBB : fichier introuvable.";
        return false;
    }
    Attachment a;
    a.id = newId("p");
    a.name = name;
    a.type = lowerExt(name);
    a.bytes = static_cast<std::uint64_t>(fs::file_size(src, ec));
    a.source = sourcePath;
    if (!root().empty()) {
        const fs::path dir = P(attachmentsDir()) / P(a.id);
        fs::create_directories(dir, ec);
        fs::copy_file(src, dir / src.filename(), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (why) *why = "\xC2\xAB " + name + " \xC2\xBB : copie impossible dans le dossier de l'Assistant (" + ec.message() + ").";
            fs::remove_all(dir, ec);
            return false;
        }
        a.file = "pieces-jointes/" + a.id + "/" + name;
    }
    out = std::move(a);
    return true;
}

std::string attachmentPath(const Attachment& a) {
    if (a.file.empty() || root().empty()) return {};
    return U(P(root()) / P(a.file));
}

std::vector<std::string> expiredConversations(const std::vector<Conversation>& all, const Settings& s, std::string_view keep) {
    std::vector<const Conversation*> sorted;
    for (const auto& c : all) sorted.push_back(&c);
    std::stable_sort(sorted.begin(), sorted.end(), [](const Conversation* a, const Conversation* b) { return a->modified > b->modified; });
    std::vector<std::string> out;
    const std::time_t limit = s.keepDays > 0 ? now() - static_cast<std::time_t>(s.keepDays) * 86400 : 0;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const Conversation& c = *sorted[i];
        if (c.id == keep) continue;
        const bool tooMany = s.keepCount > 0 && i >= static_cast<std::size_t>(s.keepCount);
        const std::time_t t = parseIso(c.modified);
        const bool tooOld = limit > 0 && t > 0 && t < limit;
        if (tooMany || tooOld) out.push_back(c.id);
    }
    return out;
}

} // namespace app::ai
