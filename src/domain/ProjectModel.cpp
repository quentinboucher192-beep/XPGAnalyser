#include "ProjectModel.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>

namespace domain {

    // --------------------------------------------------------------- StringPool ---
    SymbolId StringPool::intern(std::string_view s) {
        if (auto it = lookup_.find(s); it != lookup_.end()) return it->second;
        // Index 0 is reserved for "empty", so callers can treat 0 as "unset".
        if (storage_.empty()) { storage_.emplace_back(); lookup_.emplace(std::string_view{}, 0); }
        if (s.empty()) return 0;
        storage_.emplace_back(s);
        const auto id = static_cast<SymbolId>(storage_.size() - 1);
        lookup_.emplace(std::string_view(storage_.back()), id);
        return id;
    }

    std::string_view StringPool::text(SymbolId id) const {
        return id < storage_.size() ? std::string_view(storage_[id]) : std::string_view{};
    }

    // ------------------------------------------------------------------ Address ---
    bool Address::operator<(const Address& o) const noexcept {
        if (area != o.area)     return area < o.area;
        if (offset != o.offset) return offset < o.offset;
        return bit.value_or(0) < o.bit.value_or(0);
    }

    Address Address::parse(std::string_view s) {
        Address a;
        a.raw = std::string(s);
        if (s.size() < 2 || s[0] != '%') return a;

        std::size_t i = 1;
        std::string letters;
        while (i < s.size() && (std::isalpha(static_cast<unsigned char>(s[i])) != 0))
            letters.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(s[i++]))));

        if (letters == "M")  a.area = MemoryArea::Internal;
        else if (letters == "MW" || letters == "MD" || letters == "MF") a.area = MemoryArea::InternalWord;
        else if (letters == "I")  a.area = MemoryArea::Input;
        else if (letters == "IW") a.area = MemoryArea::InputWord;
        else if (letters == "Q")  a.area = MemoryArea::Output;
        else if (letters == "QW") a.area = MemoryArea::OutputWord;
        else if (letters == "S")  a.area = MemoryArea::System;
        else if (letters == "SW" || letters == "SD") a.area = MemoryArea::SystemWord;
        else if (letters == "K")  a.area = MemoryArea::Constant;
        else if (letters == "KW" || letters == "KD" || letters == "KF") a.area = MemoryArea::ConstantWord;
        else if (letters == "CH") a.area = MemoryArea::Topological;
        else return a;

        // Numeric part; may be dotted (%I0.3 or %CH0.2.1).
        std::vector<std::uint32_t> parts;
        while (i < s.size()) {
            std::uint32_t v = 0;
            const auto* first = s.data() + i;
            const auto* last = s.data() + s.size();
            const auto  res = std::from_chars(first, last, v);
            if (res.ec != std::errc{}) break;
            parts.push_back(v);
            i = static_cast<std::size_t>(res.ptr - s.data());
            if (i < s.size() && s[i] == '.') ++i; else break;
        }
        if (parts.empty()) { a.area = MemoryArea::None; return a; }
        a.offset = parts[0];
        if (parts.size() > 1) a.bit = static_cast<std::uint8_t>(parts[1]);
        if (a.area == MemoryArea::Topological && parts.size() >= 3) {
            // rack.slot.channel: keep slot in offset, channel in bit for sorting.
            a.offset = parts[1];
            a.bit = static_cast<std::uint8_t>(parts[2]);
        }
        return a;
    }

    // ------------------------------------------------------------------ Project ---
    void Project::buildIndices() {
        variableByName.clear();
        typeByName.clear();
        pouByName.clear();
        variableByName.reserve(variables.size());
        for (Index i = 0; i < variables.size(); ++i)
            if (variables[i].scope == VariableScope::Global)
                variableByName.emplace(variables[i].name, i);
        for (Index i = 0; i < derivedTypes.size(); ++i) typeByName.emplace(derivedTypes[i].name, i);
        for (Index i = 0; i < pous.size(); ++i)         pouByName.emplace(pous[i].name, i);
    }

    void Project::clear() {
        header = {};
        hardware = {};
        variables.clear(); derivedTypes.clear(); pous.clear(); sections.clear();
        libraries.clear(); tasks.clear(); animationTables.clear();
        memoryWindows = {};
        icon = {};
        codeIcons.clear();
        variableByName.clear(); typeByName.clear(); pouByName.clear();
        partialDataNotices.clear();
    }

    std::size_t Project::memoryFootprint() const {
        std::size_t n = variables.capacity() * sizeof(Variable)
            + derivedTypes.capacity() * sizeof(DerivedType)
            + pous.capacity() * sizeof(Pou)
            + sections.capacity() * sizeof(Section);
        for (const auto& s : sections) n += s.body.capacity();
        return n;
    }

    // ------------------------------------------------------------------- sizing ---
    namespace {

        struct Elementary { std::string_view name; std::uint32_t bits; };

        // Sorted by nothing in particular; the table is short enough that a linear scan
        // is faster than anything with a hash.
        constexpr Elementary kElementaryBits[] = {
            {"BOOL", 1},   {"EBOOL", 1},
            {"BYTE", 8},   {"SINT", 8},   {"USINT", 8},
            {"WORD", 16},  {"INT", 16},   {"UINT", 16},
            {"DWORD", 32}, {"DINT", 32},  {"UDINT", 32}, {"REAL", 32},
            {"TIME", 32},  {"DATE", 32},  {"TOD", 32},
            {"LWORD", 64}, {"LINT", 64},  {"ULINT", 64}, {"LREAL", 64}, {"DT", 64},
        };

        std::string upperOf(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }

        constexpr std::uint32_t alignTo(std::uint32_t bits, std::uint32_t boundary) {
            const auto rem = bits % boundary;
            return rem ? bits + (boundary - rem) : bits;
        }

    } // namespace

    std::uint32_t elementaryBits(std::string_view typeName) noexcept {
        const auto upper = upperOf(typeName);

        // STRING[32] -> 32 characters plus a length byte and a terminator.
        if (upper.rfind("STRING", 0) == 0) {
            std::uint32_t chars = 16;                 // Control Expert's default
            if (const auto lb = upper.find('['); lb != std::string::npos)
                std::from_chars(upper.data() + lb + 1, upper.data() + upper.size(), chars);
            return (chars + 2) * 8;
        }
        for (const auto& e : kElementaryBits)
            if (upper == e.name) return e.bits;
        return 0;                                      // not elementary
    }

    void Project::linkTypes() {
        buildIndices();
        for (auto& d : derivedTypes) d.instanceCount = 0;
        for (auto& pou : pous) pou.instanceCount = 0;

        for (auto& v : variables) {
            const auto elemName = v.type.elementType ? v.type.elementType : v.type.name;
            v.type.derivedIndex = kNoIndex;
            v.type.fbTypeIndex = kNoIndex;

            if (auto it = typeByName.find(elemName); it != typeByName.end()) {
                v.type.derivedIndex = it->second;
                if (v.type.klass == TypeClass::Unknown) v.type.klass = TypeClass::Derived;
                derivedTypes[it->second].instanceCount++;
            }
            else if (auto ip = pouByName.find(elemName); ip != pouByName.end()
                && pous[ip->second].kind == PouKind::FunctionBlockType) {
                v.type.fbTypeIndex = ip->second;
                v.type.klass = TypeClass::FunctionBlock;
                pous[ip->second].instanceCount++;
            }
        }
    }

    std::uint32_t derivedTypeSizeInBits(const Project& p, Index derivedIndex) {
        if (derivedIndex >= p.derivedTypes.size()) return 0;

        std::uint32_t bits = 0;
        for (auto fieldIndex : p.derivedTypes[derivedIndex].fields) {
            const auto& field = p.variables[fieldIndex];
            const auto  fieldBits = typeSizeInBits(p, field.type);
            if (fieldBits == 1) {
                ++bits;                                // bits pack into the current word
            }
            else {
                bits = alignTo(bits, 16) + fieldBits;  // everything else is word-aligned
            }
        }
        return alignTo(bits, 16);
    }

    std::uint32_t typeSizeInBits(const Project& p, const TypeRef& t) {
        const auto name = p.strings.text(t.name);

        if (t.klass == TypeClass::Array) {
            const auto count = (t.arrayHigh >= t.arrayLow)
                ? static_cast<std::uint64_t>(t.arrayHigh - t.arrayLow + 1)
                : 0ull;
            const auto element = t.elementType ? p.strings.text(t.elementType) : std::string_view{};
            std::uint32_t elementBits = elementaryBits(element);
            if (elementBits == 0) {
                const auto it = p.typeByName.find(t.elementType);
                if (it != p.typeByName.end()) elementBits = derivedTypeSizeInBits(p, it->second);
            }
            // An array of bits still allocates whole words.
            if (elementBits == 1) return alignTo(static_cast<std::uint32_t>(count), 16);
            return static_cast<std::uint32_t>(count * elementBits);
        }

        if (const auto bits = elementaryBits(name); bits != 0) return bits;
        if (t.derivedIndex != kNoIndex) return derivedTypeSizeInBits(p, t.derivedIndex);

        // A DFB instance's footprint is the sum of its own declarations. Not exact
        // (the firmware adds bookkeeping) but the right order of magnitude, and far
        // better than reporting zero.
        if (t.fbTypeIndex != kNoIndex) {
            std::uint32_t bits = 0;
            const auto& pou = p.pous[t.fbTypeIndex];
            for (auto vi : pou.parameters) bits = alignTo(bits, 16) + typeSizeInBits(p, p.variables[vi].type);
            for (auto vi : pou.locals)     bits = alignTo(bits, 16) + typeSizeInBits(p, p.variables[vi].type);
            return alignTo(bits, 16);
        }
        return 0;
    }

    std::string Project::layoutMismatchMessage() const {
        const LayoutStamp mine{};
        char buf[320];
        std::snprintf(buf, sizeof buf,
            "This build is inconsistent: parts of it were compiled against a different "
            "version of the data model. Variable %u vs %u bytes, POU %u vs %u, "
            "Section %u vs %u. Delete the build directory and rebuild from scratch.",
            layout.variableBytes, mine.variableBytes,
            layout.pouBytes, mine.pouBytes,
            layout.sectionBytes, mine.sectionBytes);
        return buf;
    }

    // ----------------------------------------------------------------- hardware ---
    std::uint16_t HardwareConfig::totalModules() const {
        std::uint16_t n = 0;
        for (const auto& r : racks)
            for (const auto& m : r.modules)
                if (m.kind != ModuleKind::PowerSupply) ++n;
        return n;
    }

    std::uint16_t HardwareConfig::totalInputPoints() const {
        std::uint16_t n = 0;
        for (const auto& r : racks) for (const auto& m : r.modules) n = static_cast<std::uint16_t>(n + m.inputPoints);
        return n;
    }

    std::uint16_t HardwareConfig::totalOutputPoints() const {
        std::uint16_t n = 0;
        for (const auto& r : racks) for (const auto& m : r.modules) n = static_cast<std::uint16_t>(n + m.outputPoints);
        return n;
    }

    std::string_view toString(ModuleKind k) noexcept {
        switch (k) {
        case ModuleKind::Cpu:            return "CPU";
        case ModuleKind::PowerSupply:    return "Power supply";
        case ModuleKind::DiscreteInput:  return "Discrete input";
        case ModuleKind::DiscreteOutput: return "Discrete output";
        case ModuleKind::DiscreteMixed:  return "Discrete mixed";
        case ModuleKind::AnalogInput:    return "Analog input";
        case ModuleKind::AnalogOutput:   return "Analog output";
        case ModuleKind::Communication:  return "Communication";
        case ModuleKind::Counting:       return "Counting";
        case ModuleKind::Motion:         return "Motion";
        case ModuleKind::Rack:           return "Rack";
        case ModuleKind::Extension:      return "Extension";
        case ModuleKind::Unknown:        break;
        }
        return "Module";
    }

    std::string_view toString(ChannelDirection d) noexcept {
        switch (d) {
        case ChannelDirection::Input:         return "Input";
        case ChannelDirection::Output:        return "Output";
        case ChannelDirection::Communication: return "Communication";
        case ChannelDirection::Unknown:       break;
        }
        return "-";
    }

    // ------------------------------------------------------------------ helpers ---
    PouLanguage languageFromElement(std::string_view e) noexcept {
        if (e == "STSource")  return PouLanguage::ST;
        if (e == "ILSource")  return PouLanguage::IL;
        if (e == "LDSource")  return PouLanguage::LD;
        if (e == "FBDSource") return PouLanguage::FBD;
        if (e == "SFCSource" || e == "SFCChart") return PouLanguage::SFC;
        return PouLanguage::Unknown;
    }

    std::string_view toString(PouLanguage l) noexcept {
        switch (l) {
        case PouLanguage::ST:  return "ST";
        case PouLanguage::IL:  return "IL";
        case PouLanguage::LD:  return "LD";
        case PouLanguage::FBD: return "FBD";
        case PouLanguage::SFC: return "SFC";
        case PouLanguage::Unknown: break;
        }
        return "-";
    }

    std::string_view toString(VariableScope s) noexcept {
        switch (s) {
        case VariableScope::Global:        return "Global";
        case VariableScope::Local:         return "Local";
        case VariableScope::Public:        return "Public";
        case VariableScope::Input:         return "Input";
        case VariableScope::Output:        return "Output";
        case VariableScope::InOut:         return "InOut";
        case VariableScope::Constant:      return "Constant";
        case VariableScope::DerivedMember: return "Member";
        }
        return "?";
    }

    std::string_view toString(MemoryArea a) noexcept {
        switch (a) {
        case MemoryArea::None:         return "";
        case MemoryArea::Internal:     return "%M";
        case MemoryArea::InternalWord: return "%MW";
        case MemoryArea::Input:        return "%I";
        case MemoryArea::InputWord:    return "%IW";
        case MemoryArea::Output:       return "%Q";
        case MemoryArea::OutputWord:   return "%QW";
        case MemoryArea::System:       return "%S";
        case MemoryArea::SystemWord:   return "%SW";
        case MemoryArea::Constant:     return "%K";
        case MemoryArea::ConstantWord: return "%KW";
        case MemoryArea::Topological:  return "%CH";
        }
        return "";
    }

} // namespace domain