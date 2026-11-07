// =============================================================================
//  deploy/Journal.cpp - 1.28.0 : le journal de l'agent (voir Journal.hpp)
// =============================================================================
#include "Journal.hpp"

#include "Paths.hpp"

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

// Un caractere qui ne doit pas paraitre dans une ligne du journal : les controles C0, DEL et C1, les
// separateurs de ligne et de paragraphe Unicode (qu'un lecteur prendrait pour une fin de ligne), les
// controles du sens d'ecriture (qui retournent l'affichage d'une ligne).
bool hidden(std::uint32_t cp) {
    return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || cp == 0x2028 || cp == 0x2029 || cp == 0x200E || cp == 0x200F
           || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069);
}

// La longueur de la sequence UTF-8 valide en `s[i]` (et son point de code), 0 si elle ne l'est pas.
std::size_t utf8At(std::string_view s, std::size_t i, std::uint32_t& cp) {
    const auto b = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char c = b(i);
    std::size_t len = 0;
    std::uint32_t min = 0;
    if (c < 0x80) {
        cp = c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0) len = 2, cp = c & 0x1F, min = 0x80;
    else if ((c & 0xF0) == 0xE0) len = 3, cp = c & 0x0F, min = 0x800;
    else if ((c & 0xF8) == 0xF0) len = 4, cp = c & 0x07, min = 0x10000;
    else return 0;
    if (i + len > s.size()) return 0;
    for (std::size_t k = 1; k < len; ++k) {
        if ((b(i + k) & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (b(i + k) & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    return len;
}

// "AAAA-MM-JJ.log"
bool dayFileName(const std::string& name) {
    if (name.size() != 14 || name.compare(10, 4, ".log") != 0) return false;
    for (std::size_t i = 0; i < 10; ++i) {
        const bool dash = i == 4 || i == 7;
        if (dash ? name[i] != '-' : (name[i] < '0' || name[i] > '9')) return false;
    }
    return true;
}

std::vector<std::string> dayFiles(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec)) {
        const auto u = it->path().filename().u8string();
        std::string name(u.begin(), u.end());
        if (dayFileName(name)) files.push_back(std::move(name));
    }
    std::sort(files.begin(), files.end());   // AAAA-MM-JJ : l'ordre des noms est celui des jours
    return files;
}

} // namespace

std::string Journal::field(std::string_view s, std::size_t max) {
    static constexpr std::string_view kCut = "...";
    std::string out;
    out.reserve(std::min(s.size(), max));
    for (std::size_t i = 0; i < s.size();) {
        std::uint32_t cp = 0;
        const std::size_t len = utf8At(s, i, cp);
        std::string_view piece = len == 0 ? std::string_view("?") : hidden(cp) ? std::string_view(" ") : s.substr(i, len);
        i += len == 0 ? 1 : len;
        // La place de "..." reste si la suite ne tient pas.
        const std::size_t room = i < s.size() ? max - std::min(max, kCut.size()) : max;
        if (out.size() + piece.size() > room) {
            out += kCut.substr(0, std::min(kCut.size(), max - std::min(max, out.size())));
            return out;
        }
        out += piece;
    }
    return out;
}

Journal::Journal(std::string agentDir, std::uint64_t maxDayBytes) : dir_(std::move(agentDir)), maxDay_(maxDayBytes) {}

std::string Journal::directory() const { return joinPath(dir_, "journal"); }

std::string Journal::todayPath() const {
    const std::tm tm = localNow();
    char name[32];
    std::strftime(name, sizeof name, "%Y-%m-%d.log", &tm);
    return joinPath(directory(), name);
}

void Journal::write(std::string_view opId, std::string_view op, std::string_view detail) {
    const std::tm tm = localNow();
    char hms[16], name[32];
    std::strftime(hms, sizeof hms, "%H:%M:%S", &tm);
    std::strftime(name, sizeof name, "%Y-%m-%d.log", &tm);
    std::string line = std::string(hms) + " " + (opId.empty() ? std::string("-") : field(opId)) + " " + field(op);
    if (!detail.empty()) line += " " + field(detail, kJournalMaxDetail);
    line += "\n";
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code ec;
    const std::string path = joinPath(directory(), name);
    if (day_ != name) {
        // Le premier ecrit, ou un nouveau jour : les jours en trop s'effacent (celui-ci compte).
        day_ = name;
        full_ = false;
        std::filesystem::create_directories(P(directory()), ec);
        auto files = dayFiles(directory());
        if (std::find(files.begin(), files.end(), day_) == files.end()) {
            files.push_back(day_);
            std::sort(files.begin(), files.end());
        }
        for (std::size_t i = 0; i + kJournalMaxFiles < files.size(); ++i)
            if (files[i] != day_) std::filesystem::remove(P(joinPath(directory(), files[i])), ec);
    }
    if (full_) return;
    // La taille relue a chaque ligne : --appairer ecrit aussi dans ce fichier.
    const auto size = std::filesystem::file_size(P(path), ec);
    const std::uint64_t have = ec ? 0 : static_cast<std::uint64_t>(size);
    if (have + line.size() > maxDay_) {
        full_ = true;
        if (have >= maxDay_) return;   // deja plein (et dit) avant ce lancement
        const std::string cap = maxDay_ >= 1024 * 1024 ? std::to_string(maxDay_ / (1024 * 1024)) + " Mio" : std::to_string(maxDay_) + " octets";
        line = std::string(hms) + " - JOURNAL plein pour aujourd'hui (" + cap + ") : les lignes suivantes sont perdues jusqu'\xC3\xA0 minuit\n";
    }
    std::ofstream out(P(path), std::ios::binary | std::ios::app);
    out << line;
}

std::vector<std::string> Journal::tail(std::size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::vector<std::string> files = dayFiles(directory());
    std::vector<std::string> out;
    for (auto f = files.rbegin(); f != files.rend() && out.size() < count; ++f) {
        std::ifstream in(P(joinPath(directory(), *f)), std::ios::binary);
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
