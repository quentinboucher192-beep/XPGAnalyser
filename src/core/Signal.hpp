// =============================================================================
//  core/Signal.hpp — Observer pattern, typed, with RAII lifetime management
// -----------------------------------------------------------------------------
//  Used for fine-grained widget notifications (Button::clicked,
//  TreeView::selectionChanged). Connections are owned by the subscriber through
//  a Connection token; when the token dies the slot is unsubscribed, so a widget
//  can never be called back after its own destruction.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <algorithm>

namespace core {

class SignalBase {
public:
    virtual ~SignalBase() = default;
    virtual void disconnect(std::uint64_t id) noexcept = 0;
};

// Move-only RAII token. Destroying it removes the slot.
class Connection {
public:
    Connection() = default;
    Connection(std::weak_ptr<SignalBase> owner, std::uint64_t id)
        : owner_(std::move(owner)), id_(id) {}
    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& o) noexcept { swap(o); }
    Connection& operator=(Connection&& o) noexcept { Connection tmp(std::move(o)); swap(tmp); return *this; }
    ~Connection() { reset(); }

    void reset() noexcept {
        if (id_ == 0) return;
        if (auto s = owner_.lock()) s->disconnect(id_);
        id_ = 0;
    }
    void release() noexcept { id_ = 0; owner_.reset(); }   // outlive the subscriber deliberately
    [[nodiscard]] bool connected() const noexcept { return id_ != 0 && !owner_.expired(); }

private:
    void swap(Connection& o) noexcept { std::swap(owner_, o.owner_); std::swap(id_, o.id_); }
    std::weak_ptr<SignalBase> owner_;
    std::uint64_t             id_{0};
};

// A bag of connections a widget/menu keeps as a member; cleared on OnExit().
class ConnectionScope {
public:
    void add(Connection c) { items_.push_back(std::move(c)); }
    void clear() noexcept  { items_.clear(); }
    ConnectionScope& operator+=(Connection c) { add(std::move(c)); return *this; }
private:
    std::vector<Connection> items_;
};

template <class... Args>
class Signal : public SignalBase, public std::enable_shared_from_this<Signal<Args...>> {
    struct Slot { std::uint64_t id; std::function<void(Args...)> fn; };
public:
    static std::shared_ptr<Signal> create() { return std::make_shared<Signal>(); }

    [[nodiscard]] Connection connect(std::function<void(Args...)> fn) {
        const auto id = ++counter_;
        slots_.push_back(Slot{id, std::move(fn)});
        return Connection{this->weak_from_this(), id};
    }

    void disconnect(std::uint64_t id) noexcept override {
        // Deferred erase: emit() may be on the stack above us.
        for (auto& s : slots_) if (s.id == id) { s.fn = nullptr; dirty_ = true; }
        if (!emitting_) compact();
    }

    void emit(Args... args) {
        ++emitting_;
        // Iterate by index: a slot may connect new slots during emission.
        for (std::size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].fn) slots_[i].fn(args...);
        if (--emitting_ == 0 && dirty_) compact();
    }

    [[nodiscard]] std::size_t slotCount() const noexcept { return slots_.size(); }

private:
    void compact() {
        std::erase_if(slots_, [](const Slot& s) { return !s.fn; });
        dirty_ = false;
    }
    std::vector<Slot> slots_;
    std::uint64_t     counter_{0};
    int               emitting_{0};
    bool              dirty_{false};
};

template <class... Args> using SignalPtr = std::shared_ptr<Signal<Args...>>;

} // namespace core
