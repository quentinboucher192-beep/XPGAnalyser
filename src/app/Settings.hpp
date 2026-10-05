// =============================================================================
//  app/Settings.hpp — workspace persistence
// -----------------------------------------------------------------------------
//  A flat key=value text file, deliberately not JSON or the registry:
//
//    * a human can open it, read it and fix it, which matters when a layout
//      setting makes the window unusable and the only way out is a text editor;
//    * it diffs cleanly, so a team can put a house layout in source control;
//    * it needs no parser beyond std::getline, so there is no dependency and no
//      failure mode more exotic than "a line was ignored".
//
//  Unknown keys are preserved on save. A newer build that adds a setting will
//  not have it silently deleted by an older one sharing the same profile.
// =============================================================================
#pragma once

#include <map>
#include <string>
#include <vector>

namespace app {

class Settings {
public:
    // %APPDATA%\XpgAnalyzer\settings.txt on Windows, $XDG_CONFIG_HOME or
    // ~/.config/xpg-analyzer/settings.txt elsewhere. Falls back to the working
    // directory if neither is set, so it always has somewhere to write.
    [[nodiscard]] static std::string defaultPath();

    bool load(std::string path);
    bool save() const;
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }

    [[nodiscard]] bool        getBool(const std::string& key, bool fallback) const;
    [[nodiscard]] int         getInt(const std::string& key, int fallback) const;
    [[nodiscard]] float       getFloat(const std::string& key, float fallback) const;
    [[nodiscard]] std::string getString(const std::string& key, std::string fallback = {}) const;
    [[nodiscard]] std::vector<float>       getFloats(const std::string& key) const;
    [[nodiscard]] std::vector<std::string> getList(const std::string& key) const;

    void set(const std::string& key, bool value);
    void set(const std::string& key, int value);
    void set(const std::string& key, float value);
    void set(const std::string& key, std::string value);
    void setFloats(const std::string& key, const std::vector<float>& values);
    void setList(const std::string& key, const std::vector<std::string>& values);

    // ---- Lot API 8 : oublier (les filtres retenus : "Tout effacer") ----
    void remove(const std::string& key);                        // rien : rien
    std::size_t removePrefix(const std::string& prefix);        // les cles qui commencent ainsi ; rend combien
    [[nodiscard]] std::vector<std::string> keysWithPrefix(const std::string& prefix) const;
    // ---- fin Lot API 8 ----

private:
    std::string                        path_;
    std::map<std::string, std::string> values_;   // ordered: the file stays stable
    mutable bool                       dirty_{false};
};

} // namespace app
