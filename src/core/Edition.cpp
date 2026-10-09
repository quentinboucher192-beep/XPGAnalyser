#include "Edition.hpp"
#include "Version.hpp"

#include <atomic>

namespace core {

namespace {
std::atomic<Edition> g_edition{Edition::Both};
}

void setEdition(Edition e) noexcept { g_edition.store(e); }
Edition edition() noexcept { return g_edition.load(); }

std::string_view editionKey() noexcept {
    switch (edition()) {
        case Edition::Api: return "api";
        case Edition::Ihm: return "ihm";
        case Edition::Both: break;
    }
    return {};
}

std::string_view editionLabel() noexcept {
    switch (edition()) {
        case Edition::Api: return "API";
        case Edition::Ihm: return "IHM";
        case Edition::Both: break;
    }
    return {};
}

std::string productName() {
    const auto label = editionLabel();
    return label.empty() ? std::string(XPG_ANALYZER_NAME) : std::string(XPG_ANALYZER_NAME) + " " + std::string(label);
}

} // namespace core
