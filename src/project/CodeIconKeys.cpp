// =============================================================================
//  project/CodeIconKeys.cpp - 1.8.0 : voir CodeIconKeys.hpp
// =============================================================================
#include "CodeIconKeys.hpp"

#include "../domain/ExecutionOrder.hpp"

#include <algorithm>
#include <cctype>

namespace project::codeicons {

namespace {

bool sameText(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

bool startsText(std::string_view s, std::string_view prefix) { return s.size() >= prefix.size() && sameText(s.substr(0, prefix.size()), prefix); }

// La cle telle qu'elle est rangee (sa casse), ou la fin de la table.
std::map<std::string, std::string>::iterator findKey(domain::Project& p, std::string_view key) {
    if (auto it = p.codeIcons.find(std::string(key)); it != p.codeIcons.end()) return it;
    for (auto it = p.codeIcons.begin(); it != p.codeIcons.end(); ++it)
        if (sameText(it->first, key)) return it;
    return p.codeIcons.end();
}

} // namespace

Kind kindOfSection(const domain::Project& p, domain::Index s) {
    if (s < p.sections.size()) {
        const auto owner = p.sections[s].owner;
        if (owner < p.pous.size() && p.pous[owner].kind == domain::PouKind::FunctionBlockType) return Kind::DfbSection;
    }
    return Kind::Section;
}

std::string keyForSection(const domain::Project& p, domain::Index s) {
    if (s >= p.sections.size()) return {};
    const auto& sec = p.sections[s];
    const std::string name(p.strings.text(sec.name));
    if (sec.owner < p.pous.size()) {
        const auto& pou = p.pous[sec.owner];
        if (pou.kind == domain::PouKind::FunctionBlockType)
            return core::codeicons::keyOf(Kind::DfbSection, p.strings.text(pou.name), name);
        if (pou.kind == domain::PouKind::ProgramUnit)
            return core::codeicons::keyOf(Kind::Section, p.strings.text(pou.name), name);
    }
    // Une section de tache : la tache (MAST, FAST...).
    std::string task(p.strings.text(sec.task));
    if (task.empty()) task = std::string(p.strings.text(domain::taskOf(p, s)));
    if (task.empty()) task = "MAST";
    return core::codeicons::keyOf(Kind::Section, task, name);
}

std::string keyForPou(const domain::Project& p, domain::Index i) {
    if (i >= p.pous.size()) return {};
    const auto& pou = p.pous[i];
    if (pou.kind == domain::PouKind::ProgramUnit) return core::codeicons::keyOf(Kind::Unit, {}, p.strings.text(pou.name));
    if (pou.kind == domain::PouKind::FunctionBlockType) return core::codeicons::keyOf(Kind::Dfb, {}, p.strings.text(pou.name));
    return {};
}

std::string keyForType(const domain::Project& p, domain::Index i) {
    if (i >= p.derivedTypes.size()) return {};
    return core::codeicons::keyOf(Kind::Ddt, {}, p.strings.text(p.derivedTypes[i].name));
}

int iconOf(const domain::Project& p, std::string_view key) {
    if (key.empty() || p.codeIcons.empty()) return -1;
    if (auto it = p.codeIcons.find(std::string(key)); it != p.codeIcons.end()) return core::codeicons::indexOf(it->second);
    for (const auto& [k, v] : p.codeIcons)
        if (sameText(k, key)) return core::codeicons::indexOf(v);
    return -1;
}

int sectionIcon(const domain::Project& p, domain::Index s) { return p.codeIcons.empty() ? -1 : iconOf(p, keyForSection(p, s)); }
int pouIcon(const domain::Project& p, domain::Index i) { return p.codeIcons.empty() ? -1 : iconOf(p, keyForPou(p, i)); }
int typeIcon(const domain::Project& p, domain::Index i) { return p.codeIcons.empty() ? -1 : iconOf(p, keyForType(p, i)); }

void renameKey(domain::Project& p, std::string_view from, std::string_view to) {
    if (from.empty() || to.empty() || from == to) return;
    const auto it = findKey(p, from);
    if (it == p.codeIcons.end()) return;
    auto value = it->second;
    p.codeIcons.erase(it);
    p.codeIcons[std::string(to)] = std::move(value);
}

void renamePrefix(domain::Project& p, std::string_view fromPrefix, std::string_view toPrefix) {
    if (fromPrefix.empty()) return;
    std::vector<std::pair<std::string, std::string>> moved;
    for (auto it = p.codeIcons.begin(); it != p.codeIcons.end();) {
        if (startsText(it->first, fromPrefix)) {
            moved.emplace_back(std::string(toPrefix) + it->first.substr(fromPrefix.size()), it->second);
            it = p.codeIcons.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& [k, v] : moved) p.codeIcons[k] = std::move(v);
}

SetCodeIconsCommand::SetCodeIconsCommand(std::shared_ptr<domain::Project> p, std::vector<std::pair<std::string, std::string>> changes,
                                         std::string label)
    : project_(std::move(p)), changes_(std::move(changes)), label_(std::move(label)) {}

core::Status SetCodeIconsCommand::execute() {
    if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "pas de projet");
    for (const auto& [key, icon] : changes_)
        if (!icon.empty() && core::codeicons::indexOf(icon) < 0)
            return core::fail(core::ErrorCode::InvalidArgument, "ic\xC3\xB4ne inconnue : " + icon);
    auto& p = *project_;
    before_.clear();
    for (const auto& [key, icon] : changes_) {
        const auto it = findKey(p, key);
        const std::string stored = it != p.codeIcons.end() ? it->first : key;
        before_.emplace_back(stored, it != p.codeIcons.end() ? it->second : std::string{});
        if (it != p.codeIcons.end()) p.codeIcons.erase(it);
        if (!icon.empty()) p.codeIcons[stored] = icon;
    }
    return core::ok();
}

core::Status SetCodeIconsCommand::undo() {
    if (!project_) return core::ok();
    auto& p = *project_;
    for (auto it = before_.rbegin(); it != before_.rend(); ++it) {
        p.codeIcons.erase(it->first);
        if (!it->second.empty()) p.codeIcons[it->first] = it->second;
    }
    return core::ok();
}

} // namespace project::codeicons
