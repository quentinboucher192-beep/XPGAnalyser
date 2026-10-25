// =============================================================================
//  sim/DebugShared.cpp - 1.13.0 : ce que les deux debogueurs disent pareil
// -----------------------------------------------------------------------------
//  Les messages sont ceux du debogueur de l'automate (lot API 8), repris mot
//  pour mot : ses essais (simdebug) les lisent toujours.
// =============================================================================
#include "DebugShared.hpp"

#include <algorithm>
#include <string_view>

namespace sim::debug {

std::string conditionProblem(const core::Error& e) {
    const std::string& m = e.context;
    if (m.size() > 2 && m.front() == '\'') {
        if (const auto close = m.find('\'', 1); close != std::string::npos) {
            const auto name = m.substr(1, close - 1);
            const std::string_view rest = std::string_view(m).substr(close + 1);
            if (rest == " is not declared") return name + " n'est pas d\xC3\xA9" "clar\xC3\xA9" "e ici";
            if (rest.rfind(" is not a function", 0) == 0)
                return name + " n'est pas une fonction que le simulateur conna\xC3\xAEt";
        }
    }
    if (m.rfind("line ", 0) == 0) {
        std::string found;
        if (const auto f = m.rfind("(found '"); f != std::string::npos && m.size() >= f + 10)
            found = m.substr(f + 8, m.size() - f - 10);
        return found.empty() ? std::string("elle ne se lit pas")
                             : "elle ne se lit pas (pr\xC3\xA8s de \xC2\xAB " + found + " \xC2\xBB)";
    }
    if (m == "division by zero" || m == "modulo by zero") return "division par z\xC3\xA9ro";
    if (e.code == core::ErrorCode::NotImplemented)
        return "\xC2\xAB " + m + " \xC2\xBB : une condition n'appelle ni bloc ni fonction inconnue";
    return m.empty() ? e.message() : m;
}

std::string invalidCondition(const std::string& problem) {
    return "condition invalide : " + problem + " ; il ne s'arr\xC3\xAAtera pas";
}

std::string shiftedNote(std::uint32_t at, std::uint32_t asked) {
    return "d\xC3\xA9" "cal\xC3\xA9 \xC3\xA0 la ligne " + std::to_string(at) + " : la ligne " + std::to_string(asked)
           + " n'a pas d'instruction";
}

std::string noStatementNote(std::uint32_t from) {
    return "aucune instruction \xC3\xA0 partir de la ligne " + std::to_string(from) + " : il ne s'arr\xC3\xAAtera pas";
}

void addNote(std::string& note, const std::string& more) {
    if (more.empty()) return;
    note = note.empty() ? more : note + " ; " + more;
}

std::uint32_t effectiveLine(const std::vector<std::uint32_t>& lines, std::uint32_t line) {
    const auto want = std::max<std::uint32_t>(1, line);
    const auto at = std::lower_bound(lines.begin(), lines.end(), want);
    return at == lines.end() ? 0 : *at;
}

} // namespace sim::debug
