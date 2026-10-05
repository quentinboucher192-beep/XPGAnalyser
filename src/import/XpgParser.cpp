// =============================================================================
//  import/XpgParser.cpp
// -----------------------------------------------------------------------------
//  Single streaming pass over the document. Element handlers are dispatched on
//  the current path, so the parser never builds a tree and never revisits bytes.
//
//  Schema covered (as found in a Control Expert V15.3 export, DTD 41):
//
//    PGMExchangeFile
//      fileHeader                      company / product / DTDVersion
//      contentHeader                   project name / version
//      logicConf/resource              resName / resIdent   -> CPU identity
//        taskDesc                      task / taskType / maxExecTime
//          sectionDesc                 execution order + activation condition
//          programUnitDesc
//      DDTSource                       derived data types
//        structure/variables[/comment]
//      FBSource                        DFB types
//        input|output|inOut|public|privateLocalVariables/variables
//        FBProgram/STSource            one body per language element
//      programUnit                     multi-section program units
//        program/identProgram + STSource
//        animationTable/elementDescription
//      program/identProgram + STSource task sections
//      dataBlock/variables             global dictionary (+ topologicalAddress)
// =============================================================================
#include "ProjectParser.hpp"
#include "XmlReader.hpp"

#include <cstdlib>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>

namespace importer {
    namespace {

        using namespace domain;

