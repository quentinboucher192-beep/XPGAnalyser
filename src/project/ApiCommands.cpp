// =============================================================================
//  project/ApiCommands.cpp - lots API 3 et 4
// =============================================================================
#include "ApiCommands.hpp"

#include "../import/ProjectParser.hpp"
#include "EditCommands.hpp"

#include <algorithm>
#include <cctype>

namespace project {

using namespace domain;

namespace {

constexpr std::size_t npos = static_cast<std::size_t>(-1);

std::string lowerOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

core::Status noProject() { return core::fail(core::ErrorCode::InvalidArgument, "pas de projet"); }

std::string tableName(const Project& p, std::size_t t) {
    return t < p.animationTables.size() ? std::string(p.strings.text(p.animationTables[t].name)) : std::string("?");
}

} // namespace

// ================================================================ les noms ====
std::string identifierProblem(std::string_view name) {
    if (name.empty()) return "le nom est vide";
    if (name.size() > 32) return "32 caract\xC3\xA8res au plus";
    if (!std::isalpha(static_cast<unsigned char>(name.front())))
        return "un nom commence par une lettre (sans accent)";
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
            return std::string("'") + c + "' : lettres sans accent, chiffres et _ seulement";
        if (c == '_' && i + 1 < name.size() && name[i + 1] == '_') return "pas deux _ de suite";
    }
    if (name.back() == '_') return "pas de _ \xC3\xA0 la fin";
    return {};
}

std::string animationTableNameProblem(const Project& p, std::string_view name, std::size_t except) {
    if (auto why = identifierProblem(name); !why.empty()) return why;
    const auto low = lowerOf(name);
    for (std::size_t i = 0; i < p.animationTables.size(); ++i)
        if (i != except && lowerOf(p.strings.text(p.animationTables[i].name)) == low)
            return "une table porte d\xC3\xA9j\xC3\xA0 ce nom";
    return {};
}

std::string freeAnimationTableName(const Project& p, std::string_view base) {
    std::string b(base.empty() ? std::string_view("Table") : base);
    if (animationTableNameProblem(p, b).empty()) return b;
    for (int n = 2; n < 1000; ++n) {
        const auto candidate = b + "_" + std::to_string(n);
        if (animationTableNameProblem(p, candidate).empty()) return candidate;
    }
    return b;
}

std::string defaultAnimationTableOwner(const Project& p) {
    for (const auto& t : p.animationTables)
        if (t.owner != 0) return std::string(p.strings.text(t.owner));
    for (const auto& pou : p.pous)
        if (pou.kind == PouKind::ProgramUnit) return std::string(p.strings.text(pou.name));
    return {};
}

// ================================================= les tables d'animation ====
AddAnimationTableCommand::AddAnimationTableCommand(ApiProjectPtr p, std::string name, std::string owner,
                                                   std::vector<AnimationLine> lines, std::size_t at, bool duplicate)
    : project_(std::move(p)), name_(std::move(name)), owner_(std::move(owner)), lines_(std::move(lines)), at_(at),
      duplicate_(duplicate) {}

core::Status AddAnimationTableCommand::execute() {
    if (!project_) return noProject();
    auto& p = *project_;
    if (auto why = animationTableNameProblem(p, name_); !why.empty())
        return core::fail(core::ErrorCode::InvalidArgument, "table " + name_ + " : " + why);
    AnimationTable t;
    t.name = p.strings.intern(name_);
    t.owner = owner_.empty() ? SymbolId{0} : p.strings.intern(owner_);
    for (const auto& l : lines_) t.entries.push_back({p.strings.intern(l.name), l.hmi});
    index_ = at_ == npos || at_ > p.animationTables.size() ? p.animationTables.size() : at_;
    p.animationTables.insert(p.animationTables.begin() + static_cast<long long>(index_), std::move(t));
    return core::ok();
}

core::Status AddAnimationTableCommand::undo() {
    if (!project_ || index_ >= project_->animationTables.size()) return core::ok();
    project_->animationTables.erase(project_->animationTables.begin() + static_cast<long long>(index_));
    return core::ok();
}

