#include "BlockLibrary.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace project {
namespace {

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return std::string(s);
}

std::vector<std::string> split(std::string_view text, char sep) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto at = text.find(sep, from);
        out.push_back(trim(text.substr(from, (at == std::string_view::npos ? text.size() : at) - from)));
        if (at == std::string_view::npos) break;
        from = at + 1;
    }
    return out;
}

// ">IN:BOOL" -> input, named IN, of type BOOL
std::vector<BlockParameter> parseParameters(std::string_view text) {
    std::vector<BlockParameter> out;
    if (text.empty()) return out;
    for (const auto& piece : split(text, '|')) {
        if (piece.empty()) continue;
        BlockParameter p;
        std::string_view body(piece);
        switch (body.front()) {
            case '>': p.direction = BlockParameter::Direction::In;    body.remove_prefix(1); break;
            case '<': p.direction = BlockParameter::Direction::Out;   body.remove_prefix(1); break;
            case '=': p.direction = BlockParameter::Direction::InOut; body.remove_prefix(1); break;
            default: break;
        }
        const auto colon = body.find(':');
        p.name = trim(body.substr(0, colon));
        if (colon != std::string_view::npos) p.type = trim(body.substr(colon + 1));
        if (!p.name.empty()) out.push_back(std::move(p));
    }
    return out;
}

BlockKind kindFromText(std::string_view s) {
    const auto u = upperOf(s);
    if (u == "EFB")  return BlockKind::FunctionBlock;
    if (u == "PROC") return BlockKind::Procedure;
    return BlockKind::Function;
}

Provenance provenanceFromText(std::string_view s) {
    const auto u = upperOf(s);
    if (u == "STANDARD") return Provenance::Standard;
    if (u == "OBSERVED") return Provenance::Observed;
    return Provenance::Unverified;
}

// Where the resource file lives. The working directory when the application is
// launched from a shortcut is rarely the install directory, so a few obvious
// places are tried rather than one.
std::vector<std::filesystem::path> candidatePaths(const char* file) {
    namespace fs = std::filesystem;
    std::vector<fs::path> out;
    std::error_code ec;
    const auto cwd = fs::current_path(ec);
    out.push_back(cwd / "resources" / file);
    out.push_back(cwd / ".." / "resources" / file);
    out.push_back(cwd / ".." / ".." / "resources" / file);
    out.push_back(fs::path("resources") / file);
    return out;
}

} // namespace

std::string_view BlockParameter::directionLabel() const noexcept {
    switch (direction) {
        case Direction::In:    return "IN ";
        case Direction::Out:   return "OUT";
        case Direction::InOut: return "I/O";
    }
    return "   ";
}

std::string BlockDefinition::signature() const {
    std::string out = name + "(";
    bool first = true;
    for (const auto& p : parameters) {
        if (!first) out += ", ";
        first = false;
        out += std::string(p.directionLabel()) + " " + p.name + " : " + p.type;
    }
    out += ")";
    if (!returns.empty()) out += " : " + returns;
    return out;
}

std::string_view toString(BlockKind k) noexcept {
    switch (k) {
        case BlockKind::FunctionBlock: return "function block";
        case BlockKind::Procedure:     return "procedure";
        case BlockKind::Function:      break;
    }
    return "function";
}

std::string_view toString(Provenance p) noexcept {
    switch (p) {
        case Provenance::Standard:   return "IEC 61131-3";
        case Provenance::Observed:   return "used in this project";
        case Provenance::Unverified: return "unverified";
    }
    return "";
}

// ---------------------------------------------------------------------------
void BlockLibrary::add(BlockDefinition block) {
    const auto key = upperOf(block.name);
    auto it = std::find_if(blocks_.begin(), blocks_.end(),
                           [&](const BlockDefinition& b) { return upperOf(b.name) == key; });
    if (it != blocks_.end()) *it = std::move(block);
    else blocks_.push_back(std::move(block));
}

const BlockDefinition* BlockLibrary::find(std::string_view name) const {
    const auto key = upperOf(name);
    for (const auto& b : blocks_)
        if (upperOf(b.name) == key) return &b;
    return nullptr;
}

std::size_t BlockLibrary::countWith(Provenance p) const {
    return static_cast<std::size_t>(
        std::count_if(blocks_.begin(), blocks_.end(),
                      [p](const BlockDefinition& b) { return b.provenance == p; }));
}

