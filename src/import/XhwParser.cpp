// =============================================================================
//  import/XhwParser.cpp — the hardware configuration export
// -----------------------------------------------------------------------------
//  WHAT THE FILE ACTUALLY GIVES YOU, AND WHAT IT DOESN'T
//
//  Root <IOExchangeFile>, DTD 41. The structure is:
//
//    IOConf/PLC                  data-memory sizing (%M, %MW, %KW), run flags
//      partItem                  the CPU: BMXP342020, version 03.50
//      configATS/busATS          the backplane bus ("XBusMicro")
//        rackATS                 one per rack, with its own partItem
//          powerSupply/partItem  BMXCPS2000, position -1
//          moduleATS             one per slot
//            partItem            reference, family, firmware
//            equipInfo           topological address and slot position
//            configModule
//              channelATS        one per configured channel group
//                descFB, descIOB, paramKW, paramKPW
//
//  Channel COUNTS are derivable from the file and are therefore taken from it,
//  which is always safer than trusting a table: a discrete module declares its
//  points in groups of eight (number = 0, 8, 16, 24 -> 32 points) and an analog
//  module declares them singly (number = 0..7 -> 8 channels). The stride between
//  consecutive groups of the same direction gives the group size.
//
//  That derivation FAILS in exactly one shape, and this project contains it:
//  BMXDDM16022 declares one input group at 0 and one output group at 16. With a
//  single group per direction there is no stride to measure. This is the honest
//  answer to "do you need a list of existing cards": mostly no - and precisely
//  here, yes. The catalog fills that gap and nothing else, and every figure it
//  supplies is flagged so the UI can say where the number came from.
// =============================================================================
#include "ProjectParser.hpp"
#include "XmlReader.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>

namespace importer {
namespace {

using namespace domain;

bool containsCi(std::string_view haystack, std::string_view needle) {
    if (needle.size() > haystack.size()) return false;
    const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                               [](char a, char b) {
                                   return std::tolower(static_cast<unsigned char>(a))
                                       == std::tolower(static_cast<unsigned char>(b));
                               });
    return it != haystack.end();
}

// The ASFCatKey names the discipline and the direction:
//   BasicInRackTORInSTD_MASTER                -> discrete input
//   BaInRackTOROutReactivateAndFallBack...    -> discrete output
//   BasicInRackANAInL2HighLevel0              -> analog input
//   EthernetCIP_..., ModbusSerialPort         -> communication
// "InRack" appears in nearly all of them, so the test has to look for the
// discipline token, not for a bare "In".
ChannelDirection directionFromRole(std::string_view role) {
    if (containsCi(role, "TOROut") || containsCi(role, "ANAOut")) return ChannelDirection::Output;
    if (containsCi(role, "TORIn")  || containsCi(role, "ANAIn"))  return ChannelDirection::Input;
    if (containsCi(role, "Ethernet") || containsCi(role, "Modbus") || containsCi(role, "Serial")
        || containsCi(role, "CANopen"))
        return ChannelDirection::Communication;
    return ChannelDirection::Unknown;
}

std::uint32_t parseHex(std::string_view text) {
    std::uint32_t v = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        std::from_chars(text.data() + 2, text.data() + text.size(), v, 16);
    else
        std::from_chars(text.data(), text.data() + text.size(), v, 16);
    return v;
}

int parseInt(std::string_view text, int fallback = 0) {
    int v = fallback;
    if (text.empty()) return fallback;
    const auto* first = text.data();
    if (*first == '+') ++first;
    std::from_chars(first, text.data() + text.size(), v);
    return v;
}

// Point count for one direction, from the channel numbers alone.
std::uint16_t derivePoints(const std::vector<Channel>& channels, ChannelDirection want,
                           bool& ambiguous) {
    std::vector<std::uint16_t> numbers;
    for (const auto& c : channels)
        if (c.direction == want) numbers.push_back(c.number);
    if (numbers.empty()) return 0;

    std::sort(numbers.begin(), numbers.end());
    if (numbers.size() == 1) {
        // One group: the stride is unmeasurable. Analog modules genuinely have
        // one channel here; discrete ones have eight. Flag it and let the
        // catalog decide.
        ambiguous = true;
        return 1;
    }
    const auto stride = static_cast<std::uint16_t>(numbers[1] - numbers[0]);
    return static_cast<std::uint16_t>(numbers.back() + (stride ? stride : 1));
}

ModuleKind kindFor(std::string_view family, const std::vector<Channel>& channels, bool isCpu) {
    if (isCpu) return ModuleKind::Cpu;
    if (containsCi(family, "Supply")) return ModuleKind::PowerSupply;
    if (containsCi(family, "Rack"))   return ModuleKind::Rack;
    if (containsCi(family, "Communication")) return ModuleKind::Communication;

    bool in = false, out = false;
    for (const auto& c : channels) {
        in  = in  || c.direction == ChannelDirection::Input;
        out = out || c.direction == ChannelDirection::Output;
    }
    if (containsCi(family, "Analog"))
        return out ? (in ? ModuleKind::DiscreteMixed : ModuleKind::AnalogOutput)
                   : ModuleKind::AnalogInput;
    if (containsCi(family, "Discrete")) {
        if (in && out) return ModuleKind::DiscreteMixed;
        return out ? ModuleKind::DiscreteOutput : ModuleKind::DiscreteInput;
    }
    if (containsCi(family, "Counting")) return ModuleKind::Counting;
    if (containsCi(family, "Motion"))   return ModuleKind::Motion;
    return ModuleKind::Unknown;
}

} // namespace

