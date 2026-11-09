// =============================================================================
//  app/BackgroundTasks.cpp - lot API 8 : bandeau haut, les taches de fond
// =============================================================================
#include "BackgroundTasks.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace app::bgtasks {

namespace {

struct Registry {
    std::mutex          lock;
    std::vector<Task>   tasks;
    std::vector<Notice> notices;
    int                 next{1};
    unsigned long long  revision{0};
};

Registry& registry() {
    static Registry r;
    return r;
}

} // namespace

int begin(std::string label, std::string cancelAction) {
    auto& r = registry();
    const std::lock_guard<std::mutex> g(r.lock);
    Task t;
    t.id = r.next++;
    t.label = std::move(label);
    t.cancelAction = std::move(cancelAction);
    r.tasks.push_back(std::move(t));
    ++r.revision;
    return r.tasks.back().id;
}

void progress(int id, float fraction) {
    auto& r = registry();
    const std::lock_guard<std::mutex> g(r.lock);
    for (auto& t : r.tasks)
        if (t.id == id) t.progress = fraction < 0.f ? -1.f : std::min(1.f, fraction);
    ++r.revision;
}

void end(int id) {
    auto& r = registry();
    const std::lock_guard<std::mutex> g(r.lock);
    r.tasks.erase(std::remove_if(r.tasks.begin(), r.tasks.end(), [id](const Task& t) { return t.id == id; }), r.tasks.end());
    ++r.revision;
}

std::vector<Task> list() {
    auto& r = registry();
    const std::lock_guard<std::mutex> g(r.lock);
    return r.tasks;
}

void post(Notice n) {
    auto& r = registry();
    const std::lock_guard<std::mutex> g(r.lock);
    ++r.revision;
    for (auto& x : r.notices)
        if (x.key == n.key) {
            x = std::move(n);
            return;
        }
    r.notices.push_back(std::move(n));
}

void withdraw(const std::string& key) {
    auto& r = registry();
    const std::lock_guard<std::mutex> g(r.lock);
    r.notices.erase(std::remove_if(r.notices.begin(), r.notices.end(), [&](const Notice& x) { return x.key == key; }), r.notices.end());
    ++r.revision;
}

std::vector<Notice> notices() {
    auto& r = registry();
    const std::lock_guard<std::mutex> g(r.lock);
    return r.notices;
}

unsigned long long revision() {
    auto& r = registry();
    const std::lock_guard<std::mutex> g(r.lock);
    return r.revision;
}

// ============================================================ 1.44.0 : les avis lus ==
namespace {

constexpr std::size_t kMaxRead = 500;

struct ReadSet {
    std::mutex                                      lock;
    std::vector<std::string>                        read;     // les plus anciens d'abord
    std::string                                     file;
    std::function<void(const std::string&)>         hook;
};
ReadSet& readSet() {
    static ReadSet r;
    return r;
}

std::filesystem::path P(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

// La signature d'un avis : sa cle et son titre, sur une ligne (les retours a la ligne et tabulations blanchis).
std::string signature(const std::string& key, const std::string& title) {
    std::string s = (key.empty() ? title : key) + "\t" + title;
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    return s;
}

// Sous le verrou : le fichier reecrit d'un coup (un fichier a cote, puis renomme).
void writeRead(const ReadSet& r) {
    if (r.file.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(P(r.file).parent_path(), ec);
    const std::string tmp = r.file + ".tmp";
    {
        std::ofstream out(P(tmp), std::ios::binary | std::ios::trunc);
        out << "# XPG - les avis de la cloche deja lus (cle, tabulation, titre) : ce compte, tous les projets\n";
        for (const auto& x : r.read) out << x << '\n';
        if (!out) return;
    }
    std::filesystem::rename(P(tmp), P(r.file), ec);
    if (ec) std::filesystem::remove(P(tmp), ec);
}

} // namespace

void markRead(const std::string& key, const std::string& title) {
    std::function<void(const std::string&)> hook;
    {
        auto& r = readSet();
        const std::lock_guard<std::mutex> g(r.lock);
        const std::string s = signature(key, title);
        if (std::find(r.read.begin(), r.read.end(), s) == r.read.end()) {
            r.read.push_back(s);
            if (r.read.size() > kMaxRead) r.read.erase(r.read.begin(), r.read.begin() + static_cast<std::ptrdiff_t>(r.read.size() - kMaxRead));
            writeRead(r);
        }
        hook = r.hook;
    }
    if (hook) hook(key);
    auto& reg = registry();
    const std::lock_guard<std::mutex> g(reg.lock);
    ++reg.revision;
}

bool isRead(const std::string& key, const std::string& title) {
    auto& r = readSet();
    const std::lock_guard<std::mutex> g(r.lock);
    const std::string s = signature(key, title);
    return std::find(r.read.begin(), r.read.end(), s) != r.read.end();
}

void setReadFile(std::string path) {
    auto& r = readSet();
    const std::lock_guard<std::mutex> g(r.lock);
    r.file = std::move(path);
    if (r.file.empty()) return;
    std::ifstream in(P(r.file), std::ios::binary);
    std::string line;
    std::vector<std::string> loaded;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        loaded.push_back(line);
    }
    // ce qui a ete lu avant l'ouverture (rare) reste, apres ce qui vient du fichier
    for (auto& x : r.read)
        if (std::find(loaded.begin(), loaded.end(), x) == loaded.end()) loaded.push_back(std::move(x));
    if (loaded.size() > kMaxRead) loaded.erase(loaded.begin(), loaded.begin() + static_cast<std::ptrdiff_t>(loaded.size() - kMaxRead));
    r.read = std::move(loaded);
}

void setReadHook(std::function<void(const std::string& key)> hook) {
    auto& r = readSet();
    const std::lock_guard<std::mutex> g(r.lock);
    r.hook = std::move(hook);
}

void resetReadForTests() {
    auto& r = readSet();
    const std::lock_guard<std::mutex> g(r.lock);
    r.read.clear();
    r.file.clear();
    r.hook = nullptr;
}

} // namespace app::bgtasks
