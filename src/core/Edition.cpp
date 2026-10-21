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
    switch (edition()) {      // 1.12.20 : API Studio, IHM Studio ; les deux ensemble (les essais) : API IHM Studio
        case Edition::Api: return XPG_PRODUCT_API;
        case Edition::Ihm: return XPG_PRODUCT_IHM;
        case Edition::Both: break;
    }
    return XPG_ANALYZER_NAME;
}

} // namespace core