std::string AddAnimationTableCommand::label() const {
    return (duplicate_ ? "Dupliquer la table d'animation " : "Cr\xC3\xA9" "er la table d'animation ") + name_;
}

RemoveAnimationTableCommand::RemoveAnimationTableCommand(ApiProjectPtr p, std::size_t table)
    : project_(std::move(p)), table_(table) {}

core::Status RemoveAnimationTableCommand::execute() {
    if (!project_) return noProject();
    auto& p = *project_;
    if (table_ >= p.animationTables.size()) return core::fail(core::ErrorCode::OutOfRange, "cette table n'existe plus");
    name_ = tableName(p, table_);
    removed_ = p.animationTables[table_];
    p.animationTables.erase(p.animationTables.begin() + static_cast<long long>(table_));
    return core::ok();
}

core::Status RemoveAnimationTableCommand::undo() {
    if (!project_ || !removed_) return core::ok();
    auto& tables = project_->animationTables;
    const auto at = std::min(table_, tables.size());
    tables.insert(tables.begin() + static_cast<long long>(at), *removed_);
    removed_.reset();
    return core::ok();
}

std::string RemoveAnimationTableCommand::label() const { return "Supprimer la table d'animation " + name_; }

RenameAnimationTableCommand::RenameAnimationTableCommand(ApiProjectPtr p, std::size_t table, std::string name)
    : project_(std::move(p)), table_(table), name_(std::move(name)) {}

core::Status RenameAnimationTableCommand::execute() {
    if (!project_) return noProject();
    auto& p = *project_;
    if (table_ >= p.animationTables.size()) return core::fail(core::ErrorCode::OutOfRange, "cette table n'existe plus");
    if (auto why = animationTableNameProblem(p, name_, table_); !why.empty())
        return core::fail(core::ErrorCode::InvalidArgument, name_ + " : " + why);
    previous_ = p.animationTables[table_].name;
    previousText_ = std::string(p.strings.text(previous_));
    if (previousText_ == name_) return core::fail(core::ErrorCode::Cancelled, "la table porte d\xC3\xA9j\xC3\xA0 ce nom");
    p.animationTables[table_].name = p.strings.intern(name_);
    return core::ok();
}

core::Status RenameAnimationTableCommand::undo() {
    if (project_ && table_ < project_->animationTables.size()) project_->animationTables[table_].name = previous_;
    return core::ok();
}

std::string RenameAnimationTableCommand::label() const {
    return "Renommer la table d'animation " + previousText_ + " en " + name_;
}

SetAnimationTableOwnerCommand::SetAnimationTableOwnerCommand(ApiProjectPtr p, std::size_t table, std::string owner)
    : project_(std::move(p)), table_(table), owner_(std::move(owner)) {}

core::Status SetAnimationTableOwnerCommand::execute() {
    if (!project_) return noProject();
    auto& p = *project_;
    if (table_ >= p.animationTables.size()) return core::fail(core::ErrorCode::OutOfRange, "cette table n'existe plus");
    bool known = owner_.empty();
    for (const auto& pou : p.pous)
        if (pou.kind == PouKind::ProgramUnit && lowerOf(p.strings.text(pou.name)) == lowerOf(owner_)) known = true;
    if (!known) return core::fail(core::ErrorCode::InvalidArgument, owner_ + " n'est pas une unit\xC3\xA9 de programme du projet");
    previous_ = p.animationTables[table_].owner;
    p.animationTables[table_].owner = owner_.empty() ? SymbolId{0} : p.strings.intern(owner_);
    if (p.animationTables[table_].owner == previous_) return core::fail(core::ErrorCode::Cancelled, "rien ne change");
    return core::ok();
}

core::Status SetAnimationTableOwnerCommand::undo() {
    if (project_ && table_ < project_->animationTables.size()) project_->animationTables[table_].owner = previous_;
    return core::ok();
}

std::string SetAnimationTableOwnerCommand::label() const {
    return "Ranger la table d'animation dans " + (owner_.empty() ? std::string("le projet") : owner_);
}

