// =============================================================================
//  app/ai/AiKeys.cpp - voir AiKeys.hpp
// =============================================================================
#include "AiKeys.hpp"

#include "AiMemory.hpp"
#include "AiMemoryJson.hpp"
#include "AiPaths.hpp"
#include "AiSecret.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <system_error>

namespace app::ai::keys {

namespace fs = std::filesystem;
using memory::Json;

namespace {

std::mutex& lock() {
    static std::mutex m;
    return m;
}

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string indexFile() { return detail::join(folder(), "index.json"); }
std::string fileOf(const std::string& id) { return detail::join(folder(), id + ".bin"); }

// Les cles de la seance (jamais ecrites) : l'entree et son secret.
struct SessionKey {
    Entry       entry;
    std::string secret;
};
std::vector<SessionKey>& session() {
    static std::vector<SessionKey> s;
    return s;
}
int& sessionCounter() {
    static int n = 0;
    return n;
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

// Un identifiant sur : il devient un nom de fichier.
bool safeId(const std::string& id) {
    if (id.empty() || id.size() > 24) return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); });
}

struct Index {
    std::vector<Entry>                               entries;
    std::vector<std::pair<std::string, std::string>> actives;   // fournisseur -> id
    int                                              next{1};
};

Index readIndex() {
    Index ix;
    std::string text;
    if (root().empty() || !memory::readText(indexFile(), text)) return ix;
    Json j;
    if (!memory::parseJson(text, j) || !j.isObject()) return ix;
    ix.next = std::max(1, static_cast<int>(j.num("suivant", 1)));
    if (const Json* list = j.get("cles"); list && list->isArray())
        for (const auto& o : list->items) {
            if (!o.isObject()) continue;
            Entry e;
            e.id = o.str("id");
            if (!safeId(e.id)) continue;
            e.name = o.str("nom");
            e.provider = o.str("fournisseur");
            e.endpoint = o.str("adresse");
            e.model = o.str("modele");
            e.head = o.str("debut");
            e.tail = o.str("fin");
            e.added = o.str("ajoutee");
            if (const Json* t = o.get("essai"); t && t->isObject()) {
                e.testedAt = t->str("date");
                e.tested = static_cast<int>(t->num("resultat", 0));
                e.testNote = t->str("note");
            }
            e.stored = true;
            ix.entries.push_back(std::move(e));
        }
    if (const Json* a = j.get("actives"); a && a->isObject())
        for (const auto& [k, v] : a->fields)
            if (v.isString()) ix.actives.emplace_back(k, v.text);
    return ix;
}

bool writeIndex(const Index& ix, std::string* why) {
    if (root().empty()) return fail(why, "Pas de dossier pour l'Assistant : rien n'est \xC3\xA9" "crit.");
    std::error_code ec;
    fs::create_directories(P(folder()), ec);
    Json j = Json::makeObject();
    j.set("format", Json::makeNumber(1));
    j.set("suivant", Json::makeNumber(ix.next));
    Json list = Json::makeArray();
    for (const auto& e : ix.entries) {
        Json o = Json::makeObject();
        o.set("id", Json::makeString(e.id));
        o.set("nom", Json::makeString(e.name));
        o.set("fournisseur", Json::makeString(e.provider));
        if (!e.endpoint.empty()) o.set("adresse", Json::makeString(e.endpoint));
        if (!e.model.empty()) o.set("modele", Json::makeString(e.model));
        if (!e.head.empty()) o.set("debut", Json::makeString(e.head));
        o.set("fin", Json::makeString(e.tail));
        o.set("ajoutee", Json::makeString(e.added));
        if (!e.testedAt.empty()) {
            Json t = Json::makeObject();
            t.set("date", Json::makeString(e.testedAt));
            t.set("resultat", Json::makeNumber(e.tested));
            t.set("note", Json::makeString(e.testNote));
            o.set("essai", std::move(t));
        }
        list.items.push_back(std::move(o));
    }
    j.set("cles", std::move(list));
    Json actives = Json::makeObject();
    for (const auto& [k, v] : ix.actives) actives.set(k, Json::makeString(v));
    j.set("actives", std::move(actives));
    return memory::writeText(indexFile(), memory::writeJson(j, true), why);
}

std::string activeIn(const Index& ix, std::string_view provider) {
    for (const auto& [k, v] : ix.actives)
        if (k == provider) return v;
    return {};
}
void setActiveIn(Index& ix, const std::string& provider, const std::string& id) {
    for (auto& [k, v] : ix.actives)
        if (k == provider) {
            v = id;
            return;
        }
    ix.actives.emplace_back(provider, id);
}

// Ce qui n'est pas secret dans une cle : son debut connu et ses 4 derniers caracteres (une cle courte : rien).
void describe(Entry& e, const std::string& secret) {
    e.head.clear();
    if (secret.rfind("sk-ant-", 0) == 0) e.head = "sk-ant-";
    else if (secret.rfind("sk-", 0) == 0) e.head = "sk-";
    e.tail = secret.size() >= 16 ? secret.substr(secret.size() - 4) : std::string{};
}

