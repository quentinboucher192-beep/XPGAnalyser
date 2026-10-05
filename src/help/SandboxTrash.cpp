#include "SandboxTrash.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <system_error>
#include <thread>

namespace help::sandbox {
namespace {

// 1.11.1 (T1, tranche 31) : UN fil, et une file d'attente. Avant, discard() attendait le fil
// d'avant (« au plus un fil a la fois ») et la remise en place attendait aussi l'effacement en
// cours : sous charge (la fabrication), le fil principal est reste 8,1 s dans std::thread::join
// (le rapport de blocage de T2, 03/10 11 h 57, sur la Release de 0009040 : openSandbox ->
// thread::join). Maintenant le fil principal ne fait que poser le dossier dans la file ; le fil
// efface, un dossier apres l'autre, dans l'ordre. finishDiscards() vide la file et attend le fil
// (la sortie de l'appli, les essais).
// Joint a la destruction : une sortie qui ne passe pas par tutorials::shutdown attend aussi le fil
// (un std::thread encore joignable a sa destruction arreterait l'appli : std::terminate).
struct Worker {
    std::mutex                         m;
    std::condition_variable            cv;
    std::deque<std::filesystem::path>  queue;
    std::filesystem::path              current;     // celui qu'efface le fil (vide : aucun)
    bool                               stop{false};
    std::thread                        thread;

    void run() {
        std::unique_lock<std::mutex> lock(m);
        for (;;) {
            cv.wait(lock, [this] { return stop || !queue.empty(); });
            if (queue.empty()) return;              // stop, et plus rien a effacer
            current = queue.front();
            queue.pop_front();
            const auto dir = current;
            lock.unlock();
            std::error_code e;
            std::filesystem::remove_all(dir, e);
            lock.lock();
            current.clear();
        }
    }
    void finish() {
        {
            std::lock_guard<std::mutex> lock(m);
            stop = true;
        }
        cv.notify_all();
        if (thread.joinable()) thread.join();
        std::lock_guard<std::mutex> lock(m);
        stop = false;
        current.clear();
    }
    ~Worker() { finish(); }
};
Worker g_worker;
unsigned long g_count = 0;

} // namespace

bool contains(const std::string& folder, const std::filesystem::path& root) {
    if (folder.empty() || root.empty()) return false;
    const auto r = root.lexically_normal().string();
    const auto f = std::filesystem::path(folder).lexically_normal().string();
    return f.size() >= r.size() && f.compare(0, r.size(), r) == 0
           && (f.size() == r.size() || f[r.size()] == '/' || f[r.size()] == '\\');
}

void finishDiscards() {
    g_worker.finish();
    // Rien ne reste dans la file : finish() ne rend la main qu'une fois le fil parti, et le
    // fil ne part que la file vide.
}

void discardLater(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    const auto key = dir.lexically_normal();
    try {
        std::lock_guard<std::mutex> lock(g_worker.m);
        if (key == g_worker.current || std::find(g_worker.queue.begin(), g_worker.queue.end(), key) != g_worker.queue.end())
            return;                                 // deja dans la file, ou en cours
        g_worker.queue.push_back(key);
        if (!g_worker.thread.joinable()) g_worker.thread = std::thread([] { g_worker.run(); });
    } catch (...) {     // pas de fil : tout de suite, sur place
        {
            std::lock_guard<std::mutex> lock(g_worker.m);
            g_worker.queue.erase(std::remove(g_worker.queue.begin(), g_worker.queue.end(), key), g_worker.queue.end());
        }
        std::error_code ec;
        fs::remove_all(key, ec);
        return;
    }
    g_worker.cv.notify_one();
}

bool pending(const std::filesystem::path& dir) {
    const auto key = dir.lexically_normal();
    std::lock_guard<std::mutex> lock(g_worker.m);
    return key == g_worker.current || std::find(g_worker.queue.begin(), g_worker.queue.end(), key) != g_worker.queue.end();
}

bool discard(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return true;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path trash = dir.parent_path() / (std::string(kTrashPrefix) + std::to_string(stamp) + "-" + std::to_string(++g_count));
    fs::rename(dir, trash, ec);
    if (ec) {           // renommage impossible : effacer sur place, comme avant
        ec.clear();
        fs::remove_all(dir, ec);
        return !fs::exists(dir, ec);
    }
    discardLater(trash);
    return true;
}

} // namespace help::sandbox