AddAnimationLinesCommand::AddAnimationLinesCommand(ApiProjectPtr p, std::size_t table, std::vector<AnimationLine> lines,
                                                   std::size_t at)
    : project_(std::move(p)), table_(table), lines_(std::move(lines)), at_(at) {}

core::Status AddAnimationLinesCommand::execute() {
    if (!project_) return noProject();
    auto& p = *project_;
    if (table_ >= p.animationTables.size()) return core::fail(core::ErrorCode::OutOfRange, "cette table n'existe plus");
    auto& entries = p.animationTables[table_].entries;
    std::vector<AnimationEntry> fresh;
    for (const auto& l : lines_) {
        if (l.name.empty()) continue;
        const auto low = lowerOf(l.name);
        const auto same = [&](const AnimationEntry& e) { return e.hmi == l.hmi && lowerOf(p.strings.text(e.name)) == low; };
        if (std::any_of(entries.begin(), entries.end(), same) || std::any_of(fresh.begin(), fresh.end(), same)) continue;
        fresh.push_back({p.strings.intern(l.name), l.hmi});
    }
    if (fresh.empty()) return core::fail(core::ErrorCode::Cancelled, "d\xC3\xA9j\xC3\xA0 dans la table");
    first_ = at_ == npos || at_ > entries.size() ? entries.size() : at_;
    added_ = fresh.size();
    entries.insert(entries.begin() + static_cast<long long>(first_), fresh.begin(), fresh.end());
    return core::ok();
}

core::Status AddAnimationLinesCommand::undo() {
    if (!project_ || table_ >= project_->animationTables.size()) return core::ok();
    auto& entries = project_->animationTables[table_].entries;
    if (first_ + added_ <= entries.size())
        entries.erase(entries.begin() + static_cast<long long>(first_), entries.begin() + static_cast<long long>(first_ + added_));
    return core::ok();
}

std::string AddAnimationLinesCommand::label() const {
    const auto t = project_ ? tableName(*project_, table_) : std::string("?");
    if (added_ == 1 || (added_ == 0 && lines_.size() == 1)) {
        const auto& l = lines_.front();
        return std::string(l.hmi ? "Ajouter la variable IHM " : "Ajouter ") + l.name + " \xC3\xA0 la table " + t;
    }
    return "Ajouter " + std::to_string(added_ ? added_ : lines_.size()) + " variables \xC3\xA0 la table " + t;
}

RemoveAnimationLinesCommand::RemoveAnimationLinesCommand(ApiProjectPtr p, std::size_t table, std::vector<std::size_t> rows)
    : project_(std::move(p)), table_(table), rows_(std::move(rows)) {}

core::Status RemoveAnimationLinesCommand::execute() {
    if (!project_) return noProject();
    auto& p = *project_;
    if (table_ >= p.animationTables.size()) return core::fail(core::ErrorCode::OutOfRange, "cette table n'existe plus");
    auto& entries = p.animationTables[table_].entries;
    auto rows = rows_;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    removed_.clear();
    for (const auto r : rows)
        if (r < entries.size()) removed_.emplace_back(r, entries[r]);
    if (removed_.empty()) return core::fail(core::ErrorCode::Cancelled, "aucune ligne choisie");
    for (auto it = removed_.rbegin(); it != removed_.rend(); ++it)
        entries.erase(entries.begin() + static_cast<long long>(it->first));
    return core::ok();
}

core::Status RemoveAnimationLinesCommand::undo() {
    if (!project_ || table_ >= project_->animationTables.size()) return core::ok();
    auto& entries = project_->animationTables[table_].entries;
    for (const auto& [row, e] : removed_)
        entries.insert(entries.begin() + static_cast<long long>(std::min(row, entries.size())), e);
    return core::ok();
}

std::string RemoveAnimationLinesCommand::label() const {
    const auto t = project_ ? tableName(*project_, table_) : std::string("?");
    if (removed_.size() == 1 && project_)
        return "Retirer " + std::string(project_->strings.text(removed_.front().second.name)) + " de la table " + t;
    return "Retirer " + std::to_string(removed_.size()) + " lignes de la table " + t;
}