bool nameTaken(const std::vector<Entry>& all, const std::string& name, const std::string& except) {
    const auto low = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return std::any_of(all.begin(), all.end(), [&](const Entry& e) { return e.id != except && low(e.name) == low(name); });
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::vector<Entry> allEntries(const Index& ix) {
    std::vector<Entry> out = ix.entries;
    for (const auto& s : session()) out.push_back(s.entry);
    return out;
}

} // namespace

std::string providerOf(std::string_view p) {
    if (p == "anthropic") return "anthropic";
    if (p == "openai") return "openai";
    return {};
}

std::string providerLabel(std::string_view p) {
    if (p == "anthropic") return "Anthropic";
    if (p == "openai") return "Compatible OpenAI";
    return std::string(p);
}

std::string folder() { return detail::join(root(), "cles"); }

std::vector<Entry> list(std::string_view provider) {
    std::lock_guard g(lock());
    std::vector<Entry> out;
    for (auto& e : allEntries(readIndex()))
        if (provider.empty() || e.provider == provider) out.push_back(std::move(e));
    return out;
}

std::optional<Entry> find(std::string_view id) {
    for (auto& e : list())
        if (e.id == id) return e;
    return std::nullopt;
}

std::string activeId(std::string_view provider) {
    std::lock_guard g(lock());
    const Index ix = readIndex();
    const auto all = allEntries(ix);
    const std::string want = activeIn(ix, provider);
    for (const auto& e : all)
        if (e.id == want && e.provider == provider) return want;
    // L'active n'existe plus (une cle de la seance d'avant) : la premiere de ce fournisseur.
    for (const auto& e : all)
        if (e.provider == provider) return e.id;
    return {};
}

bool setActive(const std::string& id) {
    std::lock_guard g(lock());
    Index ix = readIndex();
    for (const auto& e : allEntries(ix))
        if (e.id == id) {
            setActiveIn(ix, e.provider, id);
            return writeIndex(ix, nullptr) || !e.stored;
        }
    return false;
}

std::string add(Entry e, const std::string& secret, bool remember, std::string* why) {
    e.name = trimmed(e.name);
    if (e.provider != "anthropic" && e.provider != "openai") {
        (void)fail(why, "Le fournisseur d'une cl\xC3\xA9 : Anthropic ou Compatible OpenAI.");
        return {};
    }
    if (e.name.empty()) {
        (void)fail(why, "Donne un nom \xC3\xA0 la cl\xC3\xA9 (ex. Anthropic - perso).");
        return {};
    }
    if (trimmed(secret).empty()) {
        (void)fail(why, "La cl\xC3\xA9 est vide.");
        return {};
    }
    std::lock_guard g(lock());
    Index ix = readIndex();
    if (nameTaken(allEntries(ix), e.name, {})) {
        (void)fail(why, "Une cl\xC3\xA9 porte d\xC3\xA9j\xC3\xA0 ce nom : \xC2\xAB " + e.name + " \xC2\xBB.");
        return {};
    }
    describe(e, secret);
    e.added = memory::nowIso();
    e.tested = 0;
    e.testedAt.clear();
    e.testNote.clear();
    const bool hadActive = !activeIn(ix, e.provider).empty();
    if (remember && keyProtectionAvailable() && !root().empty()) {
        e.id = "k" + std::to_string(ix.next++);
        std::string w;
        if (!storeKeyAt(fileOf(e.id), trimmed(secret), &w)) {
            (void)fail(why, w);
            return {};
        }
        e.stored = true;
        ix.entries.push_back(e);
        if (!hadActive) setActiveIn(ix, e.provider, e.id);
        if (!writeIndex(ix, why)) {
            std::error_code ec;
            fs::remove(P(fileOf(e.id)), ec);
            return {};
        }
        return e.id;
    }
    e.id = "s" + std::to_string(++sessionCounter());
    e.stored = false;
    session().push_back({e, trimmed(secret)});
    if (!hadActive) {
        setActiveIn(ix, e.provider, e.id);
        (void)writeIndex(ix, nullptr);
    }
    return e.id;
}

bool replace(const std::string& id, const std::string& secret, bool remember, std::string* why) {
    if (trimmed(secret).empty()) return fail(why, "La cl\xC3\xA9 est vide.");
    auto old = find(id);
    if (!old) return fail(why, "Cette cl\xC3\xA9 n'existe plus.");
    // Le meme nom, la meme place d'active : on retire l'ancienne puis on ajoute la neuve sous un nom provisoire.
    const bool wasActive = activeId(old->provider) == id;
    Entry e = *old;
    std::string w;
    if (!remove(id)) return fail(why, "La cl\xC3\xA9 ne se retire pas (un fichier ouvert ?).");
    const std::string made = add(e, secret, remember, &w);
    if (made.empty()) return fail(why, w);
    if (wasActive) (void)setActive(made);
    // Les essais d'avant ne valent plus pour la cle neuve.
    return true;
}

