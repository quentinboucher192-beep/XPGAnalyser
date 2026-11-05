// =============================================================================
//  core/Trash.cpp - 1.25.1 : la corbeille d'un magasin de fichiers (Trash.hpp)
// =============================================================================
#include "Trash.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace core::trash {
namespace {

namespace fs = std::filesystem;

fs::path P(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
std::string U(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Le jour civil <-> le nombre de jours depuis 1970 (H. Hinnant) : sans fuseau, sans timegm.
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}
void civilFromDays(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
}

bool copyThenRemove(const fs::path& from, const fs::path& to, std::string* why) {
    std::error_code ec;
    if (fs::is_directory(from, ec)) fs::copy(from, to, fs::copy_options::recursive, ec);
    else fs::copy_file(from, to, ec);
    if (ec) {
        if (why) *why = ec.message();
        std::error_code e2;
        fs::remove_all(to, e2);
        return false;
    }
    fs::remove_all(from, ec);
    if (ec) {
        if (why) *why = ec.message();
        return false;
    }
    return true;
}

} // namespace

std::string stamp(std::int64_t seconds) {
    if (seconds <= 0) seconds = nowSeconds();
    const std::int64_t days = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
    const std::int64_t rest = seconds - days * 86400;
    std::int64_t y = 1970;
    unsigned m = 1, d = 1;
    civilFromDays(days, y, m, d);
    char out[32];
    std::snprintf(out, sizeof out, "%04lld%02u%02u-%02d%02d%02d", static_cast<long long>(y), m, d, static_cast<int>(rest / 3600),
                  static_cast<int>(rest / 60 % 60), static_cast<int>(rest % 60));
    return out;
}

std::int64_t trashedAt(const std::string& name) {
    // "YYYYMMDD-HHMMSS~..."
    if (name.size() < 17 || name[8] != '-' || name[15] != '~') return -1;
    for (std::size_t i = 0; i < 15; ++i)
        if (i != 8 && (name[i] < '0' || name[i] > '9')) return -1;
    const auto num = [&](std::size_t at, std::size_t n) {
        std::int64_t v = 0;
        for (std::size_t i = at; i < at + n; ++i) v = v * 10 + (name[i] - '0');
        return v;
    };
    const auto y = num(0, 4), mo = num(4, 2), d = num(6, 2), h = num(9, 2), mi = num(11, 2), s = num(13, 2);
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return -1;
    return daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400 + h * 3600 + mi * 60 + s;
}

std::string originalName(const std::string& name) {
    // (un deuxieme du meme nom a la meme seconde porte " (2)" : Ctrl+Z, lui, sait d'ou il vient)
    return trashedAt(name) < 0 ? name : name.substr(16);
}

bool move(const std::string& fromUtf8, const std::string& toUtf8, std::string* why) {
    const fs::path from = P(fromUtf8), to = P(toUtf8);
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(from, ec))) {
        if (why) *why = "introuvable : " + fromUtf8;
        return false;
    }
    fs::create_directories(to.parent_path(), ec);
    ec.clear();
    fs::rename(from, to, ec);
    if (!ec) return true;
    return copyThenRemove(from, to, why);   // un autre volume
}

bool put(const std::string& itemUtf8, const std::string& dirUtf8, std::string& trashed, std::string* why) {
    const fs::path item = P(itemUtf8);
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(item, ec))) {
        if (why) *why = "introuvable : " + itemUtf8;
        return false;
    }
    if (dirUtf8.empty()) {
        if (why) *why = "pas de corbeille ici";
        return false;
    }
    const fs::path dir = P(dirUtf8);
    fs::create_directories(dir, ec);
    const std::string name = U(item.filename());
    const std::string head = stamp() + "~";
    fs::path target = dir / P(head + name);
    for (int n = 2; fs::exists(fs::symlink_status(target, ec)) && n < 10000; ++n) {
        const fs::path p = P(name);
        const bool file = !fs::is_directory(item, ec) && p.has_extension();
        target = dir / P(head + (file ? U(p.stem()) + " (" + std::to_string(n) + ")" + U(p.extension()) : name + " (" + std::to_string(n) + ")"));
    }
    if (!move(itemUtf8, U(target), why)) return false;
    trashed = U(target);
    return true;
}

bool takeBack(const std::string& trashedUtf8, const std::string& originalUtf8, std::string* why) {
    std::error_code ec;
    if (fs::exists(fs::symlink_status(P(originalUtf8), ec))) {
        if (why) *why = "un \xC3\xA9l\xC3\xA9ment du m\xC3\xAAme nom est revenu \xC3\xA0 sa place";
        return false;
    }
    if (!fs::exists(fs::symlink_status(P(trashedUtf8), ec))) {
        if (why) *why = "il n'est plus dans la corbeille";
        return false;
    }
    return move(trashedUtf8, originalUtf8, why);
}

std::size_t purge(const std::string& dirUtf8, int days, std::int64_t now) {
    if (dirUtf8.empty()) return 0;
    if (now <= 0) now = nowSeconds();
    const std::int64_t limit = now - static_cast<std::int64_t>(days) * 86400;
    std::vector<fs::path> gone;
    std::error_code ec;
    for (fs::directory_iterator it(P(dirUtf8), ec), end; !ec && it != end; it.increment(ec)) {
        const auto at = trashedAt(U(it->path().filename()));
        if (at >= 0 && at < limit) gone.push_back(it->path());
    }
    std::size_t n = 0;
    for (const auto& p : gone) {
        std::error_code e2;
        fs::remove_all(p, e2);
        if (!e2) ++n;
    }
    return n;
}

std::vector<std::string> list(const std::string& dirUtf8) {
    std::vector<std::string> out;
    std::error_code ec;
    if (dirUtf8.empty()) return out;
    for (fs::directory_iterator it(P(dirUtf8), ec), end; !ec && it != end; it.increment(ec))
        if (trashedAt(U(it->path().filename())) >= 0) out.push_back(U(it->path()));
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return a > b; });
    return out;
}

bool takeBackAll(const std::vector<Moved>& moved, std::string* why) {
    bool all = true;
    std::string said;
    // A rebours : un dossier parti apres son contenu revient avant lui.
    for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
        std::string w;
        if (takeBack(it->trashed, it->original, &w)) continue;
        all = false;
        if (said.empty()) said = U(P(it->original).filename()) + " : " + w;
    }
    if (!all && why) *why = said;
    return all;
}

bool putAll(std::vector<Moved>& moved, const std::string& dirUtf8, std::string* why) {
    bool all = true;
    for (auto& m : moved) {
        std::string w, at;
        if (put(m.original, dirUtf8, at, &w)) {
            m.trashed = at;
            continue;
        }
        if (all && why) *why = U(P(m.original).filename()) + " : " + w;
        all = false;
    }
    return all;
}

bool apply(CommandPtr cmd, std::string* why) {
    if (!cmd) return false;
    Status st = ok();
    if (auto* stack = CommandGroupScope::stack()) st = stack->push(std::move(cmd));
    else st = cmd->execute();
    if (!st) {
        if (why) *why = st.error().context.empty() ? st.error().message() : st.error().context;
        return false;
    }
    return true;
}

} // namespace core::trash