MoveAnimationLineCommand::MoveAnimationLineCommand(ApiProjectPtr p, std::size_t table, std::size_t from, std::size_t to)
    : project_(std::move(p)), table_(table), from_(from), to_(to) {}

core::Status MoveAnimationLineCommand::execute() {
    if (!project_) return noProject();
    auto& p = *project_;
    if (table_ >= p.animationTables.size()) return core::fail(core::ErrorCode::OutOfRange, "cette table n'existe plus");
    auto& entries = p.animationTables[table_].entries;
    if (from_ >= entries.size() || to_ >= entries.size()) return core::fail(core::ErrorCode::OutOfRange, "hors de la table");
    if (from_ == to_) return core::fail(core::ErrorCode::Cancelled, "la ligne est d\xC3\xA9j\xC3\xA0 \xC3\xA0 cette place");
    auto e = entries[from_];
    entries.erase(entries.begin() + static_cast<long long>(from_));
    entries.insert(entries.begin() + static_cast<long long>(to_), e);
    return core::ok();
}

core::Status MoveAnimationLineCommand::undo() {
    if (!project_ || table_ >= project_->animationTables.size()) return core::ok();
    auto& entries = project_->animationTables[table_].entries;
    if (to_ >= entries.size() || from_ >= entries.size()) return core::ok();
    auto e = entries[to_];
    entries.erase(entries.begin() + static_cast<long long>(to_));
    entries.insert(entries.begin() + static_cast<long long>(from_), e);
    return core::ok();
}

std::string MoveAnimationLineCommand::label() const {
    return "D\xC3\xA9placer une ligne de la table " + (project_ ? tableName(*project_, table_) : std::string("?"));
}

// ================================================================ les taches ====
std::string taskKindLabel(const Task& t) {
    if (t.type == "periodic")
        return "p\xC3\xA9riodique, " + std::to_string(t.period) + " ms";
    if (t.type == "cyclic" || t.type.empty()) return "cyclique";
    return t.type;
}

SetTaskCommand::SetTaskCommand(ApiProjectPtr p, std::size_t task, std::string type, std::uint32_t period, std::uint32_t watchdog)
    : project_(std::move(p)), task_(task), type_(std::move(type)), period_(period), watchdog_(watchdog) {}

core::Status SetTaskCommand::execute() {
    if (!project_) return noProject();
    auto& p = *project_;
    if (task_ >= p.tasks.size()) return core::fail(core::ErrorCode::OutOfRange, "cette t\xC3\xA2" "che n'existe plus");
    if (type_ != "cyclic" && type_ != "periodic")
        return core::fail(core::ErrorCode::InvalidArgument, "une t\xC3\xA2" "che est cyclique ou p\xC3\xA9riodique");
    if (type_ == "periodic" && (period_ < 1 || period_ > 255))
        return core::fail(core::ErrorCode::OutOfRange, "la p\xC3\xA9riode va de 1 \xC3\xA0 255 ms");
    if (watchdog_ < 10 || watchdog_ > 1500)
        return core::fail(core::ErrorCode::OutOfRange, "le chien de garde va de 10 \xC3\xA0 1500 ms");
    const auto name = std::string(p.strings.text(p.tasks[task_].name));
    if (name.rfind("FAST", 0) == 0 && type_ != "periodic")
        return core::fail(core::ErrorCode::InvalidArgument, "FAST est toujours p\xC3\xA9riodique");
    auto& t = p.tasks[task_];
    oldType_ = t.type;
    oldPeriod_ = t.period;
    oldWatchdog_ = t.watchdog;
    if (t.type == type_ && (type_ != "periodic" || t.period == period_) && t.watchdog == watchdog_)
        return core::fail(core::ErrorCode::Cancelled, "rien ne change");
    t.type = type_;
    t.period = type_ == "periodic" ? period_ : 0;
    t.watchdog = watchdog_;
    return core::ok();
}