BlockLibrary BlockLibrary::builtin() {
    // Deliberately small: only what is needed for the editor to be useful with
    // no resource file present. The full table lives in the data file, where it
    // can be corrected.
    BlockLibrary lib;
    struct Seed { const char* name; const char* params; const char* ret; BlockKind kind; };
    static constexpr Seed kSeed[] = {
        {"TON",    ">IN:BOOL|>PT:TIME|<Q:BOOL|<ET:TIME", "", BlockKind::FunctionBlock},
        {"TOF",    ">IN:BOOL|>PT:TIME|<Q:BOOL|<ET:TIME", "", BlockKind::FunctionBlock},
        {"TP",     ">IN:BOOL|>PT:TIME|<Q:BOOL|<ET:TIME", "", BlockKind::FunctionBlock},
        {"CTU",    ">CU:BOOL|>R:BOOL|>PV:INT|<Q:BOOL|<CV:INT", "", BlockKind::FunctionBlock},
        {"CTD",    ">CD:BOOL|>LD:BOOL|>PV:INT|<Q:BOOL|<CV:INT", "", BlockKind::FunctionBlock},
        {"R_TRIG", ">CLK:BOOL|<Q:BOOL", "", BlockKind::FunctionBlock},
        {"F_TRIG", ">CLK:BOOL|<Q:BOOL", "", BlockKind::FunctionBlock},
        {"SR",     ">SET1:BOOL|>RESET:BOOL|<Q1:BOOL", "", BlockKind::FunctionBlock},
        {"RS",     ">SET:BOOL|>RESET1:BOOL|<Q1:BOOL", "", BlockKind::FunctionBlock},
        {"SEL",    ">G:BOOL|>IN0:ANY|>IN1:ANY", "ANY", BlockKind::Function},
        {"LIMIT",  ">MN:ANY|>IN:ANY|>MX:ANY", "ANY", BlockKind::Function},
        {"MAX",    ">IN1:ANY|>IN2:ANY", "ANY", BlockKind::Function},
        {"MIN",    ">IN1:ANY|>IN2:ANY", "ANY", BlockKind::Function},
        {"ABS",    ">IN:ANY_NUM", "ANY_NUM", BlockKind::Function},
        {"SQRT",   ">IN:REAL", "REAL", BlockKind::Function},
    };
    for (const auto& s : kSeed) {
        BlockDefinition b;
        b.library     = "Standard";
        b.family      = "Built-in";
        b.kind        = s.kind;
        b.name        = s.name;
        b.parameters  = parseParameters(s.params);
        b.returns     = s.ret;
        b.provenance  = Provenance::Standard;
        b.description = "";
        lib.add(std::move(b));
    }
    return lib;
}

core::Result<BlockLibrary> BlockLibrary::loadFromFile(const std::string& path) {
    auto lib = builtin();

    std::ifstream in(path);
    if (!in) return core::fail(core::ErrorCode::FileNotFound, path);

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#') continue;

        const auto f = split(trimmed, ';');
        if (f.size() < 4) continue;              // a malformed line is skipped, not fatal

        BlockDefinition b;
        b.library     = f[0];
        b.family      = f[1];
        b.kind        = kindFromText(f[2]);
        b.name        = f[3];
        if (f.size() > 4) b.parameters  = parseParameters(f[4]);
        if (f.size() > 5) b.returns     = f[5];
        if (f.size() > 6) b.provenance  = provenanceFromText(f[6]);
        if (f.size() > 7) b.description = f[7];
        if (!b.name.empty()) lib.add(std::move(b));
    }
    return lib;
}

namespace {
BlockLibrary* g_installed = nullptr;
}

void BlockLibrary::installShared(BlockLibrary library) {
    static BlockLibrary storage;
    storage = std::move(library);
    g_installed = &storage;
}

const BlockLibrary& BlockLibrary::shared() {
    if (g_installed) return *g_installed;

    // Nothing installed: search the obvious places, then fall back to the seed.
    // A missing file is not an error, it only means fewer suggestions.
    static const BlockLibrary fallback = [] {
        for (const auto& candidate : candidatePaths("schneider_library.txt")) {
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec)) continue;
            if (auto loaded = BlockLibrary::loadFromFile(candidate.string())) return *loaded;
        }
        return BlockLibrary::builtin();
    }();
    return fallback;
}

} // namespace project
