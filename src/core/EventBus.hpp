// =============================================================================
//  core/EventBus.hpp — decoupled application-wide notifications
// -----------------------------------------------------------------------------
//  Signal<> is for one publisher -> N subscribers on a *known* object.
//  EventBus is for "something happened in the app" where publisher and
//  subscriber must not know each other: the importer publishes ProjectLoaded,
//  and six unrelated views repopulate themselves.
//
//  Events are POD-ish value types. Delivery is deferred to the top of the frame
//  (drain()) so that no view is mutated in the middle of another view's render.
// =============================================================================
#pragma once

#include "Signal.hpp"

#include <any>
#include <deque>
#include <mutex>
#include <typeindex>
#include <unordered_map>

namespace core {

class EventBus {
public:
    template <class E>
    [[nodiscard]] Connection subscribe(std::function<void(const E&)> fn) {
        auto& ch = channel<E>();
        return ch->connect([f = std::move(fn)](const std::any& a) { f(std::any_cast<const E&>(a)); });
    }

    // Thread-safe: the background import worker publishes progress from its own thread.
    template <class E>
    void publish(E ev) {
        std::scoped_lock lk(mutex_);
        queue_.push_back(Envelope{std::type_index(typeid(E)), std::any(std::move(ev))});
    }

    // Called once per frame from the UI thread, before Update().
    void drain() {
        std::deque<Envelope> local;
        { std::scoped_lock lk(mutex_); local.swap(queue_); }
        for (auto& e : local) {
            auto it = channels_.find(e.type);
            if (it != channels_.end()) it->second->emit(e.payload);
        }
    }

private:
    struct Envelope { std::type_index type; std::any payload; };

    template <class E>
    SignalPtr<const std::any&>& channel() {
        auto key = std::type_index(typeid(E));
        auto it  = channels_.find(key);
        if (it == channels_.end())
            it = channels_.emplace(key, Signal<const std::any&>::create()).first;
        return it->second;
    }

    std::unordered_map<std::type_index, SignalPtr<const std::any&>> channels_;
    std::deque<Envelope> queue_;
    std::mutex           mutex_;
};

} // namespace core