core::Status SetTaskCommand::undo() {
    if (!project_ || task_ >= project_->tasks.size()) return core::ok();
    auto& t = project_->tasks[task_];
    t.type = oldType_;
    t.period = oldPeriod_;
    t.watchdog = oldWatchdog_;
    return core::ok();
}

std::string SetTaskCommand::label() const {
    const auto name = project_ && task_ < project_->tasks.size() ? std::string(project_->strings.text(project_->tasks[task_].name))
                                                                  : std::string("?");
    return "Modifier la t\xC3\xA2" "che " + name;
}

// ------------------------------------------------ lot API 5 : le plan memoire ----
SetMemoryWindowCommand::SetMemoryWindowCommand(ApiProjectPtr p, domain::MemoryZone zone, domain::MemoryWindow window)
    : project_(std::move(p)), zone_(zone), window_(window) {}

core::Status SetMemoryWindowCommand::execute() {
    if (!project_) return noProject();
    const auto z = static_cast<std::size_t>(zone_);
    if (z >= project_->memoryWindows.size()) return core::fail(core::ErrorCode::OutOfRange, "zone inconnue");
    if (window_.set && window_.to < window_.from)
        return core::fail(core::ErrorCode::InvalidArgument, "la fin vient avant le d\xC3\xA9" "but");
    if (window_.set && window_.to > 1048575u)
        return core::fail(core::ErrorCode::OutOfRange, "au plus 1048575 (la plus grande zone d'un automate)");
    old_ = project_->memoryWindows[z];
    project_->memoryWindows[z] = window_;
    return core::ok();
}

core::Status SetMemoryWindowCommand::undo() {
    if (!project_) return core::ok();
    project_->memoryWindows[static_cast<std::size_t>(zone_)] = old_;
    return core::ok();
}

std::string SetMemoryWindowCommand::label() const {
    static const char* const kZones[] = {"%M", "%MW", "%KW"};
    const std::string zone = kZones[static_cast<std::size_t>(zone_) % 3];
    if (!window_.set) return "Lire toute la zone " + zone;
    return "Borner la zone " + zone + " : " + zone + std::to_string(window_.from) + " \xC3\xA0 " + zone + std::to_string(window_.to);
}

SetVariableFieldCommand::SetVariableFieldCommand(ApiProjectPtr p, domain::Index variable, Field field, std::string value)
    : project_(std::move(p)), variable_(variable), field_(field), value_(std::move(value)) {}

core::Status SetVariableFieldCommand::execute() {
    if (!project_) return noProject();
    auto& p = *project_;
    if (variable_ >= p.variables.size()) return core::fail(core::ErrorCode::OutOfRange, "variable introuvable");
    auto& v = p.variables[variable_];
    old_ = v;
    name_ = std::string(p.strings.text(v.name));
    switch (field_) {
        case Field::Type: {
            if (value_.empty()) return core::fail(core::ErrorCode::InvalidArgument, "un type vide");
            auto t = importer::classifyTypeName(value_, p.strings);
            const auto element = t.elementType ? t.elementType : t.name;
            const bool isArray = t.klass == domain::TypeClass::Array;
            if (auto it = p.typeByName.find(element); it != p.typeByName.end()) {
                t.derivedIndex = it->second;
                if (!isArray) t.klass = domain::TypeClass::Derived;
            } else if (auto ip = p.pouByName.find(element);
                       ip != p.pouByName.end() && p.pous[ip->second].kind == domain::PouKind::FunctionBlockType) {
                if (isArray) return core::fail(core::ErrorCode::InvalidArgument, "pas de tableau de blocs DFB : une instance par variable");
                t.fbTypeIndex = ip->second;
                t.klass = domain::TypeClass::FunctionBlock;
            }
            v.type = t;
            break;
        }
        case Field::Address: {
            domain::Address a;
            if (!value_.empty()) {
                a = domain::Address::parse(value_);
                if (!a.valid()) return core::fail(core::ErrorCode::InvalidArgument, "\xC2\xAB " + value_ + " \xC2\xBB n'est pas une adresse : %MW10, %I0.3, %Q0.1\xE2\x80\xA6");
            }
            v.address = a;
            v.located = a.valid();
            break;
        }
        case Field::InitValue: v.initValue = p.strings.intern(value_); break;
        case Field::Comment:   v.comment = p.strings.intern(value_); break;
        case Field::Effective: {
            if (v.scope != domain::VariableScope::Input && v.scope != domain::VariableScope::Output
                && v.scope != domain::VariableScope::InOut)
                return core::fail(core::ErrorCode::InvalidArgument, "seul un param\xC3\xA8tre d'unit\xC3\xA9 se relie \xC3\xA0 une variable du projet");
            auto& a = v.attributes;
            const auto it = std::find_if(a.begin(), a.end(), [](const auto& kv) { return kv.first == "EffectiveParameter"; });
            if (value_.empty()) {
                if (it != a.end()) a.erase(it);
            } else if (it != a.end()) {
                it->second = value_;
            } else {
                a.emplace_back("EffectiveParameter", value_);
            }
            break;
        }
    }
    p.buildIndices();
    return core::ok();
}

