#include "GrafcetLayoutFile.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace project {

    namespace {

        std::vector<std::string> tokens(const std::string& line) {
            std::vector<std::string> out;
            std::istringstream in(line);
            std::string word;
            while (in >> word) out.push_back(word);
            return out;
        }

        bool toFloat(const std::string& text, float& out) {
            try {
                std::size_t used = 0;
                const float value = std::stof(text, &used);
                if (used != text.size()) return false;
                out = value;
                return true;
            }
            catch (...) { return false; }
        }

        bool toInt(const std::string& text, int& out) {
            if (text.empty() || !std::all_of(text.begin(), text.end(),
                [](unsigned char c) { return std::isdigit(c) != 0; }))
                return false;
            try { out = std::stoi(text); return true; }
            catch (...) { return false; }
        }

        // Written with enough digits to survive a round trip and no more.
        //
        // Both halves of that matter. A layout file full of 120.000000 is one nobody
        // reads, so a whole number is written whole. But the default six significant
        // digits are NOT enough for the rest: 1234567.5 comes back as 1.23457e+06 and
        // the box has moved. The round trip still looks like an identity, because both
        // sides are lossy in the same way - which is exactly how a defect like this
        // survives a test that compares the text instead of the value. Nine digits is
        // what a 32-bit float needs to come back bit for bit.
        std::string number(float value) {
            std::ostringstream out;
            if (value == static_cast<float>(static_cast<long long>(value)))
                out << static_cast<long long>(value);
            else
                out << std::setprecision(9) << value;
            return out.str();
        }

    } // namespace

    const ChartLayout* LayoutFile::find(const std::string& chart) const {
        const auto at = charts_.find(chart);
        return at == charts_.end() ? nullptr : &at->second;
    }

    void LayoutFile::parse(const std::string& text) {
        charts_.clear();
        unknown_.clear();

        std::string current;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();

            const auto words = tokens(line);
            if (words.empty()) continue;
            if (words[0].front() == '#') continue;     // a comment, or the header

            if (words[0] == "chart" && words.size() >= 2) {
                current = words[1];
                charts_[current];                       // exists even if it has nothing yet
                continue;
            }
            if (words[0] == "step" && words.size() == 4 && !current.empty()) {
                int id = 0;
                gfx::Point at{};
                if (toInt(words[1], id) && toFloat(words[2], at.x) && toFloat(words[3], at.y)) {
                    charts_[current].steps[id] = at;
                    continue;
                }
            }
            if (words[0] == "wire" && words.size() >= 4 && !current.empty()) {
                int id = 0;
                if (toInt(words[1], id) && (words.size() - 2) % 2 == 0) {
                    std::vector<gfx::Point> bends;
                    bool ok = true;
                    for (std::size_t i = 2; i + 1 < words.size(); i += 2) {
                        gfx::Point at{};
                        if (!toFloat(words[i], at.x) || !toFloat(words[i + 1], at.y)) { ok = false; break; }
                        bends.push_back(at);
                    }
                    if (ok) { charts_[current].wires[id] = std::move(bends); continue; }
                }
            }

            // Not understood. Kept, so a file written by a later version does not
            // lose its contents by passing through this one.
            unknown_.push_back(line);
        }
    }

    std::string LayoutFile::serialise() const {
        std::ostringstream out;
        out << "# xpglayout 1\n";
        out << "# Positions only. Deleting this file costs nothing: the automatic\n"
            "# layout takes over and no part of the program lives here.\n";

        for (const auto& [name, layout] : charts_) {
            if (layout.empty()) continue;
            out << "chart " << name << "\n";
            for (const auto& [id, at] : layout.steps)
                out << "step " << id << " " << number(at.x) << " " << number(at.y) << "\n";
            for (const auto& [id, bends] : layout.wires) {
                if (bends.empty()) continue;
                out << "wire " << id;
                for (const auto& at : bends) out << " " << number(at.x) << " " << number(at.y);
                out << "\n";
            }
        }
        for (const auto& line : unknown_) out << line << "\n";
        return out.str();
    }

    std::string LayoutFile::pathFor(const std::string& projectPath) {
        const auto dot = projectPath.find_last_of('.');
        const auto slash = projectPath.find_last_of("/\\");
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
            return projectPath + ".xpglayout";
        return projectPath.substr(0, dot) + ".xpglayout";
    }

} // namespace project