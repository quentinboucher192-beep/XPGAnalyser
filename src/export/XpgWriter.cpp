#include "XpgWriter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace exporter {

    using namespace domain;

    namespace {

        // Indentation is a tab per level, matching what Control Expert emits. It has no
        // semantic weight but keeps a diff between an original and a re-export readable,
        // which is how the round trip gets checked by eye as well as by test.
        struct Out {
            std::string  text;
            int          depth{ 0 };

            bool crlf{ true };

            void line(std::string_view s) {
                text.append(static_cast<std::size_t>(depth), '\t');
                text.append(s);
                // Les fins de ligne DU DOCUMENT LU. Control Expert ecrit en CRLF;
                // emettre des LF faisait changer chacune des lignes d'un fichier relu et
                // reecrit, ce qui rend une comparaison avec l'original illisible - alors
                // que c'est precisement ce qu'on veut faire apres une modification.
                if (crlf) text.push_back('\r');
                text.push_back('\n');
            }
            void open(std::string_view s) { line(s); ++depth; }
            void close(std::string_view s) { --depth; line(s); }
            void raw(std::string_view s) { text.append(s); }
        };

        std::string timestamp() {
            // Control Expert's own format: date_and_time#2026-9-4-8:50:41
            const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &now);
#else
            localtime_r(&now, &tm);
#endif
            char buf[64];
            std::snprintf(buf, sizeof buf, "date_and_time#%d-%d-%d-%d:%02d:%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
            return buf;
        }

        std::string attr(std::string_view name, std::string_view value) {
            std::string s;
            s += ' ';
            s += name;
            s += "=\"";
            s += escapeXml(value);
            s += '"';
            return s;
        }

        std::string_view languageElement(PouLanguage l) {
            switch (l) {
            case PouLanguage::IL:  return "ILSource";
            case PouLanguage::LD:  return "LDSource";
            case PouLanguage::FBD: return "FBDSource";
            case PouLanguage::SFC: return "SFCSource";
            default:               return "STSource";
            }
        }

        // Writes one <variables> element, with its comment and initial value when the
        // model carries them.
        void writeVariable(Out& o, const Project& p, const Variable& v) {
            std::string head = "<variables";
            head += attr("name", p.strings.text(v.name));
            head += attr("typeName", p.strings.text(v.type.name));
            if (v.located && !v.address.raw.empty())
                head += attr("topologicalAddress", v.address.raw);

            const bool hasBody = v.comment != 0 || v.initValue != 0
                || !v.attributes.empty() || !v.instanceElements.empty();
            if (!hasBody) { o.line(head + "></variables>"); return; }

            o.open(head + ">");
            // Order follows the DTD: attributes, then the initial value, then the
            // comment, then the per-element descriptors.
            for (const auto& [name, value] : v.attributes)
                o.line("<attribute" + attr("name", name) + attr("value", value) + "></attribute>");
            if (v.initValue != 0)
                o.line("<variableInit" + attr("value", p.strings.text(v.initValue)) + "></variableInit>");
            if (v.comment != 0)
                o.line("<comment>" + escapeXml(p.strings.text(v.comment)) + "</comment>");
            for (const auto& e : v.instanceElements) {
                if (e.comment == 0) {
                    o.line("<instanceElementDesc" + attr("name", e.name) + "></instanceElementDesc>");
                    continue;
                }
                o.open("<instanceElementDesc" + attr("name", e.name) + ">");
                o.line("<comment>" + escapeXml(p.strings.text(e.comment)) + "</comment>");
                o.close("</instanceElementDesc>");
            }
            o.close("</variables>");
        }

        void writeVariableGroup(Out& o, const Project& p, std::string_view element,
            const std::vector<Index>& indices, VariableScope scope) {
            std::vector<const Variable*> members;
            for (auto i : indices)
                if (p.variables[i].scope == scope) members.push_back(&p.variables[i]);
            if (members.empty()) return;

            o.open("<" + std::string(element) + ">");
            for (const auto* v : members) writeVariable(o, p, *v);
            o.close("</" + std::string(element) + ">");
        }

    } // namespace

    // For an ATTRIBUTE value, where the quote characters matter.
    std::string escapeXml(std::string_view in) {
        std::string out;
        out.reserve(in.size() + in.size() / 8);
        for (char c : in) {
            switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out.push_back(c);
            }
        }
        return out;
    }

    // For ELEMENT TEXT - a section's source, a comment. Only the three characters
    // that would end the element or start an entity.
    //
    // An apostrophe is ordinary text here, and Control Expert writes it as one. We
    // were writing &apos;, which is valid XML and NOT what the file said: 650 lines
    // of the reference project came back different for no reason but this, and ST
    // is full of apostrophes because that is how it quotes a string.
    std::string escapeXmlText(std::string_view in) {
        std::string out;
        out.reserve(in.size() + in.size() / 16);
        for (char c : in) {
            switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            default:  out.push_back(c);
            }
        }
        return out;
    }

    // ---------------------------------------------------------------------------
    core::Result<std::string> writeXpg(const Project& p, const XpgWriteOptions& opts) {
        if (!p.layoutMatches())
            return core::fail(core::ErrorCode::IncompleteProject, p.layoutMismatchMessage());

        Out o;
        o.crlf = p.header.crlf;
        // Via line(), pour que la declaration suive les memes fins de ligne que le
        // reste. Ecrite en raw, elle etait la seule ligne du document a rester en LF.
        o.line("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
        o.open("<PGMExchangeFile>");

        // --- headers ----------------------------------------------------------
        {
            std::string h = "<fileHeader";
            h += attr("company", p.header.company.empty() ? "Schneider Automation" : p.header.company);
            h += attr("product", p.header.product.empty() ? opts.exportedBy : p.header.product);
            h += attr("dateTime", opts.dateTime.empty() ? timestamp() : opts.dateTime);
            h += attr("content", p.header.contentKind.empty() ? "Fichier source programme"
                : p.header.contentKind);
            h += attr("DTDVersion", p.header.dtdVersion.empty() ? "41" : p.header.dtdVersion);
            o.line(h + "></fileHeader>");

            std::string c = "<contentHeader";
            c += attr("name", p.header.projectName);
            c += attr("version", p.header.projectVersion.empty() ? "0.0.1" : p.header.projectVersion);
            // The project's own date, not the export's - two different facts: when
            // the program was last changed, and when this file was written.
            //
            // ABSENT WHEN THE SOURCE HAD NONE. Inventing one made a file that never
            // carried the attribute come back carrying it, which is a difference the
            // reader did not ask for and cannot undo.
            // La date DU CONTENU, pas celle du fichier. Prendre celle du fileHeader
            // faisait apparaitre l'attribut sur un contentHeader qui n'en avait pas.
            if (!p.header.contentDateTime.empty())
                c += attr("dateTime", p.header.contentDateTime);
            o.line(c + "></contentHeader>");
        }

        // --- logical configuration --------------------------------------------
        {
            o.open("<logicConf>");
            std::string resIdent = p.hardware.cpuReference;
            if (!p.hardware.cpuFirmware.empty()) resIdent += " " + p.hardware.cpuFirmware;

            std::string r = "<resource";
            r += attr("resName", p.hardware.resourceName.empty() ? "Micro Basic"
                : p.hardware.resourceName);
            r += attr("resIdent", resIdent);
            o.open(r + ">");

            for (const auto& task : p.tasks) {
                std::string t = "<taskDesc";
                t += attr("task", p.strings.text(task.name));
                t += attr("taskType", task.type.empty() ? "cyclic" : task.type);
                // Lot API 4 : la periode d'une tache periodique (FAST, AUX).
                if (task.type == "periodic" && task.period > 0) t += attr("value", std::to_string(task.period));
                t += attr("valueType", "0");
                t += attr("maxExecTime", std::to_string(task.watchdog));
                o.open(t + ">");
                for (auto si : task.sections) {
                    const auto& s = p.sections[si];
                    // A SUBROUTINE IS NOT A SECTION. It is declared with <SRDesc>
                    // and it is CALLED; a section runs every scan. Writing one as
                    // the other changes what the program does.
                    if (s.isSubroutine) {
                        o.line("<SRDesc" + attr("name", p.strings.text(s.name)) + "></SRDesc>");
                        continue;
                    }
                    std::string d = "<sectionDesc";
                    d += attr("name", p.strings.text(s.name));
                    // THE ACTIVATION CONDITION WAS BEING DROPPED. A section that only
                    // runs when ConfigurationMemoireOk is true came back running on
                    // every scan - the model held the condition all along, the writer
                    // simply never emitted it.
                    if (s.activationCondition != 0)
                        d += attr("activationCondition", p.strings.text(s.activationCondition));
                    if (s.logicCondition != 0)
                        d += attr("logicCondition", p.strings.text(s.logicCondition));
                    d += attr("SectionOrder", std::to_string(s.order));
                    o.line(d + "></sectionDesc>");
                }
                // Program units are scheduled by the same descriptor list, after the
                // plain sections. Sorted by SectionOrder rather than by position in
                // the model: the model's order depends on how the project was
                // loaded, and two loads of the same project must export identically.
                std::vector<const Pou*> units;
                for (const auto& pou : p.pous)
                    if (pou.kind == PouKind::ProgramUnit && pou.task == task.name)
                        units.push_back(&pou);
                std::stable_sort(units.begin(), units.end(),
                    [](const Pou* a, const Pou* b) { return a->order < b->order; });
                for (const auto* pou : units) {
                    std::string d = "<programUnitDesc";
                    d += attr("name", p.strings.text(pou->name));
                    d += attr("SectionOrder", std::to_string(pou->order));
                    o.line(d + "></programUnitDesc>");
                }
                o.close("</taskDesc>");
            }
            o.close("</resource>");
            o.close("</logicConf>");
        }

        // --- derived data types ------------------------------------------------
        for (Index d = 0; d < p.derivedTypes.size(); ++d) {
            const auto& ddt = p.derivedTypes[d];
            std::string head = "<DDTSource";
            head += attr("DDTName", p.strings.text(ddt.name));
            head += attr("version", ddt.version.empty() ? "0.01" : ddt.version);
            o.open(head + ">");

            // Zeroed on instruction: Control Expert recomputes the signature on
            // import, and a stale value carried across an edit is worse than none.
            std::string a = "<attribute";
            a += attr("name", "TypeSignatureCheckSumString");
            a += attr("value", opts.zeroChecksums ? "0" : ddt.checksum);
            o.line(a + "></attribute>");

            o.open("<structure>");
            for (auto fi : ddt.fields) writeVariable(o, p, p.variables[fi]);
            o.close("</structure>");
            o.close("</DDTSource>");
        }

        // --- DFB types ----------------------------------------------------------
        for (Index i = 0; i < p.pous.size(); ++i) {
            const auto& pou = p.pous[i];
            if (pou.kind != PouKind::FunctionBlockType) continue;

            std::string head = "<FBSource";
            head += attr("nameOfFBType", p.strings.text(pou.name));
            head += attr("version", pou.version.empty() ? "0.01" : pou.version);
            o.open(head + ">");

            // Attributes first, then the declarations. The checksums among them are
            // zeroed on instruction; flags such as UseNewTplSignAlgo are given back
            // exactly as Control Expert wrote them.
            for (const auto& [name, value] : pou.attributes) {
                const bool isChecksum = name.find("CheckSum") != std::string::npos;
                std::string a = "<attribute";
                a += attr("name", name);
                a += attr("value", (opts.zeroChecksums && isChecksum) ? "0" : value);
                o.line(a + "></attribute>");
            }

            // Order matters: DTD 41 is a sequence, and inOut comes before output.
            // Emitting output first produces a file that parses and that Control
            // Expert refuses.
            writeVariableGroup(o, p, "inputParameters", pou.parameters, VariableScope::Input);
            writeVariableGroup(o, p, "inOutParameters", pou.parameters, VariableScope::InOut);
            writeVariableGroup(o, p, "outputParameters", pou.parameters, VariableScope::Output);
            writeVariableGroup(o, p, "publicLocalVariables", pou.locals, VariableScope::Public);
            writeVariableGroup(o, p, "privateLocalVariables", pou.locals, VariableScope::Local);

            for (auto si : pou.sections) {
                const auto& s = p.sections[si];
                std::string prog = "<FBProgram";
                prog += attr("name", p.strings.text(s.name));
                if (s.activationCondition != 0)
                    prog += attr("activationCondition", p.strings.text(s.activationCondition));
                if (s.logicCondition != 0)
                    prog += attr("logicCondition", p.strings.text(s.logicCondition));
                o.open(prog + ">");
                o.line("<" + std::string(languageElement(s.language)) + ">"
                    + escapeXml(s.body) + "</" + std::string(languageElement(s.language)) + ">");
                o.close("</FBProgram>");
            }
            o.close("</FBSource>");
        }

        // --- program units ------------------------------------------------------
        for (Index i = 0; i < p.pous.size(); ++i) {
            const auto& pou = p.pous[i];
            if (pou.kind != PouKind::ProgramUnit) continue;

            o.open("<programUnit" + attr("name", p.strings.text(pou.name)) + ">");
            // A program unit has an interface too - this project has 97 in/out
            // parameters on program units and 7 on DFB types, so omitting them here
            // was losing more than the DFB case would have.
            writeVariableGroup(o, p, "inputParameters", pou.parameters, VariableScope::Input);
            writeVariableGroup(o, p, "inOutParameters", pou.parameters, VariableScope::InOut);
            writeVariableGroup(o, p, "outputParameters", pou.parameters, VariableScope::Output);
            writeVariableGroup(o, p, "publicLocalVariables", pou.locals, VariableScope::Public);
            writeVariableGroup(o, p, "privateLocalVariables", pou.locals, VariableScope::Local);

            for (auto si : pou.sections) {
                const auto& s = p.sections[si];
                std::string prog = "<program";
                if (s.activationCondition != 0)
                    prog += attr("activationCondition", p.strings.text(s.activationCondition));
                if (s.logicCondition != 0)
                    prog += attr("logicCondition", p.strings.text(s.logicCondition));
                o.open(prog + ">");

                std::string ident = "<identProgram";
                ident += attr("name", p.strings.text(s.name));
                ident += attr("type", s.isSubroutine ? "SR" : "section");
                ident += attr("SectionOrder", std::to_string(s.order));
                o.line(ident + "></identProgram>");
                o.line("<" + std::string(languageElement(s.language)) + ">"
                    + escapeXml(s.body) + "</" + std::string(languageElement(s.language)) + ">");
                o.close("</program>");
            }

            if (opts.includeAnimationTables)
                for (const auto& table : p.animationTables) {
                    if (table.owner != pou.name) continue;
                    std::string t = "<animationTable";
                    t += attr("name", p.strings.text(table.name));
                    t += attr("PouOwner", p.strings.text(table.owner));
                    o.open(t + ">");
                    // Les lignes de l'IHM restent dans le projet : Control Expert ne
                    // connait pas ces variables (lot API 3).
                    for (const auto& e : table.entries) {
                        if (e.hmi) continue;
                        o.line("<elementDescription" + attr("name", p.strings.text(e.name))
                            + "></elementDescription>");
                    }
                    o.close("</animationTable>");
                }
            o.close("</programUnit>");
        }

        // --- task sections ------------------------------------------------------
        for (Index si = 0; si < p.sections.size(); ++si) {
            const auto& s = p.sections[si];
            // Sections owned by a POU were emitted above.
            if (s.owner != kNoIndex && s.owner < p.pous.size()) {
                const auto kind = p.pous[s.owner].kind;
                if (kind == PouKind::FunctionBlockType || kind == PouKind::ProgramUnit) continue;
            }
            std::string prog = "<program";
            // 1.8.0 : la condition est sur <sectionDesc> (ecrite plus haut) ; ici
            // seulement si le fichier lu l'y portait aussi.
            if (s.conditionOnProgram && s.activationCondition != 0)
                prog += attr("activationCondition", p.strings.text(s.activationCondition));
            if (s.conditionOnProgram && s.logicCondition != 0)
                prog += attr("logicCondition", p.strings.text(s.logicCondition));
            o.open(prog + ">");

            std::string ident = "<identProgram";
            ident += attr("name", p.strings.text(s.name));
            ident += attr("type", s.isSubroutine ? "SR" : "section");
            ident += attr("task", p.strings.text(s.task));
            ident += attr("SectionOrder", std::to_string(s.order));
            o.line(ident + "></identProgram>");
            o.line("<" + std::string(languageElement(s.language)) + ">"
                + escapeXml(s.body) + "</" + std::string(languageElement(s.language)) + ">");
            o.close("</program>");
        }

        // --- the global dictionary ----------------------------------------------
        {
            std::vector<const Variable*> globals;
            for (const auto& v : p.variables)
                if (v.scope == VariableScope::Global || v.scope == VariableScope::Constant)
                    globals.push_back(&v);

            if (!globals.empty()) {
                o.open("<dataBlock>");
                for (const auto* v : globals) writeVariable(o, p, *v);
                o.close("</dataBlock>");
            }
        }

        o.close("</PGMExchangeFile>");
        return o.text;
    }

    // ---------------------------------------------------------------------------
    core::Result<std::string> writeXhw(const Project& p, const XpgWriteOptions& opts) {
        if (p.hardware.racks.empty())
            return core::fail(core::ErrorCode::IncompleteProject,
                "no hardware configuration to export");

        Out o;
        o.crlf = p.header.crlf;
        // Via line(), pour que la declaration suive les memes fins de ligne que le
        // reste. Ecrite en raw, elle etait la seule ligne du document a rester en LF.
        o.line("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
        o.open("<IOExchangeFile>");

        std::string h = "<fileHeader";
        h += attr("company", p.header.company.empty() ? "Schneider Automation" : p.header.company);
        h += attr("product", p.header.product.empty() ? opts.exportedBy : p.header.product);
        h += attr("dateTime", opts.dateTime.empty() ? timestamp() : opts.dateTime);
        h += attr("content", "Fichier source configuration");
        h += attr("DTDVersion", p.header.dtdVersion.empty() ? "41" : p.header.dtdVersion);
        o.line(h + "></fileHeader>");
        o.line("<contentHeader" + attr("name", p.header.projectName)
            + attr("version", p.header.projectVersion.empty() ? "0.0.1" : p.header.projectVersion)
            + "></contentHeader>");

        o.open("<IOConf>");
        {
            const auto& m = p.hardware.memory;
            std::string plc = "<PLC";
            plc += attr("autoRun", m.autoRun ? "true" : "false");
            plc += attr("MWInitZero", m.initialiseWords ? "true" : "false");
            plc += attr("ColdStartOnly", m.coldStartOnly ? "true" : "false");
            plc += attr("ccotfActive", m.changeConfigOnTheFly ? "true" : "false");
            plc += attr("numberInternalWord", std::to_string(m.internalWords));
            plc += attr("numberConstantWord", std::to_string(m.constantWords));
            plc += attr("numberInternalBit", std::to_string(m.internalBits));
            o.open(plc + ">");
        }

        auto partItem = [&](std::string_view family, std::string_view part, std::string_view version) {
            std::string s = "<partItem";
            s += attr("family", family);
            s += attr("partNumber", part);
            s += attr("vendorName", "Schneider Automation");
            s += attr("version", version.empty() ? "01.00" : version);
            return s + "></partItem>";
            };
        auto equipInfo = [&](std::string_view topo, int position, std::string_view guid) {
            std::string s = "<equipInfo";
            s += attr("topoAddress", topo);
            s += attr("position", std::to_string(position));
            if (!guid.empty()) s += attr("NodeGuid", guid);
            return s + "></equipInfo>";
            };

        o.line(partItem(p.hardware.resourceName, p.hardware.cpuReference, p.hardware.cpuFirmware));
        o.line(equipInfo("\\0.0\\0.0", 0, {}));

        o.open("<configATS>");
        o.open("<busATS" + attr("name", p.hardware.busName.empty() ? "XBusMicro"
            : p.hardware.busName) + ">");
        o.line(equipInfo("\\0", 0, {}));

        for (const auto& rack : p.hardware.racks) {
            o.open("<rackATS>");
            o.line(partItem("Rack", rack.reference, "01.00"));
            o.line(equipInfo(rack.topologicalAddress.empty()
                ? "\\0.0\\" + std::to_string(rack.number)
                : rack.topologicalAddress,
                rack.number, {}));

            for (const auto& m : rack.modules) {
                if (m.kind == ModuleKind::PowerSupply) {
                    o.open("<powerSupply>");
                    o.line(partItem("Supply", m.reference, m.firmware));
                    o.line(equipInfo(m.topologicalAddress, m.slot, m.nodeGuid));
                    o.close("</powerSupply>");
                    continue;
                }
                o.open(std::string("<moduleATS") + (m.isCpu ? " UCMod=\"true\"" : "") + ">");
                o.line(partItem(m.family, m.reference, m.firmware));
                o.line(equipInfo(m.topologicalAddress, m.slot, m.nodeGuid));

                if (!m.channels.empty()) {
                    o.open("<configModule>");
                    for (const auto& c : m.channels) {
                        std::string ch = "<channelATS";
                        ch += attr("ASFCatKey", c.role);
                        ch += attr("task", c.task.empty() ? "MAST" : c.task);
                        ch += attr("number", std::to_string(c.number));
                        o.open(ch + ">");
                        o.line("<descFB" + attr("code", std::to_string(c.functionCode))
                            + attr("version", std::to_string(c.functionVersion)) + "></descFB>");
                        if (!c.iobFile.empty())
                            o.line("<descIOB" + attr("IOBFileName", c.iobFile)
                                + attr("version", "1") + "></descIOB>");

                        auto params = [&](std::string_view element, const std::vector<std::uint32_t>& v) {
                            if (v.empty()) return;
                            o.open("<" + std::string(element) + ">");
                            for (auto value : v) {
                                char buf[24];
                                std::snprintf(buf, sizeof buf, "0x%x", value);
                                o.line(std::string("<hexaValue") + attr("hexaValue", buf)
                                    + "></hexaValue>");
                            }
                            o.close("</" + std::string(element) + ">");
                            };
                        params("paramKW", c.paramKW);
                        params("paramKPW", c.paramKPW);
                        o.close("</channelATS>");
                    }
                    o.close("</configModule>");
                }
                o.close("</moduleATS>");
            }
            o.close("</rackATS>");
        }

        o.close("</busATS>");
        o.close("</configATS>");
        o.line("<dataMemoryProtect protectM=\"false\" protectMW=\"false\" "
            "protectIO=\"false\" protectS=\"false\"></dataMemoryProtect>");
        o.close("</PLC>");
        o.close("</IOConf>");
        o.close("</IOExchangeFile>");
        return o.text;
    }

    // ---------------------------------------------------------------------------
    namespace {
        core::Status writeToFile(const std::string& path, const std::string& text) {
            std::error_code ec;
            const auto parent = std::filesystem::path(path).parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent, ec);

            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) return core::fail(core::ErrorCode::FileUnreadable, "cannot write", path);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!out) return core::fail(core::ErrorCode::FileUnreadable, "short write", path);
            return core::ok();
        }
    } // namespace

    core::Status writeXpgFile(const Project& p, const std::string& path, const XpgWriteOptions& o) {
        auto text = writeXpg(p, o);
        if (!text) return core::Err<core::Error>(text.error());
        return writeToFile(path, *text);
    }

    core::Status writeXhwFile(const Project& p, const std::string& path, const XpgWriteOptions& o) {
        auto text = writeXhw(p, o);
        if (!text) return core::Err<core::Error>(text.error());
        return writeToFile(path, *text);
    }

} // namespace exporter