// =============================================================================
//  project/BlockLibrary.hpp — the functions and blocks Control Expert provides
// -----------------------------------------------------------------------------
//  A project's own DFBs come out of the export. The blocks the platform provides
//  do not: TON, INT_TO_REAL and the rest exist inside Control Expert, and a file
//  that uses them only records the name. So they have to come from somewhere
//  else, and that somewhere is a data file rather than a table compiled in.
//
//  EVERY ENTRY CARRIES ITS PROVENANCE. `Status` says whether a definition comes
//  from IEC 61131-3, from the user's own project code, or from a plausible guess
//  at Control Expert's naming. The editor shows it, because a signature the tool
//  invented and a signature it knows are not the same claim - a lesson from the
//  hardware catalogue, where a point count inferred from a part number was wrong
//  and nothing on screen said it had been inferred.
// =============================================================================
#pragma once

#include "../core/Result.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace project {

enum class BlockKind : std::uint8_t {
    Function,       // EF: one result, no state
    FunctionBlock,  // EFB: has state, needs an instance
    Procedure,      // several outputs, no state
};

enum class Provenance : std::uint8_t {
    Standard,    // IEC 61131-3
    Observed,    // seen in the user's own exported program
    Unverified,  // follows the naming convention, not confirmed
};

struct BlockParameter {
    enum class Direction : std::uint8_t { In, Out, InOut } direction{Direction::In};
    std::string name;
    std::string type;

    [[nodiscard]] std::string_view directionLabel() const noexcept;
};

struct BlockDefinition {
    std::string                 library;      // Base, Standard, Communication...
    std::string                 family;       // Timers, Conversion, String...
    BlockKind                   kind{BlockKind::Function};
    std::string                 name;
    std::vector<BlockParameter> parameters;
    std::string                 returns;      // empty for a function block
    Provenance                  provenance{Provenance::Unverified};
    std::string                 description;

    [[nodiscard]] std::string signature() const;   // "TON(IN : BOOL, PT : TIME)"
};

class BlockLibrary {
public:
    // The built-in set, then resources/schneider_library.txt on top of it. An
    // entry in the file replaces the built-in one with the same name, so the
    // table can be corrected without a rebuild.
    // The application installs the library it loaded, because the working
    // directory when a program is launched from a shortcut is not the install
    // directory and searching relative to it is a coin toss. Without an
    // installed one, shared() falls back to searching and then to the built-in
    // seed, so nothing ever ends up with no library at all.
    static void               installShared(BlockLibrary library);
    static const BlockLibrary& shared();
    static BlockLibrary        builtin();
    [[nodiscard]] static core::Result<BlockLibrary> loadFromFile(const std::string& path);

    [[nodiscard]] const BlockDefinition* find(std::string_view name) const;   // case-insensitive
    [[nodiscard]] const std::vector<BlockDefinition>& all() const noexcept { return blocks_; }
    [[nodiscard]] std::size_t size() const noexcept { return blocks_.size(); }
    [[nodiscard]] std::size_t countWith(Provenance) const;

    void add(BlockDefinition block);

private:
    std::vector<BlockDefinition> blocks_;
};

[[nodiscard]] std::string_view toString(BlockKind) noexcept;
[[nodiscard]] std::string_view toString(Provenance) noexcept;

} // namespace project
