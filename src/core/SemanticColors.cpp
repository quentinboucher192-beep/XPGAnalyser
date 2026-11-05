// =============================================================================
//  core/SemanticColors.cpp - 1.23.1 : le registre des couleurs semantiques
// =============================================================================
#include "SemanticColors.hpp"

#include <cctype>
#include <map>
#include <memory>
#include <mutex>

namespace core::semantic {

namespace {

// Les couleurs dans l'ordre publie (le nom tel qu'il s'ecrit), et l'index des NOMS EN MAJUSCULES.
struct Table {
    std::vector<std::pair<std::string, std::string>> list;
    std::map<std::string, std::size_t>               index;
};

std::mutex& guard() {
    static std::mutex m;
    return m;
}
std::shared_ptr<const Table>& slot() {
    static std::shared_ptr<const Table> t = std::make_shared<const Table>();
    return t;
}
std::shared_ptr<const Table> current() {
    std::lock_guard<std::mutex> lk(guard());
    return slot();
}
std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string_view trimView(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

} // namespace

void publish(std::vector<std::pair<std::string, std::string>> colors) {
    auto t = std::make_shared<Table>();
    for (auto& [name, value] : colors) {
        const std::string key = upper(trimView(name));
        if (key.empty() || t->index.count(key)) continue;   // la premiere d'un nom (celle du projet passe avant)
        t->index[key] = t->list.size();
        t->list.emplace_back(std::string(trimView(name)), std::move(value));
    }
    std::lock_guard<std::mutex> lk(guard());
    slot() = std::move(t);
}

std::optional<std::string> lookup(std::string_view name) {
    const auto t = current();
    std::string_view n = trimView(name);
    if (!n.empty() && n.front() == '@') n.remove_prefix(1);
    const auto it = t->index.find(upper(n));
    if (it == t->index.end()) return std::nullopt;
    return t->list[it->second].second;
}

bool isReference(std::string_view text, std::string* name) {
    const std::string_view s = trimView(text);
    if (s.size() < 2 || s.front() != '@') return false;
    const std::string_view n = trimView(s.substr(1));
    if (n.empty()) return false;
    if (name) *name = std::string(n);
    return true;
}

std::string resolve(std::string_view text) {
    std::string out(text);
    std::string name;
    for (int hop = 0; hop < 8 && isReference(out, &name); ++hop) {
        const auto v = lookup(name);
        if (!v) break;
        out = *v;
    }
    return out;
}

std::size_t count() { return current()->list.size(); }

std::vector<std::pair<std::string, std::string>> all() { return current()->list; }

} // namespace core::semantic