core::Status SetVariableFieldCommand::undo() {
    if (!project_ || variable_ >= project_->variables.size()) return core::ok();
    project_->variables[variable_] = old_;
    project_->buildIndices();
    return core::ok();
}

std::string SetVariableFieldCommand::label() const {
    if (field_ == Field::Effective)
        return value_.empty() ? "D\xC3\xA9lier le param\xC3\xA8tre " + name_ : "Relier " + name_ + " \xC3\xA0 " + value_;
    const char* what = field_ == Field::Type ? "le type" : field_ == Field::Address ? "l'adresse"
                     : field_ == Field::InitValue ? "la valeur initiale" : "le commentaire";
    return std::string("Changer ") + what + " de " + name_;
}

SetProjectIconCommand::SetProjectIconCommand(ApiProjectPtr p, domain::ProjectIcon icon, std::string label)
    : project_(std::move(p)), icon_(std::move(icon)), label_(std::move(label)) {}

core::Status SetProjectIconCommand::execute() {
    if (!project_) return noProject();
    if (!icon_.pixels.empty() && icon_.pixels.size() != 32u * 32u)
        return core::fail(core::ErrorCode::InvalidArgument, "une ic\xC3\xB4ne fait 32 x 32 pixels");
    old_ = project_->icon;
    project_->icon = icon_;
    return core::ok();
}

core::Status SetProjectIconCommand::undo() {
    if (project_) project_->icon = old_;
    return core::ok();
}

std::string SetProjectIconCommand::label() const {
    if (!label_.empty()) return label_;
    return icon_.empty() ? "Retirer l'ic\xC3\xB4ne du projet" : "Changer l'ic\xC3\xB4ne du projet";
}

AddTaskCommand::AddTaskCommand(ApiProjectPtr p, std::string name) : project_(std::move(p)), name_(std::move(name)) {}

core::Status AddTaskCommand::execute() {
    if (!project_) return noProject();
    auto& p = *project_;
    const bool fast = name_ == "FAST";
    const bool aux = name_.size() == 4 && name_.rfind("AUX", 0) == 0 && name_[3] >= '0' && name_[3] <= '3';
    if (!fast && !aux)
        return core::fail(core::ErrorCode::InvalidArgument, name_ + " : les t\xC3\xA2" "ches ajout\xC3\xA9" "es sont FAST ou AUX0 \xC3\xA0 AUX3");
    for (const auto& t : p.tasks)
        if (p.strings.text(t.name) == name_) return core::fail(core::ErrorCode::DuplicateSymbol, name_ + " existe d\xC3\xA9j\xC3\xA0");
    Task t;
    t.name = p.strings.intern(name_);
    t.type = "periodic";
    t.period = fast ? 5 : 100;
    t.watchdog = fast ? 100 : 500;
    p.tasks.push_back(std::move(t));
    index_ = p.tasks.size() - 1;
    return core::ok();
}