// ---------------------------------------------------------------------------
bool XhwParser::canParse(std::string_view head) const noexcept {
    return head.find("IOExchangeFile") != std::string_view::npos
        || head.find("HWExchangeFile") != std::string_view::npos;
}

core::Result<ParseOutcome> XhwParser::parse(std::string_view buffer,
                                            Project& p,
                                            const ParseOptions& opts,
                                            const ProgressFn& progress) {
    if (!p.layoutMatches())
        return core::fail(core::ErrorCode::IncompleteProject, p.layoutMismatchMessage());

    const auto t0 = std::chrono::steady_clock::now();
    ParseOutcome out;
    XmlReader r(buffer);
    const auto catalog = HardwareCatalog::builtinFallback();

    auto note = [&](ParseDiagnostic::Level lvl, std::string msg) {
        out.diagnostics.push_back(ParseDiagnostic{lvl, std::move(msg), r.line(), "CONFIG.XHW"});
    };

    for (;;) {
        auto ev = r.next();
        if (!ev) return core::Err<core::Error>(ev.error());
        if (*ev == XmlEvent::EndOfDocument)
            return core::fail(core::ErrorCode::XmlUnexpectedRoot, "no root element");
        if (*ev == XmlEvent::StartElement) break;
    }
    if (r.name() != "IOExchangeFile" && r.name() != "HWExchangeFile")
        return core::fail(core::ErrorCode::XmlUnexpectedRoot,
                          "root is <" + std::string(r.name()) + ">, expected <IOExchangeFile>");

    // Context for the streaming pass.
    enum class Scope { None, Plc, Rack, PowerSupply, Module };
    Scope scope = Scope::None;
    Rack    currentRack;
    Module  currentModule;
    Channel currentChannel;
    bool    haveRack = false, haveModule = false, haveChannel = false;
    bool    inParamKPW = false;
    std::vector<Rack> racks;
    const std::string xpgFirmware = p.hardware.cpuFirmware;   // may be empty

    auto flushChannel = [&] {
        if (!haveChannel) return;
        currentModule.channels.push_back(std::move(currentChannel));
        currentChannel = Channel{};
        haveChannel = false;
    };

    auto flushModule = [&] {
        if (!haveModule) return;
        flushChannel();

        currentModule.kind = kindFor(currentModule.family, currentModule.channels,
                                     currentModule.isCpu);

        bool ambiguousIn = false, ambiguousOut = false;
        currentModule.inputPoints  = derivePoints(currentModule.channels,
                                                  ChannelDirection::Input, ambiguousIn);
        currentModule.outputPoints = derivePoints(currentModule.channels,
                                                  ChannelDirection::Output, ambiguousOut);

        if (const auto* entry = catalog.findModule(currentModule.reference)) {
            currentModule.knownReference = true;
            currentModule.description     = entry->description;
            // The catalog only overrides what the file could not settle.
            if (ambiguousIn && entry->inputPoints) {
                currentModule.inputPoints = entry->inputPoints;
                currentModule.pointsFromCatalog = true;
            }
            if (ambiguousOut && entry->outputPoints) {
                currentModule.outputPoints = entry->outputPoints;
                currentModule.pointsFromCatalog = true;
            }
        } else if (currentModule.kind != ModuleKind::PowerSupply) {
            note(ParseDiagnostic::Level::Info,
                 "module reference '" + currentModule.reference
                     + "' is not in the catalog; channel counts come from the file only");
        }

        currentRack.modules.push_back(std::move(currentModule));
        currentModule = Module{};
        haveModule = false;
    };

    auto flushRack = [&] {
        if (!haveRack) return;
        flushModule();
        std::sort(currentRack.modules.begin(), currentRack.modules.end(),
                  [](const Module& a, const Module& b) { return a.slot < b.slot; });
        racks.push_back(std::move(currentRack));
        currentRack = Rack{};
        haveRack = false;
    };

    for (;;) {
        auto ev = r.next();
        if (!ev) return core::Err<core::Error>(ev.error());
        if (*ev == XmlEvent::EndOfDocument) break;
        ++out.elementsVisited;

        if (*ev == XmlEvent::EndElement) {
            const auto n = r.name();
            if      (n == "channelATS")  flushChannel();
            else if (n == "moduleATS")   { flushModule(); scope = Scope::Rack; }
            else if (n == "powerSupply") { flushModule(); scope = Scope::Rack; }
            else if (n == "rackATS")     { flushRack();  scope = Scope::Plc; }
            else if (n == "paramKPW")    inParamKPW = false;
            continue;
        }
        if (*ev != XmlEvent::StartElement) continue;
        const auto el = r.name();

        if (el == "fileHeader") {
            if (p.header.company.empty()) {
                p.header.company     = r.attr("company");
                p.header.product     = r.attr("product");
                p.header.dtdVersion  = r.attr("DTDVersion");
                p.header.contentKind = r.attr("content");
            }
            continue;
        }
        if (el == "contentHeader") {
            if (p.header.projectName.empty()) p.header.projectName = r.attr("name");
            continue;
        }

        if (el == "PLC") {
            scope = Scope::Plc;
            auto& m = p.hardware.memory;
            m.declared             = true;
            m.internalBits         = static_cast<std::uint32_t>(parseInt(r.attr("numberInternalBit")));
            m.internalWords        = static_cast<std::uint32_t>(parseInt(r.attr("numberInternalWord")));
            m.constantWords        = static_cast<std::uint32_t>(parseInt(r.attr("numberConstantWord")));
            m.initialiseWords      = r.attr("MWInitZero") == "true";
            m.autoRun              = r.attr("autoRun") == "true";
            m.coldStartOnly        = r.attr("ColdStartOnly") == "true";
            m.changeConfigOnTheFly = r.attr("ccotfActive") == "true";
            continue;
        }
        if (el == "busATS") { p.hardware.busName = r.attr("name"); continue; }

        if (el == "rackATS") {
            flushRack();
            currentRack = Rack{};
            haveRack = true;
            scope = Scope::Rack;
            continue;
        }
        if (el == "powerSupply") { flushModule(); haveModule = true; scope = Scope::PowerSupply; continue; }
        if (el == "moduleATS") {
            flushModule();
            currentModule = Module{};
            currentModule.isCpu = r.attr("UCMod") == "true";
            haveModule = true;
            scope = Scope::Module;
            continue;
        }

        if (el == "partItem") {
            const auto family = r.attr("family");
            const auto part   = r.attr("partNumber");
            const auto ver    = r.attr("version");

            if (scope == Scope::Module || scope == Scope::PowerSupply) {
                currentModule.reference = part;
                currentModule.family    = family;
                currentModule.firmware  = ver;
                if (scope == Scope::PowerSupply) currentModule.kind = ModuleKind::PowerSupply;
            } else if (scope == Scope::Rack && haveRack) {
                currentRack.reference = part;
                if (const auto* entry = catalog.findModule(part)) {
                    currentRack.slotCount   = entry->slots;
                    currentRack.description = entry->description;
                }
            } else if (scope == Scope::Plc || scope == Scope::None) {
                // The CPU, declared on <PLC> before the racks.
                p.hardware.cpuReference = part;
                p.hardware.resourceName = family;
                if (const auto* entry = catalog.findModule(part)) {
                    p.hardware.family = entry->family;
                } else {
                    note(ParseDiagnostic::Level::Warning,
                         "CPU reference '" + part + "' is not in the catalog");
                }
                // The .XPG records the OS the program was built against; the
                // .XHW records the one the hardware is configured for. When they
                // disagree that is worth knowing, so both are reported rather
                // than one silently overwriting the other.
                if (!xpgFirmware.empty() && !ver.empty()) {
                    const auto normalise = [](std::string v) {
                        v.erase(std::remove_if(v.begin(), v.end(),
                                               [](char c) { return c == ' '; }), v.end());
                        return v;
                    };
                    if (normalise(xpgFirmware) != normalise(ver))
                        note(ParseDiagnostic::Level::Warning,
                             "OS version differs between exports: the program export says "
                                 + xpgFirmware + ", the hardware export says " + ver);
                }
                p.hardware.cpuFirmware = ver;
            }
            continue;
        }

        if (el == "equipInfo") {
            const auto topo = r.attr("topoAddress");
            const auto pos  = parseInt(r.attr("position"), 0);
            if (scope == Scope::Module || scope == Scope::PowerSupply) {
                currentModule.topologicalAddress = topo;
                currentModule.slot     = static_cast<std::int16_t>(pos);
                currentModule.nodeGuid = r.attr("NodeGuid");
                currentModule.rack     = static_cast<std::uint16_t>(racks.size());
            } else if (scope == Scope::Rack && haveRack) {
                currentRack.topologicalAddress = topo;
                currentRack.number = static_cast<std::uint16_t>(pos);
            }
            continue;
        }

        if (el == "channelATS") {
            flushChannel();
            currentChannel = Channel{};
            currentChannel.number    = static_cast<std::uint16_t>(parseInt(r.attr("number")));
            currentChannel.role      = r.attr("ASFCatKey");
            currentChannel.task      = r.attr("task", "MAST");
            currentChannel.direction = directionFromRole(currentChannel.role);
            haveChannel = true;
            continue;
        }
        if (el == "descFB" && haveChannel) {
            currentChannel.functionCode    = static_cast<std::uint16_t>(parseInt(r.attr("code")));
            currentChannel.functionVersion = static_cast<std::uint16_t>(parseInt(r.attr("version")));
            continue;
        }
        if (el == "descIOB" && haveChannel) {
            currentChannel.iobFile = r.attr("IOBFileName");
            continue;
        }
        if (el == "paramKW")  { inParamKPW = false; continue; }
        if (el == "paramKPW") { inParamKPW = true;  continue; }
        if (el == "hexaValue" && haveChannel) {
            const auto v = parseHex(r.attr("hexaValue"));
            (inParamKPW ? currentChannel.paramKPW : currentChannel.paramKW).push_back(v);
            continue;
        }

        if (el == "dataMemoryProtect" || el == "configATS" || el == "configModule"
            || el == "IOConf")
            continue;

        if (opts.strict)
            return core::fail(core::ErrorCode::XmlMalformed,
                              "line " + std::to_string(r.line()) + ": unexpected <"
                                  + std::string(el) + ">");
    }
    flushRack();

    // Renumber racks by their declared position and record the supply.
    for (std::size_t i = 0; i < racks.size(); ++i)
        for (auto& m : racks[i].modules) m.rack = racks[i].number;
    for (const auto& rack : racks)
        for (const auto& m : rack.modules)
            if (m.kind == ModuleKind::PowerSupply && p.hardware.powerSupply.empty())
                p.hardware.powerSupply = m.reference;

    p.hardware.racks    = std::move(racks);
    p.hardware.inferred = false;

    // The notice the .XPG import added is no longer true.
    std::erase_if(p.partialDataNotices, [](const std::string& n) {
        return n.find("Rack and module layout") != std::string::npos;
    });

    out.bytesRead = buffer.size();
    out.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (progress) progress(ParseProgress{"hardware", 1.f, buffer.size()});
    return out;
}

} // namespace importer
