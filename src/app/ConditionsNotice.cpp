// =============================================================================
//  app/ConditionsNotice.cpp - 1.11 (R111) : les conditions d'activation
//  manquantes, dans la cloche et dans « Verifier l'ordre » (voir le .hpp)
// =============================================================================
#include "ConditionsNotice.hpp"

#include "BackgroundTasks.hpp"
#include "Settings.hpp"
#include "../project/ActivationConditions.hpp"
#include "../project/ProjectStore.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace app::conditions {

std::string folderKey(const std::string& folder) {
    if (folder.empty()) return {};
    std::error_code ec;
    auto p = std::filesystem::weakly_canonical(std::filesystem::absolute(folder, ec), ec);
    if (ec || p.empty()) p = folder;
    auto key = p.generic_string();
    while (key.size() > 1 && key.back() == '/') key.pop_back();
    return key;
}

std::string message(const domain::Project* p, const project::Manifest& m, const Settings& s, const std::string& folder) {
    if (!p) return {};
    auto r = project::missingTaskConditions(*p, m);
    if (!r.suspected) return {};
    const auto key = folderKey(folder);
    if (!key.empty()) {
        const auto tus = s.getList(kSilencedSetting);
        if (std::find(tus.begin(), tus.end(), key) != tus.end()) return {};
    }
    return std::move(r.message);
}

bool syncNotice(const std::string& message) {
    const auto now = noticeText();
    if (message == now) return false;
    if (message.empty()) {
        bgtasks::withdraw(kNoticeKey);
        return true;
    }
    bgtasks::post({kNoticeKey, "Projet", "Conditions d'activation manquantes", message, kSilenceButton, kSilenceAction, "warning"});
    return true;
}

std::string noticeText() {
    for (const auto& n : bgtasks::notices())
        if (n.key == kNoticeKey) return n.detail;
    return {};
}

std::string shortText() {
    if (noticeText().empty()) return {};
    return "Conditions d'activation manquantes (projet import\xC3\xA9 avant la 1.8.0) : "
           "Fichier \xE2\x80\xBA Importer un .XPG (nouveau MAST)\xE2\x80\xA6 les r\xC3\xA9tablit";
}

bool silence(Settings& s, const std::string& folder) {
    const auto key = folderKey(folder);
    if (key.empty()) return false;
    auto tus = s.getList(kSilencedSetting);
    if (std::find(tus.begin(), tus.end(), key) == tus.end()) {
        tus.push_back(key);
        s.setList(kSilencedSetting, tus);
        if (!s.path().empty()) (void)s.save();
    }
    bgtasks::withdraw(kNoticeKey);
    return true;
}

} // namespace app::conditions
