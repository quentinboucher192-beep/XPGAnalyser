#include "HmiPolicy.hpp"

#include "HmiCrypto.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace hmi {

namespace {

std::size_t codepoints(std::string_view s) {
    std::size_t n = 0;
    for (const char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

// Les jours depuis le 1er janvier 1970 (calendrier civil ; H. Hinnant).
long daysFromCivil(long y, long m, long d) {
    y -= m <= 2 ? 1 : 0;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = y - era * 400;
    const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

std::optional<long> dayNumber(std::string_view s) {
    int y = 0, m = 0, d = 0;
    if (s.size() < 10 || std::sscanf(std::string(s.substr(0, 10)).c_str(), "%d-%d-%d", &y, &m, &d) != 3) return std::nullopt;
    if (m < 1 || m > 12 || d < 1 || d > 31) return std::nullopt;
    return daysFromCivil(y, m, d);
}

// Le mot de passe correspond-il a l'un des recents ("sel:empreinte") ?
bool reused(const Security& sec, const User& u, std::string_view password) {
    if (sec.pwHistory <= 0) return false;
    if (!u.passwordHash.empty() && passwordMatches(u.salt, u.passwordHash, password)) return true;
    const auto keep = static_cast<std::size_t>(std::max(0, sec.pwHistory - 1));
    for (std::size_t i = 0; i < u.previous.size() && i < keep; ++i) {
        const auto& p = u.previous[i];
        const auto colon = p.find(':');
        if (colon == std::string::npos) continue;
        if (passwordMatches(std::string_view(p).substr(0, colon), std::string_view(p).substr(colon + 1), password)) return true;
    }
    return false;
}

} // namespace

std::string passwordProblem(const Security& sec, const User* user, std::string_view password, std::size_t localMin) {
    const std::size_t min = std::max(localMin, static_cast<std::size_t>(std::max(0, sec.pwMinLength)));
    if (codepoints(password) < min) return "au moins " + std::to_string(min) + " caract\xC3\xA8res";
    bool digit = false, letter = false, upper = false, lower = false, special = false;
    for (const char ch : password) {
        const auto c = static_cast<unsigned char>(ch);
        if (c >= '0' && c <= '9') digit = true;
        else if (c >= 'A' && c <= 'Z') { letter = true; upper = true; }
        else if (c >= 'a' && c <= 'z') { letter = true; lower = true; }
        else if (c >= 0x80) letter = true;          // une lettre accentuee (UTF-8)
        else special = true;
    }
    if (sec.pwDigit && !digit) return "au moins un chiffre";
    if (sec.pwLetter && !letter) return "au moins une lettre";
    if (sec.pwMixedCase && !(upper && lower)) return "des majuscules et des minuscules";
    if (sec.pwSpecial && !special) return "au moins un caract\xC3\xA8re sp\xC3\xA9" "cial (! # - _ ...)";
    if (user && reused(sec, *user, password))
        return sec.pwHistory > 1 ? "d\xC3\xA9j\xC3\xA0 utilis\xC3\xA9 : les " + std::to_string(sec.pwHistory) + " derniers mots de passe ne reviennent pas"
                                 : std::string("c'est le mot de passe actuel");
    return {};
}

std::string passwordRules(const Security& sec, std::size_t localMin) {
    const std::size_t min = std::max(localMin, static_cast<std::size_t>(std::max(0, sec.pwMinLength)));
    std::string s = std::to_string(min) + " caract\xC3\xA8res au moins";
    if (sec.pwDigit) s += ", un chiffre";
    if (sec.pwLetter) s += ", une lettre";
    if (sec.pwMixedCase) s += ", majuscules et minuscules";
    if (sec.pwSpecial) s += ", un caract\xC3\xA8re sp\xC3\xA9" "cial";
    if (sec.pwHistory > 1) s += ", pas un des " + std::to_string(sec.pwHistory) + " derniers";
    return s;
}

std::string dayOf(std::string_view stamp) { return std::string(stamp.substr(0, std::min<std::size_t>(10, stamp.size()))); }

std::optional<long> daysBetween(std::string_view from, std::string_view to) {
    const auto a = dayNumber(from), b = dayNumber(to);
    if (!a || !b) return std::nullopt;
    return *b - *a;
}

std::optional<long> passwordDaysLeft(const Security& sec, const User& u, std::string_view today) {
    if (sec.pwMaxAgeDays <= 0 || u.protection != "classique" || u.passwordHash.empty() || u.passwordSet.empty()) return std::nullopt;
    const auto age = daysBetween(u.passwordSet, today);
    if (!age) return std::nullopt;
    return sec.pwMaxAgeDays - *age;
}

std::string renewalReason(const Security& sec, const User& u, std::string_view today) {
    if (u.protection != "classique" || u.passwordHash.empty()) return {};
    if (u.mustChange) return "mot de passe donn\xC3\xA9 par un administrateur : choisis le tien";
    if (const auto left = passwordDaysLeft(sec, u, today); left && *left < 0)
        return "mot de passe expir\xC3\xA9 (plus de " + std::to_string(sec.pwMaxAgeDays) + " jours) : choisis-en un nouveau";
    return {};
}

void storePassword(const Security& sec, User& u, std::string salt, std::string hash, std::string_view today, bool byAdministrator) {
    // L'historique : le mot de passe du moment compte parmi les N derniers ; on
    // garde donc les N - 1 d'avant lui.
    const auto keep = static_cast<std::size_t>(std::max(0, sec.pwHistory - 1));
    if (!u.passwordHash.empty() && keep > 0) u.previous.insert(u.previous.begin(), u.salt + ":" + u.passwordHash);
    if (u.previous.size() > keep) u.previous.resize(keep);
    u.salt = std::move(salt);
    u.passwordHash = std::move(hash);
    u.protection = "classique";
    u.passwordSet = std::string(today);
    u.mustChange = byAdministrator && sec.pwChangeFirst;
}

std::string badgeHash(std::string_view number) {
    const std::string salt = randomHex(8);
    return salt + ":" + passwordHash(salt, number);
}

bool badgeMatches(std::string_view stored, std::string_view number) {
    const auto colon = stored.find(':');
    if (colon == std::string_view::npos || number.empty()) return false;
    return passwordMatches(stored.substr(0, colon), stored.substr(colon + 1), number);
}

} // namespace hmi