        // --- type-string analysis ---------------------------------------------------
        // "ARRAY[0..27] OF ST_GC_Step" / "string[32]" / "config_gaz" / "DFB_GRAFCETENGINE"
        TypeRef classifyTypeImpl(std::string_view raw, StringPool& pool) {
            TypeRef t;
            t.name = pool.intern(raw);

            std::string upper(raw);
            std::transform(upper.begin(), upper.end(), upper.begin(),
                [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

            if (upper.rfind("ARRAY", 0) == 0) {
                t.klass = TypeClass::Array;
                const auto lb = raw.find('['), dots = raw.find("..", lb == std::string_view::npos ? 0 : lb);
                const auto rb = raw.find(']');
                if (lb != std::string_view::npos && dots != std::string_view::npos && rb != std::string_view::npos) {
                    std::int64_t lo = 0, hi = 0;
                    const auto* p1 = raw.data() + lb + 1;
                    const auto* p2 = raw.data() + dots + 2;
                    std::from_chars(p1, raw.data() + dots, lo);
                    std::from_chars(p2, raw.data() + rb, hi);
                    t.arrayLow = lo;
                    t.arrayHigh = hi;
                }
                const auto of = upper.find(" OF ");
                if (of != std::string::npos) t.elementType = pool.intern(raw.substr(of + 4));
                return t;
            }

            if (upper.rfind("STRING", 0) == 0) {
                t.klass = TypeClass::Elementary;
                const auto lb = raw.find('[');
                if (lb != std::string_view::npos) {
                    std::uint32_t n = 0;
                    std::from_chars(raw.data() + lb + 1, raw.data() + raw.size(), n);
                    t.stringLength = n;
                }
                return t;
            }

            static constexpr std::string_view kElementary[] = {
                "BOOL", "EBOOL", "BYTE", "WORD", "DWORD", "INT", "DINT", "UINT", "UDINT",
                "REAL", "TIME", "DATE", "TOD", "DT", "STRING",
            };
            for (auto e : kElementary)
                if (upper == e) { t.klass = TypeClass::Elementary; return t; }

            // Standard IEC function blocks that Control Expert does not export as DFBs.
            static constexpr std::string_view kStandardFb[] = {
                "TON", "TOF", "TP", "CTU", "CTD", "CTUD", "R_TRIG", "F_TRIG", "SR", "RS", "RTC",
            };
            for (auto e : kStandardFb)
                if (upper == e) { t.klass = TypeClass::FunctionBlock; return t; }

            t.elementType = t.name;
            t.klass = TypeClass::Unknown;   // resolved in the link step
            return t;
        }

        VariableScope scopeFromElement(std::string_view e) {
            if (e == "inputParameters")        return VariableScope::Input;
            if (e == "outputParameters")       return VariableScope::Output;
            if (e == "inOutParameters")        return VariableScope::InOut;
            if (e == "publicLocalVariables")   return VariableScope::Public;
            if (e == "privateLocalVariables")  return VariableScope::Local;
            return VariableScope::Global;
        }

        std::uint32_t countStatements(std::string_view body) {
            // Semicolons outside comments and string literals: close enough for a metric,
            // and two orders of magnitude cheaper than tokenising ST.
            std::uint32_t n = 0;
            bool inComment = false, inString = false;
            for (std::size_t i = 0; i < body.size(); ++i) {
                if (inComment) {
                    if (body[i] == '*' && i + 1 < body.size() && body[i + 1] == ')') { inComment = false; ++i; }
                    continue;
                }
                if (inString) { if (body[i] == '\'') inString = false; continue; }
                if (body[i] == '(' && i + 1 < body.size() && body[i + 1] == '*') { inComment = true; ++i; continue; }
                if (body[i] == '\'') { inString = true; continue; }
                if (body[i] == ';') ++n;
            }
            return n;
        }

        // Checked access. Every index here is already guarded against kNoIndex, but a
        // raw subscript turns any future mistake into undefined behaviour on a release
        // build and a hard crash on a debug one. Returning null costs nothing and turns
        // the same mistake into a diagnostic the user can report.
        template <class T>
        T* at(std::vector<T>& v, Index i) {
            return i < v.size() ? &v[i] : nullptr;
        }

        std::uint32_t countLines(std::string_view s) {
            if (s.empty()) return 0;
            return static_cast<std::uint32_t>(std::count(s.begin(), s.end(), '\n')) + 1;
        }

    } // namespace

    // ---------------------------------------------------------------------------
    domain::TypeRef classifyTypeName(std::string_view raw, domain::StringPool& pool) {
        return classifyTypeImpl(raw, pool);
    }

    bool XpgParser::canParse(std::string_view head) const noexcept {
        return head.find("PGMExchangeFile") != std::string_view::npos;
    }

    core::Result<ParseOutcome> XpgParser::parse(std::string_view buffer,
        Project& p,
        const ParseOptions& opts,
        const ProgressFn& progress) {
        if (!p.layoutMatches())
            return core::fail(core::ErrorCode::IncompleteProject, p.layoutMismatchMessage());

        const auto t0 = std::chrono::steady_clock::now();
        ParseOutcome out;

        // Les fins de ligne DU DOCUMENT, relevees une fois. Control Expert V15.3
        // ecrit en CRLF; le writer emettait des LF, si bien qu'un fichier relu et
        // reecrit voyait chacune de ses lignes changer. Comparer un projet modifie
        // avec son original devenait impossible - alors que c'est exactement ce
        // qu'on veut faire apres une modification.
        p.header.crlf = buffer.find("\r\n") != std::string_view::npos;
        // 1.8.0 (l'import suivi) : la progression en lignes du document, pas en
        // lignes divisees par 100 000 - une barre qui avance vraiment.
        const auto totalLines = static_cast<float>(std::max<std::ptrdiff_t>(1, std::count(buffer.begin(), buffer.end(), '\n')));

        XmlReader r(buffer);

        auto note = [&](ParseDiagnostic::Level lvl, std::string msg) {
            out.diagnostics.push_back(ParseDiagnostic{ lvl, std::move(msg), r.line(), p.header.sourceFile });
            };

        // --- root -------------------------------------------------------------
        for (;;) {
            auto ev = r.next();
            if (!ev) return core::Err<core::Error>(ev.error());
            if (*ev == XmlEvent::EndOfDocument)
                return core::fail(core::ErrorCode::XmlUnexpectedRoot, "no root element");
            if (*ev == XmlEvent::StartElement) break;
        }
        if (r.name() != "PGMExchangeFile")
            return core::fail(core::ErrorCode::XmlUnexpectedRoot,
                "root is <" + std::string(r.name()) + ">, expected <PGMExchangeFile>");

        // Context carried across the streaming pass.
        Index currentPou = kNoIndex;   // FBSource or programUnit currently open
        Index currentDdt = kNoIndex;
        Index currentTask = kNoIndex;
        VariableScope varScope = VariableScope::Global;
        std::string pendingSectionName, pendingSectionTask, pendingActivation, pendingLogic;
        bool pendingSubroutine = false;
        std::uint32_t pendingOrder = 0;
        Index lastVariable = kNoIndex;
        // <attribute> also appears inside <variables>; this flag says whether we are
        // still in the header of an FBSource, where the type checksums live.
        bool fbHeaderOpen = false;
        bool insideVariable = false;      // <attribute> means different things inside and outside
        bool lastInstanceElement = false; // the next <comment> belongs to the element, not the variable
        std::vector<std::tuple<std::string, std::uint32_t, Index>> pendingUnitOrders;
        // 1.8.0 : la condition d'activation d'une section de tache est sur son
        // <sectionDesc> (Control Expert V15) ; le corps arrive plus loin : (nom,
        // tache, activation, logique), posees a l'etape de liaison.
        std::vector<std::tuple<std::string, Index, std::string, std::string>> pendingSectionDescs;

        p.tasks.clear();

        for (;;) {
            auto ev = r.next();
            if (!ev) return core::Err<core::Error>(ev.error());
            if (*ev == XmlEvent::EndOfDocument) break;

            if (*ev == XmlEvent::EndElement) {
                const auto n = r.name();
                if (n == "variables") { insideVariable = false; lastInstanceElement = false; }
                else if (n == "instanceElementDesc")   lastInstanceElement = false;
                if (n == "FBSource" || n == "programUnit") currentPou = kNoIndex;
                else if (n == "DDTSource")                      currentDdt = kNoIndex;
                else if (n == "taskDesc")                       currentTask = kNoIndex;
                else if (n == "inputParameters" || n == "outputParameters" || n == "inOutParameters"
                    || n == "publicLocalVariables" || n == "privateLocalVariables")
                    varScope = VariableScope::Global;
                continue;
            }
            if (*ev != XmlEvent::StartElement) continue;

            ++out.elementsVisited;
            const auto el = r.name();
            // 1.8.0 : tout le document avance la barre (les declarations aussi).
            if (progress && (out.elementsVisited % 512) == 0)
                progress(ParseProgress{ "xml", std::min(1.f, static_cast<float>(r.line()) / totalLines), r.line() });

            // ---------------------------------------------------------- headers ---
            if (el == "fileHeader") {
                p.header.company = r.attr("company");
                p.header.product = r.attr("product");
                p.header.dtdVersion = r.attr("DTDVersion");
                p.header.contentKind = r.attr("content");
                p.header.exportedAt = r.attr("dateTime");
                // "Control Expert V15.3 - 230214C" -> "V15.3"
                if (const auto v = p.header.product.find(" V"); v != std::string::npos) {
                    const auto end = p.header.product.find(' ', v + 1);
                    p.header.productVersion = p.header.product.substr(
                        v + 1, end == std::string::npos ? std::string::npos : end - v - 1);
                }
                if (!p.header.dtdVersion.empty() && p.header.dtdVersion != "41")
                    note(ParseDiagnostic::Level::Warning,
                        "DTD version " + p.header.dtdVersion + " differs from the validated version 41");
                continue;
            }
            if (el == "contentHeader") {
                p.header.contentDateTime = r.attr("dateTime");
                p.header.projectName = r.attr("name");
                p.header.projectVersion = r.attr("version");
                continue;
            }

            // ------------------------------------------------ hardware identity ---
            if (el == "resource") {
                p.hardware.resourceName = r.attr("resName");
                const auto ident = r.attr("resIdent");
                auto catalog = HardwareCatalog::builtinFallback();
                if (auto res = catalog.resolve(ident)) {
                    p.hardware.family = res->first.family;
                    p.hardware.cpuReference = res->first.reference;
                    p.hardware.cpuFirmware = res->second;
                }
                else {
                    p.hardware.cpuReference = ident;
                    note(ParseDiagnostic::Level::Warning,
                        "CPU reference '" + ident + "' is not in the hardware catalog");
                }
                p.hardware.inferred = true;
                p.partialDataNotices.emplace_back(
                    "Rack and module layout is not part of a .XPG export. Import the matching "
                    ".XHW or .XEF file to populate the PLC configuration view.");
                continue;
            }
            if (el == "taskDesc") {
                Task t;
                t.name = p.strings.intern(r.attr("task"));
                t.type = r.attr("taskType");
                t.watchdog = static_cast<std::uint32_t>(std::atoi(r.attr("maxExecTime", "0").c_str()));
                t.period = static_cast<std::uint32_t>(std::atoi(r.attr("value", "0").c_str()));   // lot API 4
                p.tasks.push_back(std::move(t));
                currentTask = static_cast<Index>(p.tasks.size() - 1);
                continue;
            }
            if (el == "sectionDesc") {
                // Declaration of execution order; the body arrives later, and the
                // sections are bound to their task in the link step.
                // 1.8.0 : sa condition d'activation aussi (elle etait perdue).
                if (!r.attr("activationCondition").empty() || !r.attr("logicCondition").empty())
                    pendingSectionDescs.emplace_back(r.attr("name"), currentTask, r.attr("activationCondition"), r.attr("logicCondition"));
                continue;
            }

            // ------------------------------------------------------ derived types ---
            if (el == "DDTSource") {
                DerivedType d;
                d.name = p.strings.intern(r.attr("DDTName"));
                d.version = r.attr("version");
                p.derivedTypes.push_back(std::move(d));
                currentDdt = static_cast<Index>(p.derivedTypes.size() - 1);
                varScope = VariableScope::DerivedMember;
                continue;
            }
            if (el == "attribute") {
                if (currentDdt != kNoIndex && varScope != VariableScope::DerivedMember
                    && r.attr("name") == "TypeSignatureCheckSumString")
                    p.derivedTypes[currentDdt].checksum = r.attr("value");
                else if (fbHeaderOpen) {
                    if (auto* pou = at(p.pous, currentPou))
                        pou->attributes.emplace_back(r.attr("name"), r.attr("value"));
                }
                else if (lastVariable != kNoIndex && insideVariable)
                    p.variables[lastVariable].attributes.emplace_back(r.attr("name"), r.attr("value"));
                continue;
            }
            if (el == "instanceElementDesc" && lastVariable != kNoIndex) {
                p.variables[lastVariable].instanceElements.push_back(
                    InstanceElement{ r.attr("name"), 0 });
                lastInstanceElement = true;
                continue;
            }
            if (el == "programUnitDesc") {
                pendingUnitOrders.emplace_back(r.attr("name"),
                    static_cast<std::uint32_t>(
                        std::atoi(r.attr("SectionOrder", "0").c_str())),
                    currentTask);
                continue;
            }

            // -------------------------------------------------------- DFB types ---
            if (el == "FBSource") {
                Pou pou;
                pou.name = p.strings.intern(r.attr("nameOfFBType"));
                pou.kind = PouKind::FunctionBlockType;
                pou.version = r.attr("version");
                p.pous.push_back(std::move(pou));
                currentPou = static_cast<Index>(p.pous.size() - 1);

                LibraryEntry lib;
                lib.name = p.pous.back().name;
                lib.kind = LibraryKind::User;      // refined by the analyzer
                lib.version = p.pous.back().version;
                lib.family = p.strings.intern("Custom");
                lib.pouIndex = currentPou;
                p.libraries.push_back(lib);
                fbHeaderOpen = true;
                continue;
            }

            // ---------------------------------------------------- program units ---
            if (el == "programUnit") {
                fbHeaderOpen = false;
                Pou pou;
                pou.name = p.strings.intern(r.attr("name"));
                pou.kind = PouKind::ProgramUnit;
                p.pous.push_back(std::move(pou));
                currentPou = static_cast<Index>(p.pous.size() - 1);
                continue;
            }

            // --------------------------------------------------- variable groups ---
            if (el == "inputParameters" || el == "outputParameters" || el == "inOutParameters"
                || el == "publicLocalVariables" || el == "privateLocalVariables") {
                varScope = scopeFromElement(el);
                fbHeaderOpen = false;       // past the header, into the declarations
                continue;
            }
            if (el == "dataBlock") { varScope = VariableScope::Global; continue; }

            // ------------------------------------------------------- declarations ---
            if (el == "variables") {
                Variable v;
                auto nameAttr = r.requireAttr("name");
                if (!nameAttr) {
                    if (opts.strict) return core::Err<core::Error>(nameAttr.error());
                    note(ParseDiagnostic::Level::Error, nameAttr.error().message());
                    (void)r.skipElement();
                    continue;
                }
                v.name = p.strings.intern(*nameAttr);
                v.type = classifyTypeImpl(r.attr("typeName", "?"), p.strings);
                v.scope = varScope;
                v.owner = (varScope == VariableScope::DerivedMember) ? currentDdt : currentPou;

                if (r.has("topologicalAddress")) {
                    v.address = Address::parse(r.attr("topologicalAddress"));
                    v.located = v.address.valid();
                }
                p.variables.push_back(std::move(v));
                lastVariable = static_cast<Index>(p.variables.size() - 1);
                insideVariable = !r.selfClosing();
                lastInstanceElement = false;

                if (varScope == VariableScope::DerivedMember) {
                    if (auto* ddt = at(p.derivedTypes, currentDdt))
                        ddt->fields.push_back(lastVariable);
                }
                else if (auto* pou = at(p.pous, currentPou)) {
                    if (varScope == VariableScope::Input || varScope == VariableScope::Output
                        || varScope == VariableScope::InOut)
                        pou->parameters.push_back(lastVariable);
                    else if (varScope != VariableScope::Global)
                        pou->locals.push_back(lastVariable);
                }
                continue;
            }
            if (el == "comment" && lastVariable != kNoIndex) {
                auto text = r.readElementText();
                if (!text) return core::Err<core::Error>(text.error());
                auto& v = p.variables[lastVariable];
                if (lastInstanceElement && !v.instanceElements.empty())
                    v.instanceElements.back().comment = p.strings.intern(*text);
                else
                    v.comment = p.strings.intern(*text);
                continue;
            }
            if (el == "variableInit" && lastVariable != kNoIndex) {
                p.variables[lastVariable].initValue = p.strings.intern(r.attr("value"));
                continue;
            }

            // ------------------------------------------------------- section body ---
            if (el == "identProgram") {
                pendingSectionName = r.attr("name");
                pendingSectionTask = r.attr("task");
                // type="SR" is a SUBROUTINE. It was read as an ordinary section, so
                // a re-export turned every subroutine into a section - and a section
                // runs every scan while a subroutine runs when it is called.
                pendingSubroutine = r.attr("type") == "SR";
                pendingOrder = static_cast<std::uint32_t>(std::atoi(r.attr("SectionOrder", "0").c_str()));
                continue;
            }
            if (el == "FBProgram") {
                pendingSectionName = r.attr("name");
                pendingSectionTask.clear();
                pendingActivation = r.attr("activationCondition");
                pendingLogic = r.attr("logicCondition");
                continue;
            }
            if (el == "program") {
                pendingActivation = r.attr("activationCondition");
                pendingLogic = r.attr("logicCondition");
                continue;
            }

            if (const auto lang = languageFromElement(el); lang != PouLanguage::Unknown) {
                auto body = r.readElementText();
                if (!body) return core::Err<core::Error>(body.error());

                Section s;
                s.name = p.strings.intern(pendingSectionName);
                s.language = lang;
                // Only a top-level <program> carries a task attribute. Defaulting a
                // DFB body or a program-unit section to "MAST" made the re-export
                // list every one of them in the MAST task descriptor, which the
                // original file does not do.
                s.task = p.strings.intern(pendingSectionTask);
                s.order = pendingOrder;
                s.activationCondition = p.strings.intern(pendingActivation);
                s.logicCondition = p.strings.intern(pendingLogic);
                s.conditionOnProgram = !pendingActivation.empty() || !pendingLogic.empty();
                s.isSubroutine = pendingSubroutine;
                s.lineCount = countLines(*body);
                s.statementCount = countStatements(*body);
                s.owner = currentPou;
                if (opts.keepSourceBodies) s.body = std::move(*body);

                pendingSubroutine = false;
                p.sections.push_back(std::move(s));
                const auto sectionIndex = static_cast<Index>(p.sections.size() - 1);
                if (auto* pou = at(p.pous, currentPou)) pou->sections.push_back(sectionIndex);
                if (auto* task = at(p.tasks, currentTask)) task->sections.push_back(sectionIndex);

                if (currentPou >= p.pous.size()) {
                    // A bare task section: give it its own POU so the tree is uniform.
                    Pou pou;
                    pou.name = s.name;
                    pou.kind = PouKind::Section;
                    pou.sections.push_back(sectionIndex);
                    p.pous.push_back(std::move(pou));
                    p.sections[sectionIndex].owner = static_cast<Index>(p.pous.size() - 1);
                }

                pendingActivation.clear();
                pendingLogic.clear();
                if (progress && (out.elementsVisited % 64) == 0)
                    progress(ParseProgress{ "sections",
                                           std::min(1.f, static_cast<float>(r.line()) / totalLines),
                                           r.line() });
                continue;
            }

            // ---------------------------------------------------- animation tables ---
            if (el == "animationTable") {
                if (!opts.parseAnimationTables) { (void)r.skipElement(); continue; }
                AnimationTable a;
                a.name = p.strings.intern(r.attr("name"));
                a.owner = p.strings.intern(r.attr("PouOwner"));
                p.animationTables.push_back(std::move(a));
                continue;
            }
            if (el == "elementDescription" && !p.animationTables.empty()) {
                p.animationTables.back().entries.push_back({p.strings.intern(r.attr("name")), false});
                continue;
            }

            if (el == "instanceElementDesc" || el == "structure" || el == "logicConf") continue;

            if (opts.strict)
                return core::fail(core::ErrorCode::XmlMalformed,
                    "line " + std::to_string(r.line()) + ": unexpected <"
                    + std::string(el) + ">");
        }

        // --- link step -----------------------------------------------------------
        // Sections are declared in <taskDesc> but their bodies arrive later in the
        // document, after that element has closed. Bind them by task name now that
        // both halves are known, preserving the declared execution order.
        // <programUnitDesc> declares which task runs a program unit and in what
        // order; without it a re-export cannot reproduce the task descriptor.
        for (const auto& [name, order, taskIndex] : pendingUnitOrders) {
            const auto id = p.strings.intern(name);
            for (auto& pou : p.pous)
                if (pou.name == id && pou.kind == PouKind::ProgramUnit) {
                    pou.order = order;
                    if (taskIndex != kNoIndex && taskIndex < p.tasks.size())
                        pou.task = p.tasks[taskIndex].name;
                    break;
                }
        }

        for (auto& t : p.tasks) t.sections.clear();
        for (Index i = 0; i < p.sections.size(); ++i) {
            const auto taskName = p.sections[i].task;
            if (taskName == 0) continue;              // a POU body, not a task section
            for (auto& t : p.tasks)
                if (t.name == taskName) { t.sections.push_back(i); break; }
        }
        // 1.8.0 : les conditions des <sectionDesc>, sur la section de meme nom de la
        // meme tache (une condition deja lue sur <program> reste).
        for (const auto& [name, taskIndex, activation, logic] : pendingSectionDescs) {
            if (taskIndex == kNoIndex || taskIndex >= p.tasks.size()) continue;
            const auto id = p.strings.intern(name);
            for (const auto i : p.tasks[taskIndex].sections) {
                auto& s = p.sections[i];
                if (s.name != id) continue;
                if (s.activationCondition == 0 && !activation.empty()) s.activationCondition = p.strings.intern(activation);
                if (s.logicCondition == 0 && !logic.empty()) s.logicCondition = p.strings.intern(logic);
                break;
            }
        }
        // stable_sort, not sort: several sections legitimately share a SectionOrder,
        // and an unstable sort gave them a different relative order on every run -
        // enough to make two exports of the same project differ.
        for (auto& t : p.tasks)
            std::stable_sort(t.sections.begin(), t.sections.end(), [&](Index a, Index b) {
            return p.sections[a].order < p.sections[b].order;
                });

        // The resolution itself now lives on Project, because three places need it:
        // this parser, the shared-library import, and a macro that creates a
        // variable. The diagnostics stay here - only a file being READ can say which
        // of its declarations named a type nobody provides.
        p.linkTypes();
        for (auto& v : p.variables) {
            const auto elemName = v.type.elementType ? v.type.elementType : v.type.name;
            if (p.typeByName.count(elemName) || p.pouByName.count(elemName)) {
                // resolved by linkTypes
            }
            else if (v.type.klass == TypeClass::Unknown) {
                // A type the platform provides is not a problem, it is simply not in
                // this file. Warning about TON 27 times buried the real findings.
                note(ParseDiagnostic::Level::Info,
                    "type '" + std::string(p.strings.text(elemName)) + "' declared by '"
                    + std::string(p.strings.text(v.name))
                    + "' is not defined in this export; it comes from a Control Expert library");
            }
        }
        for (auto& lib : p.libraries)
            if (auto* pou = at(p.pous, lib.pouIndex)) lib.usageCount = pou->instanceCount;

        out.bytesRead = buffer.size();
        out.milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (progress) progress(ParseProgress{ "done", 1.f, buffer.size() });
        return out;
    }

    // --------------------------------------------------------------- stubs ------
    bool XdbParser::canParse(std::string_view h) const noexcept {
        return h.find("VariableDeclarationFile") != std::string_view::npos
            || h.find("DataExchangeFile") != std::string_view::npos;
    }
    core::Result<ParseOutcome> XdbParser::parse(std::string_view, Project&,
        const ParseOptions&, const ProgressFn&) {
        return core::fail(core::ErrorCode::NotImplemented,
            "the .XDB variable dictionary parser is not part of this milestone");
    }

    // -------------------------------------------------------------- catalog -----
    namespace {
        // Normalised for lookup. The same part is spelled three ways across Schneider
        // exports: "BMX P34 2020" in the program file, "BMXP342020" in the hardware
        // file, and "BMXNOC0401.2" where the trailing ".2" is a hardware revision
        // rather than part of the reference. Strip separators, upper-case the rest.
        std::string normaliseReference(std::string_view s) {
            std::string out;
            out.reserve(s.size());
            for (char ch : s)
                if (ch != ' ' && ch != '-' && ch != '_' && ch != '.')
                    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            return out;
        }
    } // namespace

    const HardwareCatalog::ModuleEntry* HardwareCatalog::findModule(std::string_view reference) const {
        const auto key = normaliseReference(reference);
        for (const auto& m : modules_)
            if (normaliseReference(m.reference) == key) return &m;
        return nullptr;
    }

    const HardwareCatalog::Cpu* HardwareCatalog::findCpu(std::string_view reference) const {
        const auto key = normaliseReference(reference);
        for (const auto& cpu : cpus_)
            if (normaliseReference(cpu.reference) == key) return &cpu;
        return nullptr;
    }

    std::uint16_t HardwareCatalog::maxSlotsForRacks(const Cpu& cpu, std::uint16_t rackCount) {
        // 11 usable slots on one rack, then 12 more per additional rack: 23, 35, 47.
        // The arithmetic is Schneider's, not an extrapolation - their catalogue
        // lists exactly those four numbers for the BMX P34 20x0 range.
        if (cpu.maxRacks == 0) return 0;
        const auto racks = std::min<std::uint16_t>(rackCount, cpu.maxRacks);
        return racks == 0 ? 0 : static_cast<std::uint16_t>(11 + 12 * (racks - 1));
    }

    std::vector<const HardwareCatalog::ModuleEntry*>
        HardwareCatalog::modulesStartingWith(std::string_view prefix) const {
        const auto key = normaliseReference(prefix);
        std::vector<const ModuleEntry*> out;
        for (const auto& m : modules_) {
            const auto ref = normaliseReference(m.reference);
            if (ref.size() >= key.size() && ref.compare(0, key.size(), key) == 0) out.push_back(&m);
        }
        return out;
    }

    std::vector<const HardwareCatalog::ModuleEntry*> HardwareCatalog::powerSupplies() const {
        // The supplies are the CPS family. Recognised by reference rather than by a
        // separate flag, so adding one to the catalogue file is enough to make it
        // appear in the dialog.
        auto out = modulesStartingWith("BMXCPS");
        for (auto* m : modulesStartingWith("BMECPS")) out.push_back(m);
        return out;
    }

    std::vector<const HardwareCatalog::ModuleEntry*> HardwareCatalog::backplanes() const {
        std::vector<const ModuleEntry*> out;
        for (const auto& m : modules_)
            if (m.slots > 0) out.push_back(&m);
        return out;
    }

    HardwareCatalog HardwareCatalog::builtinFallback() {
        HardwareCatalog c;
        // Figures from Schneider's Modicon M340 documentation and product pages.
        // reference / family / range / maxRacks / slotsPerRack / ports
        //   / max discrete / max analog / specific channels / program kB / data kB
        c.cpus_ = {
            {"BMX P34 1000", "Modicon M340", "BMX", 1, 12, {"Modbus serial"},
             256, 128, 20, 2048, 256},
            {"BMX P34 2000", "Modicon M340", "BMX", 4, 12, {"Modbus serial"},
             1024, 256, 36, 4096, 256},
            {"BMX P34 2010", "Modicon M340", "BMX", 4, 12, {"CANopen", "Modbus serial"},
             1024, 256, 36, 4096, 256},
            {"BMX P34 2020", "Modicon M340", "BMX", 4, 12, {"Ethernet TCP/IP", "Modbus serial"},
             1024, 256, 36, 4096, 256},
            {"BMX P34 2030", "Modicon M340", "BMX", 4, 12, {"Ethernet TCP/IP", "CANopen"},
             1024, 256, 36, 4096, 256},
            {"BME P58 2040", "Modicon M580", "BME", 8, 16, {"Ethernet TCP/IP"}, 0, 0, 0, 0, 0},
            {"BME P58 3040", "Modicon M580", "BME", 8, 16, {"Ethernet TCP/IP"}, 0, 0, 0, 0, 0},
            {"TSX P57 204M", "Premium",      "TSX", 4, 12, {"Modbus serial"}, 0, 0, 0, 0, 0},
        };

        // Modicon M340. This is only a seed so the application works with no
        // resource file present; resources/plc_catalog.txt carries the full
        // catalogue transcribed from Control Expert and overrides every entry here.
        //
        // The BMXDDM16022 line below used to say 16 inputs / 8 outputs, inferred
        // from the "1602" in the reference. It is 8 and 8. The export could not
        // contradict it - one channel group per direction has no stride to measure -
        // so the guess stood until the vendor catalogue was checked. Inferring a
        // figure from a part number is not the same as knowing it.
        c.modules_ = {
            // reference        family          description                                        in  out slots
            {"BMXP342020",  "Modicon M340", "Processor, Ethernet TCP/IP + Modbus serial",           0,  0,  0},
            {"BMXP342000",  "Modicon M340", "Processor, Modbus serial",                             0,  0,  0},
            {"BMXP342030",  "Modicon M340", "Processor, Ethernet TCP/IP + CANopen",                 0,  0,  0},
            {"BMXCPS2000",  "Modicon M340", "Power supply, 100-240 VAC, 20 W",                      0,  0,  0},
            {"BMXCPS3500",  "Modicon M340", "Power supply, 100-240 VAC, 36 W",                      0,  0,  0},
            {"BMXCPS3020",  "Modicon M340", "Power supply, 24-48 VDC, 32 W",                        0,  0,  0},
            {"BMXCPS2010",  "Modicon M340", "Power supply, 24 VDC, 16.8 W",                         0,  0,  0},
            {"BMXCPS3540T", "Modicon M340", "Power supply, 125 VDC, 36 W",                          0,  0,  0},
            {"BMXXBP0400",  "Modicon M340", "Backplane, 4 slots",                                   0,  0,  4},
            {"BMXXBP0600",  "Modicon M340", "Backplane, 6 slots",                                   0,  0,  6},
            {"BMXXBP0800",  "Modicon M340", "Backplane, 8 slots",                                   0,  0,  8},
            {"BMXXBP1200",  "Modicon M340", "Backplane, 12 slots",                                  0,  0, 12},
            {"BMEXBP1200",  "Modicon M580", "Ethernet backplane, 12 slots",                         0,  0, 12},
            {"BMEXBP0800",  "Modicon M580", "Ethernet backplane, 8 slots",                          0,  0,  8},
            {"BMXDDI1602",  "Modicon M340", "Discrete input, 16 points, 24 VDC sink",              16,  0,  0},
            {"BMXDDI3202K", "Modicon M340", "Discrete input, 32 points, 24 VDC sink",              32,  0,  0},
            {"BMXDDI6402K", "Modicon M340", "Discrete input, 64 points, 24 VDC sink",              64,  0,  0},
            {"BMXDDO1602",  "Modicon M340", "Discrete output, 16 points, transistor source",        0, 16,  0},
            {"BMXDDO3202K", "Modicon M340", "Discrete output, 32 points, transistor source",        0, 32,  0},
            {"BMXDDO6402K", "Modicon M340", "Discrete output, 64 points, transistor source",        0, 64,  0},
            {"BMXDAO1605",  "Modicon M340", "Discrete output, 16 points, triac",                    0, 16,  0},
            {"BMXDRA1605",  "Modicon M340", "Discrete output, 16 points, relay",                    0, 16,  0},
            {"BMXDDM16022", "Modicon M340", "Dig 8E 24 Vdc, 8S source transistor",                 8,  8,  0},
            {"BMXDDM16025", "Modicon M340", "Dig 8E 24 Vdc, 8S relais",                            8,  8,  0},
            {"BMXDDM3202K", "Modicon M340", "Dig 16E 24 Vdc, 16S source transistor",              16, 16,  0},
            {"BMXAMI0410",  "Modicon M340", "Analog input, 4 channels, high level, isolated",       4,  0,  0},
            {"BMXAMI0800",  "Modicon M340", "Analog input, 8 channels, high level",                 8,  0,  0},
            {"BMXAMI0810",  "Modicon M340", "Analog input, 8 channels, high level, isolated",       8,  0,  0},
            {"BMXAMO0210",  "Modicon M340", "Analog output, 2 channels, isolated",                  0,  2,  0},
            {"BMXAMO0410",  "Modicon M340", "Analog output, 4 channels, isolated",                  0,  4,  0},
            {"BMXART0414",  "Modicon M340", "Analog input, 4 channels, temperature",                4,  0,  0},
            {"BMXNOC0401",  "Modicon M340", "Ethernet/IP - Modbus TCP network module, 4 ports",     0,  0,  0},
            {"BMXNOC04012", "Modicon M340", "Ethernet/IP - Modbus TCP network module, 4 ports",     0,  0,  0},
            {"BMXNOE0100",  "Modicon M340", "Ethernet TCP/IP module",                               0,  0,  0},
            {"BMXNOE0110",  "Modicon M340", "Ethernet TCP/IP module with web server",               0,  0,  0},
            {"BMXNRP0200",  "Modicon M340", "Fibre-optic converter, 2 channels, multimode",         0,  0,  0},
            {"BMXNRP0201",  "Modicon M340", "Fibre-optic converter, 2 channels, singlemode",        0,  0,  0},
            {"BMXNOM0200",  "Modicon M340", "Serial link module, 2 ports",                          0,  0,  0},
            {"BMXEHC0200",  "Modicon M340", "Counting module, 2 channels",                          0,  0,  0},
            {"BMXEHC0800",  "Modicon M340", "Counting module, 8 channels",                          0,  0,  0},
            {"BMXMSP0200",  "Modicon M340", "Motion module, 2 axes",                                0,  0,  0},
        };
        return c;
    }

    core::Result<HardwareCatalog> HardwareCatalog::loadFromFile(const std::string& path) {
        // resources/plc_catalog.txt, one module per line:
        //   REFERENCE ; family ; description ; inputs ; outputs ; slots
        // Lines starting with '#' are comments. Entries replace built-in ones with
        // the same reference, so a site can correct or extend the table without a
        // rebuild - which is the whole point of not compiling this data in.
        auto catalog = builtinFallback();

        auto text = readFile(path, 4u * 1024 * 1024);
        if (!text) return catalog;                     // no file: the seed table stands

        std::size_t start = 0;
        while (start <= text->size()) {
            const auto nl = text->find('\n', start);
            const auto line = std::string_view(*text).substr(
                start, (nl == std::string::npos ? text->size() : nl) - start);
            start = (nl == std::string::npos) ? text->size() + 1 : nl + 1;

            if (line.empty() || line.front() == '#') continue;

            std::vector<std::string> fields;
            std::size_t from = 0;
            while (from <= line.size()) {
                const auto sep = line.find(';', from);
                auto piece = line.substr(from, (sep == std::string_view::npos ? line.size() : sep) - from);
                while (!piece.empty() && (piece.front() == ' ' || piece.front() == '\t')) piece.remove_prefix(1);
                while (!piece.empty() && (piece.back() == ' ' || piece.back() == '\r' || piece.back() == '\t'))
                    piece.remove_suffix(1);
                fields.emplace_back(piece);
                if (sep == std::string_view::npos) break;
                from = sep + 1;
            }
            if (fields.size() < 3) continue;

            ModuleEntry e;
            e.reference = fields[0];
            e.family = fields[1];
            e.description = fields[2];
            if (fields.size() > 3) e.inputPoints = static_cast<std::uint16_t>(std::atoi(fields[3].c_str()));
            if (fields.size() > 4) e.outputPoints = static_cast<std::uint16_t>(std::atoi(fields[4].c_str()));
            if (fields.size() > 5) e.slots = static_cast<std::uint16_t>(std::atoi(fields[5].c_str()));

            auto it = std::find_if(catalog.modules_.begin(), catalog.modules_.end(),
                [&](const ModuleEntry& m) {
                    return normaliseReference(m.reference) == normaliseReference(e.reference);
                });
            if (it != catalog.modules_.end()) *it = std::move(e);
            else catalog.modules_.push_back(std::move(e));
        }
        return catalog;
    }

    core::Result<std::pair<HardwareCatalog::Cpu, std::string>>
        HardwareCatalog::resolve(std::string_view resIdent) const {
        // "BMX P34 2020 03.30" -> reference + trailing OS version.
        for (const auto& cpu : cpus_) {
            if (resIdent.size() < cpu.reference.size()) continue;
            if (resIdent.compare(0, cpu.reference.size(), cpu.reference) != 0) continue;
            std::string firmware(resIdent.substr(cpu.reference.size()));
            while (!firmware.empty() && firmware.front() == ' ') firmware.erase(firmware.begin());
            return std::pair<Cpu, std::string>{cpu, firmware};
        }
        return core::fail(core::ErrorCode::UnknownCpuReference, std::string(resIdent));
    }

} // namespace importer