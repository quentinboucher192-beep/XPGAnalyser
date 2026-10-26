// =============================================================================
//  hmi/HmiScriptTask.cpp - 1.12.26 : un script detache (voir HmiScriptTask.hpp)
// =============================================================================
#include "HmiScriptTask.hpp"

#include <chrono>
#include <deque>
#include <system_error>

namespace hmi {

namespace {
thread_local ScriptTask* t_running = nullptr;        // la tache de ce fil (nulle : pas un fil de tache)
} // namespace

// 1.13.0 : LA RESERVE DES FILS. Une tache finie rend son fil ; la suivante le reprend. Un fil sans travail depuis
// 30 s s'en va. `free` : les fils qui attendent un travail et qu'aucun n'a encore promis (sous le verrou, avec
// `jobs`) - une tache n'attend jamais un fil occupe. Jamais detruite : un fil peut l'attendre a la sortie du programme.
struct TaskWorkers {
    std::mutex               m;
    std::condition_variable  cv;
    std::deque<ScriptTask*>  jobs;
    std::size_t              free{0};

    static TaskWorkers& get() {
        static TaskWorkers* w = new TaskWorkers;
        return *w;
    }
    // Faux : pas de fil de plus (le systeme le refuse).
    bool submit(ScriptTask* t) {
        {
            std::lock_guard<std::mutex> lock(m);
            if (free > 0) {
                --free;
                jobs.push_back(t);
                cv.notify_one();
                return true;
            }
        }
        try {
            std::thread([this, t] { loop(t); }).detach();
            return true;
        } catch (const std::system_error&) {
            return false;
        }
    }
    void loop(ScriptTask* t) {
        for (;;) {
            t_running = t;
            t->run();
            t_running = nullptr;                     // la tache n'est plus touchee : elle peut partir
            std::unique_lock<std::mutex> lock(m);
            ++free;
            if (!cv.wait_for(lock, std::chrono::seconds(30), [this] { return !jobs.empty(); })) {
                --free;
                return;
            }
            t = jobs.front();
            jobs.pop_front();
        }
    }
};

ScriptTask::ScriptTask(Body body) : body_(std::move(body)) {}

ScriptTask::~ScriptTask() {
    if (started()) {
        cancel();
        while (!finished()) resume();
    }
}

void ScriptTask::resume() {
    std::unique_lock<std::mutex> lock(m_);
    if (finished_) return;
    taskTurn_ = true;
    if (!started_) {
        started_ = true;
        lock.unlock();
        const bool ok = TaskWorkers::get().submit(this);
        lock.lock();
        if (!ok) {                                   // pas de fil de plus : la tache s'arrete la
            finished_ = true;
            taskTurn_ = false;
            return;
        }
    } else {
        cv_.notify_all();
    }
    // La tache rend la main : sa fin, un yield - ou une demande (onRootThread), qui tourne ici avant qu'elle reprenne.
    for (;;) {
        cv_.wait(lock, [this] { return !taskTurn_; });
        if (!ownerCall_) return;
        const std::function<void()>* call = ownerCall_;
        lock.unlock();
        std::exception_ptr failure;
        try {
            (*call)();
        } catch (...) {
            failure = std::current_exception();
        }
        lock.lock();
        ownerCall_ = nullptr;
        ownerFailure_ = failure;
        taskTurn_ = true;
        cv_.notify_all();
    }
}

void ScriptTask::run() {
    {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [this] { return taskTurn_; });
    }
    try {
        body_(*this);
    } catch (...) {
        // 1.13.0 : gardee, pas avalee - le moteur la relance sur le fil de l'IHM (takeFailure). Le corps ne laisse
        // jamais l'IHM attendre : la tache finit ici.
        failure_ = std::current_exception();
    }
    std::lock_guard<std::mutex> lock(m_);
    finished_ = true;
    taskTurn_ = false;
    cv_.notify_all();
}

bool ScriptTask::yield() {
    if (cancelled_) return false;
    std::unique_lock<std::mutex> lock(m_);
    taskTurn_ = false;
    cv_.notify_all();
    cv_.wait(lock, [this] { return taskTurn_; });
    return !cancelled_;
}

void ScriptTask::callOwner(const std::function<void()>& fn) {
    std::unique_lock<std::mutex> lock(m_);
    ownerCall_ = &fn;
    taskTurn_ = false;
    cv_.notify_all();
    cv_.wait(lock, [this] { return taskTurn_; });
    std::exception_ptr failure = std::move(ownerFailure_);
    ownerFailure_ = nullptr;
    lock.unlock();
    if (failure) std::rethrow_exception(failure);
}

bool ScriptTask::onTaskThread() noexcept { return t_running != nullptr; }

void ScriptTask::onRootThread(const std::function<void()>& fn) {
    ScriptTask* t = t_running;
    if (!t) {
        fn();
        return;
    }
    // Chez qui l'a lancee - qui peut etre une tache aussi (un script detache lance d'un geste) : on remonte encore.
    const std::function<void()> up = [&fn] { onRootThread(fn); };
    t->callOwner(up);
}

std::exception_ptr ScriptTask::takeFailure() {
    std::lock_guard<std::mutex> lock(m_);
    std::exception_ptr out = std::move(failure_);
    failure_ = nullptr;
    return out;
}

bool ScriptTask::finished() const {
    std::lock_guard<std::mutex> lock(m_);
    return finished_;
}

bool ScriptTask::started() const {
    std::lock_guard<std::mutex> lock(m_);
    return started_;
}

} // namespace hmi