core::Status AddTaskCommand::undo() {
    if (project_ && index_ < project_->tasks.size()) project_->tasks.erase(project_->tasks.begin() + static_cast<long long>(index_));
    return core::ok();
}

std::string AddTaskCommand::label() const { return "Ajouter la t\xC3\xA2" "che " + name_; }

RemoveTaskCommand::RemoveTaskCommand(ApiProjectPtr p, std::size_t task) : project_(std::move(p)), task_(task) {}

core::Status RemoveTaskCommand::execute() {
    if (!project_) return noProject();
    auto& p = *project_;
    if (task_ >= p.tasks.size()) return core::fail(core::ErrorCode::OutOfRange, "cette t\xC3\xA2" "che n'existe plus");
    const auto& t = p.tasks[task_];
    const std::string name(p.strings.text(t.name));
    if (name == "MAST") return core::fail(core::ErrorCode::InvalidArgument, "MAST ne se retire pas : c'est la t\xC3\xA2" "che ma\xC3\xAEtre");
    for (const auto& s : p.sections)
        if (s.task == t.name) return core::fail(core::ErrorCode::InvalidArgument, name + " ex\xC3\xA9" "cute encore des sections : d\xC3\xA9placez-les d'abord");
    for (const auto& pou : p.pous)
        if (pou.kind == PouKind::ProgramUnit && pou.task == t.name)
            return core::fail(core::ErrorCode::InvalidArgument, name + " ex\xC3\xA9" "cute encore une unit\xC3\xA9 de programme");
    removed_ = t;
    p.tasks.erase(p.tasks.begin() + static_cast<long long>(task_));
    return core::ok();
}

core::Status RemoveTaskCommand::undo() {
    if (!project_ || !removed_) return core::ok();
    auto& tasks = project_->tasks;
    tasks.insert(tasks.begin() + static_cast<long long>(std::min(task_, tasks.size())), *removed_);
    removed_.reset();
    return core::ok();
}

std::string RemoveTaskCommand::label() const {
    return "Retirer la t\xC3\xA2" "che " + (removed_ && project_ ? std::string(project_->strings.text(removed_->name)) : std::string());
}

// ============================================================== le materiel ====
ReplaceHardwareCommand::ReplaceHardwareCommand(ApiProjectPtr p, HardwareConfig next, std::string source)
    : project_(std::move(p)), next_(std::move(next)), source_(std::move(source)) {}

core::Status ReplaceHardwareCommand::execute() {
    if (!project_) return noProject();
    auto& hw = project_->hardware;
    previous_ = hw;
    auto merged = next_;
    // L'identite du processeur : celle du projet quand il la porte (l'export .XPG
    // la donne aussi) ; le .XHW la complete sinon.
    if (!hw.family.empty()) merged.family = hw.family;
    if (!hw.cpuReference.empty()) merged.cpuReference = hw.cpuReference;
    if (!hw.cpuFirmware.empty()) merged.cpuFirmware = hw.cpuFirmware;
    if (!hw.resourceName.empty()) merged.resourceName = hw.resourceName;
    merged.inferred = merged.racks.empty();
    hw = std::move(merged);
    return core::ok();
}

core::Status ReplaceHardwareCommand::undo() {
    if (project_ && previous_) project_->hardware = *previous_;
    return core::ok();
}

std::string ReplaceHardwareCommand::label() const { return "Importer la configuration mat\xC3\xA9rielle " + source_; }

namespace {
Rack* rackOf(Project& p, std::uint16_t number) {
    for (auto& r : p.hardware.racks)
        if (r.number == number) return &r;
    return nullptr;
}
} // namespace

RemoveModuleCommand::RemoveModuleCommand(ApiProjectPtr p, std::uint16_t rack, std::int16_t slot)
    : project_(std::move(p)), rack_(rack), slot_(slot) {}

