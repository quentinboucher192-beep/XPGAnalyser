// =============================================================================
//  deploy/Journal.cpp - 1.28.0 : le journal de l'agent (voir Journal.hpp)
// =============================================================================
#include "Journal.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace deploy {

namespace {

std::filesystem::path P(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

std::tm localNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

std::string clean(std::string_view s) {
    std::string out(s);
    std::replace(out.begin(), out.end(), '\n', ' ');
    std::replace(out.begin(), out.end(), '\r', ' ');
    return out;
}

} // namespace

Journal::Journal(std::string agentDir) : dir_(std::move(agentDir)) {}

std::string Journal::directory() const { return dir_ + "/journal"; }

std::string Journal::todayPath() const {
    const std::tm tm = localNow();
    char name[32];
    std::strftime(name, sizeof name, "%Y-%m-%d.log", &tm);
    return directory() + "/" + name;
}

void Journal::write(std::string_view opId, std::string_view op, std::string_view detail) {
    const std::tm tm = localNow();
    char hms[16];
    std::strftime(hms, sizeof hms, "%H:%M:%S", &tm);
    std::string line = std::string(hms) + " " + (opId.empty() ? std::string("-") : clean(opId)) + " " + clean(op);
    if (!detail.empty()) line += " " + clean(detail);
    line += "\n";
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code ec;
    std::filesystem::create_directories(P(directory()), ec);
    std::ofstream out(P(todayPath()), std::ios::binary | std::ios::app);
    out << line;
}

std::vector<std::string> Journal::tail(std::size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> files;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(P(directory()), ec), end; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().u8string();
        if (name.size() == 14 && name.substr(10) == u8".log") files.emplace_back(name.begin(), name.end());
    }
    std::sort(files.begin(), files.end());   // AAAA-MM-JJ : l'ordre des noms est celui des jours
    std::vector<std::string> out;
    for (auto f = files.rbegin(); f != files.rend() && out.size() < count; ++f) {
        std::ifstream in(P(directory() + "/" + *f), std::ios::binary);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(std::move(line));
        }
        const std::size_t need = count - out.size();
        const std::size_t from = lines.size() > need ? lines.size() - need : 0;
        out.insert(out.begin(), lines.begin() + static_cast<std::ptrdiff_t>(from), lines.end());
    }
    return out;
}

} // namespace deploy
