// =============================================================================
//  core/ServiceRegistry.hpp — constructor-injection container
// -----------------------------------------------------------------------------
//  Deliberately *not* a service locator sprinkled through the code base. The
//  registry is built once in main(), and each subsystem receives the interfaces
//  it needs through its constructor. The registry exists so that main() does not
//  become a 300-line wiring function and so tests can swap implementations.
// =============================================================================
#pragma once

#include "Result.hpp"

#include <memory>
#include <typeindex>
#include <unordered_map>

namespace core {

class ServiceRegistry {
public:
    template <class Iface, class Impl, class... Deps>
    void provide(std::shared_ptr<Impl> impl) {
        static_assert(std::is_base_of_v<Iface, Impl>, "Impl must implement Iface");
        services_[std::type_index(typeid(Iface))] = std::static_pointer_cast<void>(std::move(impl));
    }

    template <class Iface>
    [[nodiscard]] std::shared_ptr<Iface> get() const {
        auto it = services_.find(std::type_index(typeid(Iface)));
        return it == services_.end() ? nullptr : std::static_pointer_cast<Iface>(it->second);
    }

    // Fails loudly at wiring time rather than silently at first use.
    template <class Iface>
    [[nodiscard]] std::shared_ptr<Iface> require() const {
        auto p = get<Iface>();
        if (!p) throw std::runtime_error(std::string("unregistered service: ") + typeid(Iface).name());
        return p;
    }

private:
    std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
};

} // namespace core
