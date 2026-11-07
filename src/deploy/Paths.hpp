// =============================================================================
//  deploy/Paths.hpp - 1.28.0 : un chemin sous un dossier, avec le separateur du
//  systeme (sous Windows : "C:\Users\op\AppData\Local\XPG\Agent\journal", pas un
//  melange de / et de \ dans les messages). Les chemins sont en UTF-8.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace deploy {

[[nodiscard]] inline std::string joinPath(const std::string& dir, std::string_view name) {
    namespace fs = std::filesystem;
    const fs::path p = (fs::path(std::u8string(dir.begin(), dir.end())) / fs::path(std::u8string(name.begin(), name.end()))).make_preferred();
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

} // namespace deploy
