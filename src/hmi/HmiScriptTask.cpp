// =============================================================================
//  hmi/HmiScriptTask.cpp - 1.12.26 : un script detache (voir HmiScriptTask.hpp)
// =============================================================================
#include "HmiScriptTask.hpp"

#include <system_error>

namespace hmi {

ScriptTask::ScriptTask(Body body) : body_(std::move(body)) {}

ScriptTask::~ScriptTask() {
    if (started()) {
        cancel();
        while (!finished()) resume();
    }
    if (thread_.joinable()) thread_.join();
}

void ScriptTask::resume() {
    std::unique_lock<std::mutex> lock(m_);
    if (finished_) return;
    taskTurn_ = true;
    if (!started_) {
        started_ = true;
        lock.unlock();
        try {
            thread_ = std::thread([this] { run(); });
        } catch (const std::system_error&) {        // pas de fil de plus : la tache s'arrete la
            lock.lock();
            finished_ = true;
            taskTurn_ = false;
            return;
        }
        lock.lock();
    } else {
        cv_.notify_all();
    }
    cv_.wait(lock, [this] { return !taskTurn_; });
}

void ScriptTask::run() {
    {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [this] { return taskTurn_; });
    }
    try {
        body_(*this);
    } catch (...) {
        // le corps dit lui-meme ses fautes ; une exception ne doit pas laisser l'IHM attendre
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

bool ScriptTask::finished() const {
    std::lock_guard<std::mutex> lock(m_);
    return finished_;
}

bool ScriptTask::started() const {
    std::lock_guard<std::mutex> lock(m_);
    return started_;
}

} // namespace hmi
