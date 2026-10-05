// =============================================================================
//  app/BackgroundTasks.cpp - lot API 8 : bandeau haut, les taches de fond
// =============================================================================
#include "BackgroundTasks.hpp"

#include <algorithm>
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

} // namespace app::bgtasks