bool update(const Entry& in, std::string* why) {
    const std::string name = trimmed(in.name);
    if (name.empty()) return fail(why, "Donne un nom \xC3\xA0 la cl\xC3\xA9.");
    if (in.provider != "anthropic" && in.provider != "openai") return fail(why, "Le fournisseur d'une cl\xC3\xA9 : Anthropic ou Compatible OpenAI.");
    std::lock_guard g(lock());
    Index ix = readIndex();
    if (nameTaken(allEntries(ix), name, in.id)) return fail(why, "Une cl\xC3\xA9 porte d\xC3\xA9j\xC3\xA0 ce nom : \xC2\xAB " + name + " \xC2\xBB.");
    const auto apply = [&](Entry& e) {
        const bool moved = e.provider != in.provider;
        e.name = name;
        e.provider = in.provider;
        e.endpoint = trimmed(in.endpoint);
        e.model = trimmed(in.model);
        return moved;
    };
    for (auto& s : session())
        if (s.entry.id == in.id) {
            (void)apply(s.entry);
            return true;
        }
    for (auto& e : ix.entries)
        if (e.id == in.id) {
            const std::string before = e.provider;
            if (apply(e) && activeIn(ix, before) == e.id) setActiveIn(ix, before, {});
            return writeIndex(ix, why);
        }
    return fail(why, "Cette cl\xC3\xA9 n'existe plus.");
}

bool remove(const std::string& id) {
    std::lock_guard g(lock());
    auto& ses = session();
    Index ix = readIndex();
    std::string provider;
    const auto before = ses.size();
    for (const auto& s : ses)
        if (s.entry.id == id) provider = s.entry.provider;
    std::erase_if(ses, [&](const SessionKey& s) { return s.entry.id == id; });
    bool found = ses.size() != before;
    for (const auto& e : ix.entries)
        if (e.id == id) provider = e.provider;
    const auto n = ix.entries.size();
    std::erase_if(ix.entries, [&](const Entry& e) { return e.id == id; });
    if (ix.entries.size() != n) {
        found = true;
        std::error_code ec;
        if (safeId(id)) fs::remove(P(fileOf(id)), ec);
    }
    if (!found) return false;
    if (activeIn(ix, provider) == id) {
        std::string next;
        for (const auto& e : allEntries(ix))
            if (e.provider == provider) {
                next = e.id;
                break;
            }
        setActiveIn(ix, provider, next);
    }
    (void)writeIndex(ix, nullptr);
    return true;
}

std::optional<std::string> secret(const std::string& id, std::string* why) {
    {
        std::lock_guard g(lock());
        for (const auto& s : session())
            if (s.entry.id == id) return s.secret;
    }
    if (!safeId(id)) return std::nullopt;
    return loadKeyAt(fileOf(id), why);
}

void noteTest(const std::string& id, bool ok, const std::string& note) {
    std::lock_guard g(lock());
    for (auto& s : session())
        if (s.entry.id == id) {
            s.entry.tested = ok ? 1 : -1;
            s.entry.testedAt = memory::nowIso();
            s.entry.testNote = note;
            return;
        }
    Index ix = readIndex();
    for (auto& e : ix.entries)
        if (e.id == id) {
            e.tested = ok ? 1 : -1;
            e.testedAt = memory::nowIso();
            e.testNote = note;
            (void)writeIndex(ix, nullptr);
            return;
        }
}

std::string masked(const Entry& e) {
    const std::string dots = "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2";   // quatre points
    std::string out = e.head;
    if (!out.empty()) out += "\xE2\x80\xA6";
    return out + dots + e.tail;
}

std::size_t storedCount() {
    std::lock_guard g(lock());
    return readIndex().entries.size();
}

bool migrateLegacy(std::string_view settingsProvider) {
    if (root().empty() || !hasStoredKey()) return false;
    std::string provider = providerOf(settingsProvider);
    if (provider.empty()) provider = "anthropic";   // 1.15.0 : la cle d'avant etait celle d'Anthropic
    {
        std::lock_guard g(lock());
        if (!readIndex().entries.empty()) return false;   // le gestionnaire a deja ses cles
    }
    auto key = loadStoredKey();
    if (!key) return false;   // illisible ici (une autre session) : on la laisse ou elle est
    Entry e;
    e.provider = provider;
    e.name = "Ma cl\xC3\xA9 (" + providerLabel(provider) + ")";
    const std::string id = add(e, *key, true, nullptr);
    if (id.empty() || id.front() != 'k') return false;
    (void)setActive(id);
    (void)forgetStoredKey();   // elle vit maintenant dans IA/cles/
    return true;
}

void resetSession() {
    std::lock_guard g(lock());
    session().clear();
}

} // namespace app::ai::keys