core::Status RemoveModuleCommand::execute() {
    if (!project_) return noProject();
    auto* rack = rackOf(*project_, rack_);
    if (!rack) return core::fail(core::ErrorCode::OutOfRange, "le rack " + std::to_string(rack_) + " n'existe pas");
    for (std::size_t i = 0; i < rack->modules.size(); ++i) {
        const auto& m = rack->modules[i];
        if (m.slot != slot_) continue;
        // Le processeur : l'emplacement 00 du rack 0 (« toujours installe dans
        // l'emplacement marque 00 »), quel que soit ce que le .XHW en dit.
        if (m.isCpu || m.kind == ModuleKind::Cpu || (rack_ == 0 && slot_ == 0))
            return core::fail(core::ErrorCode::InvalidArgument, "le processeur ne se retire pas");
        if (m.slot < 0 || m.kind == ModuleKind::PowerSupply)
            return core::fail(core::ErrorCode::InvalidArgument, "un rack garde son alimentation");
        removed_ = m;
        position_ = i;
        rack->modules.erase(rack->modules.begin() + static_cast<long long>(i));
        return core::ok();
    }
    return core::fail(core::ErrorCode::OutOfRange, "l'emplacement " + std::to_string(slot_) + " est vide");
}

core::Status RemoveModuleCommand::undo() {
    if (!project_ || !removed_) return core::ok();
    if (auto* rack = rackOf(*project_, rack_))
        rack->modules.insert(rack->modules.begin() + static_cast<long long>(std::min(position_, rack->modules.size())), *removed_);
    removed_.reset();
    return core::ok();
}

std::string RemoveModuleCommand::label() const {
    return "Retirer " + (removed_ ? removed_->reference : std::string("le module")) + " (rack " + std::to_string(rack_)
         + ", emplacement " + std::to_string(slot_) + ")";
}

ReplaceModuleCommand::ReplaceModuleCommand(ApiProjectPtr p, std::uint16_t rack, std::int16_t slot, std::string reference)
    : project_(std::move(p)), rack_(rack), slot_(slot), reference_(std::move(reference)) {}

core::Status ReplaceModuleCommand::execute() {
    if (!project_) return noProject();
    auto* rack = rackOf(*project_, rack_);
    if (!rack) return core::fail(core::ErrorCode::OutOfRange, "le rack " + std::to_string(rack_) + " n'existe pas");
    std::size_t at = rack->modules.size();
    for (std::size_t i = 0; i < rack->modules.size(); ++i)
        if (rack->modules[i].slot == slot_) at = i;
    if (at == rack->modules.size()) return core::fail(core::ErrorCode::OutOfRange, "l'emplacement " + std::to_string(slot_) + " est vide");
    if (rack->modules[at].isCpu || (rack_ == 0 && slot_ == 0))
        return core::fail(core::ErrorCode::InvalidArgument, "le processeur ne se remplace pas ici");
    previous_ = rack->modules[at];
    position_ = at;
    rack->modules.erase(rack->modules.begin() + static_cast<long long>(at));
    // Poser le nouveau comme on pose un module : le catalogue, les limites du processeur.
    AddModuleCommand add(project_, rack_, slot_, reference_);
    if (auto r = add.execute(); !r) {
        rack = rackOf(*project_, rack_);
        if (rack) rack->modules.insert(rack->modules.begin() + static_cast<long long>(std::min(position_, rack->modules.size())), *previous_);
        previous_.reset();
        return r;
    }
    return core::ok();      // AddModuleCommand range les modules par emplacement
}

core::Status ReplaceModuleCommand::undo() {
    if (!project_ || !previous_) return core::ok();
    if (auto* rack = rackOf(*project_, rack_)) {
        for (std::size_t i = 0; i < rack->modules.size(); ++i)
            if (rack->modules[i].slot == slot_) { rack->modules.erase(rack->modules.begin() + static_cast<long long>(i)); break; }
        rack->modules.insert(rack->modules.begin() + static_cast<long long>(std::min(position_, rack->modules.size())), *previous_);
    }
    previous_.reset();
    return core::ok();
}

std::string ReplaceModuleCommand::label() const {
    return "Remplacer " + (previous_ ? previous_->reference : std::string("le module")) + " par " + reference_;
}

} // namespace project
