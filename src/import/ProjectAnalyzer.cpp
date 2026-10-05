#include "ProjectAnalyzer.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string_view>

namespace importer {

using namespace domain;

namespace {

// Hand-rolled ASCII tests rather than std::isalpha/std::isalnum: those are not
// constexpr, and they are locale-dependent, which an IEC 61131-3 identifier is
// not. MSVC rejects the constexpr form outright (C3615).
constexpr bool asciiAlpha(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
constexpr bool asciiDigit(char c) noexcept { return c >= '0' && c <= '9'; }
constexpr bool identStart(char c) noexcept { return asciiAlpha(c) || c == '_'; }
constexpr bool identChar(char c)  noexcept { return asciiAlpha(c) || asciiDigit(c) || c == '_'; }
constexpr char asciiLower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive hash/equality: IEC 61131-3 identifiers are case-insensitive,
// so `Armoires` and `armoires` are the same symbol.
struct CiHash {
    std::size_t operator()(std::string_view s) const noexcept {
        std::size_t h = 1469598103934665603ull;
        for (char c : s) {
            h ^= static_cast<std::size_t>(static_cast<unsigned char>(asciiLower(c)));
            h *= 1099511628211ull;
        }
        return h;
    }
};
struct CiEq {
    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return a.size() == b.size()
            && std::equal(a.begin(), a.end(), b.begin(),
                          [](char x, char y) { return asciiLower(x) == asciiLower(y); });
    }
};
using CiCount = std::unordered_map<std::string_view, std::uint32_t, CiHash, CiEq>;

// One pass over a body, skipping (* comments *), // comments and 'strings'.
void tokenize(std::string_view body, CiCount& out) {
    for (std::size_t i = 0; i < body.size();) {
        const char c = body[i];
        if (c == '(' && i + 1 < body.size() && body[i + 1] == '*') {
            const auto end = body.find("*)", i + 2);
            i = (end == std::string_view::npos) ? body.size() : end + 2;
            continue;
        }
        if (c == '/' && i + 1 < body.size() && body[i + 1] == '/') {
            const auto end = body.find('\n', i);
            i = (end == std::string_view::npos) ? body.size() : end + 1;
            continue;
        }
        if (c == '\'') {
            const auto end = body.find('\'', i + 1);
            i = (end == std::string_view::npos) ? body.size() : end + 1;
            continue;
        }
        if (identStart(c)) {
            const auto start = i;
            while (i < body.size() && identChar(body[i])) ++i;
            ++out[body.substr(start, i - start)];
            continue;
        }
        ++i;
    }
}

} // namespace

ProjectAnalyzer::ReferenceIndex ProjectAnalyzer::buildReferenceIndex(const Project& p) {
    CiCount counts;
    counts.reserve(4096);
    for (const auto& s : p.sections) tokenize(s.body, counts);
    // Activation conditions are references too: a variable used only to gate a
    // section is not dead code.
    for (const auto& s : p.sections) {
        tokenize(p.strings.text(s.activationCondition), counts);
        tokenize(p.strings.text(s.logicCondition), counts);
    }
    // Une ligne de l'IHM n'est pas une reference du programme (lot API 3).
    for (const auto& t : p.animationTables)
        for (const auto& e : t.entries)
            if (!e.hmi) tokenize(p.strings.text(e.name), counts);

    ReferenceIndex index;
    index.reserve(p.variables.size());
    for (const auto& v : p.variables) {
        const auto name = p.strings.text(v.name);
        if (auto it = counts.find(name); it != counts.end()) index[v.name] += it->second;
    }
    // Structured accesses (Armoires[0].prete) tokenise into their parts, so a
    // member reference also credits the parent symbol; nothing more to do here.
    return index;
}

core::Result<AnalysisReport> ProjectAnalyzer::analyze(const Project& p,
                                                      const AnalysisOptions& opt,
                                                      const ProgressFn& progress,
                                                      const std::atomic_bool* cancel) const {
    // Checked before a single element is read: an inconsistent build must
    // produce this sentence, not a walk off the end of an array.
    if (!p.layoutMatches())
        return core::fail(core::ErrorCode::IncompleteProject, p.layoutMismatchMessage());

    const auto t0 = std::chrono::steady_clock::now();
    AnalysisReport rep;

    auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    auto step = [&](float f, std::string_view s) { if (progress) progress(f, s); };

    // ---- counters ---------------------------------------------------------
    step(0.05f, "counting declarations");
    rep.totalVariables = static_cast<std::uint32_t>(p.variables.size());
    for (const auto& v : p.variables) {
        switch (v.scope) {
            case VariableScope::Global:        ++rep.globalVariables; break;
            case VariableScope::Constant:      ++rep.constants; break;
            case VariableScope::DerivedMember: ++rep.derivedTypeFields; break;
            default:                           ++rep.localVariables; break;
        }
        if (v.located) ++rep.locatedVariables;
        if (v.comment != 0) ++rep.commentedVariables;
        if (v.type.klass == TypeClass::FunctionBlock && v.type.fbTypeIndex != kNoIndex)
            ++rep.dfbInstances;
    }
    rep.derivedTypes    = static_cast<std::uint32_t>(p.derivedTypes.size());
    rep.sections        = static_cast<std::uint32_t>(p.sections.size());
    rep.tasks           = static_cast<std::uint32_t>(p.tasks.size());
    rep.animationTables = static_cast<std::uint32_t>(p.animationTables.size());
    for (const auto& pou : p.pous) {
        if (pou.kind == PouKind::FunctionBlockType) ++rep.functionBlockTypes;
        if (pou.kind == PouKind::ProgramUnit)       ++rep.programUnits;
    }
    for (const auto& s : p.sections) {
        rep.linesOfCode += s.lineCount;
        rep.statements  += s.statementCount;
    }
    if (cancelled()) return core::fail(core::ErrorCode::Cancelled, "analysis cancelled");

    // ---- language breakdown ----------------------------------------------
    step(0.2f, "language breakdown");
    std::unordered_map<int, LanguageBreakdown> langs;
    for (const auto& s : p.sections) {
        auto& e = langs[static_cast<int>(s.language)];
        e.language = s.language;
        ++e.sections;
        e.lines += s.lineCount;
    }
    for (auto& [_, v] : langs) rep.byLanguage.push_back(v);
    std::sort(rep.byLanguage.begin(), rep.byLanguage.end(),
              [](const auto& a, const auto& b) { return a.lines > b.lines; });

    // ---- top-N tables -----------------------------------------------------
    step(0.35f, "ranking");
    {
        std::unordered_map<SymbolId, std::uint32_t> typeUse;
        for (const auto& v : p.variables) ++typeUse[v.type.name];
        std::vector<std::pair<std::string, std::uint32_t>> tmp;
        tmp.reserve(typeUse.size());
        for (auto [id, n] : typeUse) tmp.emplace_back(std::string(p.strings.text(id)), n);
        std::sort(tmp.begin(), tmp.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        tmp.resize(std::min<std::size_t>(tmp.size(), opt.topN));
        rep.topTypes = std::move(tmp);
    }
    {
        std::vector<std::pair<std::string, std::uint32_t>> tmp;
        tmp.reserve(p.sections.size());
        for (const auto& s : p.sections) tmp.emplace_back(std::string(p.strings.text(s.name)), s.lineCount);
        std::sort(tmp.begin(), tmp.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        tmp.resize(std::min<std::size_t>(tmp.size(), opt.topN));
        rep.largestSections = std::move(tmp);
    }
    {
        std::vector<std::pair<std::string, std::uint32_t>> tmp;
        for (const auto& pou : p.pous)
            if (pou.kind == PouKind::FunctionBlockType)
                tmp.emplace_back(std::string(p.strings.text(pou.name)), pou.instanceCount);
        std::sort(tmp.begin(), tmp.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        tmp.resize(std::min<std::size_t>(tmp.size(), opt.topN));
        rep.mostUsedDfbs = std::move(tmp);
    }
    // ---- type usage and memory -------------------------------------------
    step(0.45f, "sizing types");
    {
        struct Bucket { TypeUsage u; };
        std::unordered_map<SymbolId, Bucket> buckets;
        buckets.reserve(256);

        for (const auto& v : p.variables) {
            auto& b = buckets[v.type.name];
            if (b.u.name.empty()) {
                b.u.name     = std::string(p.strings.text(v.type.name));
                b.u.klass    = v.type.klass;
                b.u.bitsEach = typeSizeInBits(p, v.type);
                b.u.resolved = b.u.bitsEach != 0;
            }
            ++b.u.declarations;
            b.u.bitsTotal += b.u.bitsEach;

            // Members of a DDT are counted once through the type itself, not
            // once per instance, or a 5-instance DDT would be counted twice.
            if (v.scope == VariableScope::DerivedMember) continue;

            rep.memory.totalBits += b.u.bitsEach;
            switch (v.scope) {
                case VariableScope::Global:   rep.memory.globalBits += b.u.bitsEach; break;
                case VariableScope::Input:
                case VariableScope::Output:
                case VariableScope::InOut:    rep.memory.parameterBits += b.u.bitsEach; break;
                default:                      rep.memory.localBits += b.u.bitsEach; break;
            }
            if (v.located) rep.memory.locatedBits += b.u.bitsEach;
            if (b.u.bitsEach == 0) ++rep.memory.unresolvedTypes;
            if (v.type.klass == TypeClass::FunctionBlock) rep.memory.dfbInstanceBits += b.u.bitsEach;
        }

        for (Index i = 0; i < p.derivedTypes.size(); ++i)
            rep.memory.derivedTypeBits += derivedTypeSizeInBits(p, i);

        rep.typeUsage.reserve(buckets.size());
        for (auto& [id, b] : buckets) {
            if (auto it = p.typeByName.find(id); it != p.typeByName.end())
                b.u.instances = p.derivedTypes[it->second].instanceCount;
            else if (auto ip = p.pouByName.find(id); ip != p.pouByName.end())
                b.u.instances = p.pous[ip->second].instanceCount;
            rep.typeUsage.push_back(std::move(b.u));
        }
        // Heaviest first: that is the order someone hunting for memory reads in.
        std::sort(rep.typeUsage.begin(), rep.typeUsage.end(),
                  [](const TypeUsage& a, const TypeUsage& b) {
                      if (a.bitsTotal != b.bitsTotal) return a.bitsTotal > b.bitsTotal;
                      return a.declarations > b.declarations;
                  });
    }
    if (cancelled()) return core::fail(core::ErrorCode::Cancelled, "analysis cancelled");

    // ---- cross-reference --------------------------------------------------
    step(0.55f, "cross-referencing");
    const auto refs = buildReferenceIndex(p);

    if (opt.detectUnusedVariables) {
        for (Index i = 0; i < p.variables.size(); ++i) {
            const auto& v = p.variables[i];
            if (v.scope == VariableScope::DerivedMember) continue;   // usage is per instance
            if (refs.find(v.name) != refs.end()) continue;
            rep.findings.push_back(Finding{
                v.scope == VariableScope::Global ? FindingKind::UnusedGlobalVariable
                                                 : FindingKind::UnusedLocalVariable,
                std::string(p.strings.text(v.name)),
                v.located ? "located at " + v.address.raw + " but never read or written"
                          : "declared but never referenced in any section",
                i,
                v.located ? Finding::Severity::Warning : Finding::Severity::Info});
        }
    }

    for (Index i = 0; i < p.derivedTypes.size(); ++i)
        if (p.derivedTypes[i].instanceCount == 0)
            rep.findings.push_back(Finding{FindingKind::UnusedDerivedType,
                                           std::string(p.strings.text(p.derivedTypes[i].name)),
                                           "no variable declares this type", i,
                                           Finding::Severity::Info});

    for (Index i = 0; i < p.pous.size(); ++i) {
        const auto& pou = p.pous[i];
        if (pou.kind == PouKind::FunctionBlockType && pou.instanceCount == 0)
            rep.findings.push_back(Finding{FindingKind::UnusedFunctionBlockType,
                                           std::string(p.strings.text(pou.name)),
                                           "the DFB type has no instance", i,
                                           Finding::Severity::Warning});
    }

    if (opt.detectUnusedSections) {
        for (Index i = 0; i < p.sections.size(); ++i) {
            const auto& s = p.sections[i];
            const auto  name = std::string(p.strings.text(s.name));
            if (s.lineCount == 0 || s.statementCount == 0)
                rep.findings.push_back(Finding{FindingKind::EmptySection, name,
                                               "the section contains no executable statement", i,
                                               Finding::Severity::Warning});
            if (s.lineCount > opt.longSectionThreshold)
                rep.findings.push_back(Finding{FindingKind::LongSection, name,
                                               std::to_string(s.lineCount) + " lines exceeds the "
                                                   + std::to_string(opt.longSectionThreshold)
                                                   + "-line review threshold",
                                               i, Finding::Severity::Info});
        }
    }

    if (opt.detectDuplicateAddresses) {
        step(0.85f, "checking addresses");
        std::unordered_map<std::string, Index> byAddress;
        for (Index i = 0; i < p.variables.size(); ++i) {
            const auto& v = p.variables[i];
            if (!v.located) continue;
            auto [it, inserted] = byAddress.emplace(v.address.raw, i);
            if (!inserted)
                rep.findings.push_back(Finding{
                    FindingKind::DuplicateAddress, std::string(p.strings.text(v.name)),
                    "shares " + v.address.raw + " with '"
                        + std::string(p.strings.text(p.variables[it->second].name)) + "'",
                    i, Finding::Severity::Error});
        }
    }

    if (opt.requireCommentsOnIo)
        for (Index i = 0; i < p.variables.size(); ++i) {
            const auto& v = p.variables[i];
            if (v.located && v.comment == 0)
                rep.findings.push_back(Finding{FindingKind::MissingComment,
                                               std::string(p.strings.text(v.name)),
                                               "located I/O without a comment", i,
                                               Finding::Severity::Info});
        }

    std::stable_sort(rep.findings.begin(), rep.findings.end(),
                     [](const Finding& a, const Finding& b) { return a.severity > b.severity; });

    rep.analysisMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    step(1.f, "done");
    return rep;
}

std::uint32_t AnalysisReport::count(FindingKind k) const {
    return static_cast<std::uint32_t>(
        std::count_if(findings.begin(), findings.end(),
                      [k](const Finding& f) { return f.kind == k; }));
}

std::string_view toString(FindingKind k) noexcept {
    switch (k) {
        case FindingKind::UnusedGlobalVariable:    return "Unused global variable";
        case FindingKind::UnusedLocalVariable:     return "Unused local variable";
        case FindingKind::UnusedDerivedType:       return "Unused derived type";
        case FindingKind::UnusedFunctionBlockType: return "Unused DFB type";
        case FindingKind::UnreferencedSection:     return "Unreferenced section";
        case FindingKind::EmptySection:            return "Empty section";
        case FindingKind::UnconditionalSection:    return "Unconditional section";
        case FindingKind::DuplicateAddress:        return "Duplicate address";
        case FindingKind::UndefinedType:           return "Undefined type";
        case FindingKind::MissingComment:          return "Missing comment";
        case FindingKind::LongSection:             return "Long section";
    }
    return "Finding";
}

} // namespace importer
